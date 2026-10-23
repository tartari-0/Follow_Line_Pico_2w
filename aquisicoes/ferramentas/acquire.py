#!/usr/bin/env python3
"""Executa um ensaio em malha aberta no robô e salva os dados em CSV.

O sinal é gerado dentro do robô (loop de 1 kHz); o PC só dispara o ensaio e
recebe a telemetria. Conecte o PC na rede "FollowLine-Pico" antes.

Exemplos (de qualquer pasta):
    python3 aquisicoes/ferramentas/acquire.py "step 1 40 2000" --campanha degraus_suspenso
    python3 aquisicoes/ferramentas/acquire.py "stairs ambos 100 10 2000" --repeticoes 3
    python3 aquisicoes/ferramentas/acquire.py "prbs 2 50 20 50 20000" -o meu_ensaio.csv

Por padrão os arquivos vão para aquisicoes/dados/<data>_<campanha>/<hora>_<ensaio>.csv

Ensaios (alvo: 1, 2, ambos, rot):
    step   <alvo> <pwm> <duração_ms>
    stairs <alvo> <pwm_max> <n_degraus> <ms_por_degrau>
    ramp   <alvo> <pwm_inicial> <pwm_final> <duração_ms>
    prbs   <alvo> <pwm_médio> <amplitude> <ms_por_bit> <duração_ms>

Gera <arquivo>.csv com as colunas:
    t_s     tempo desde o início do ensaio (s)
    pwm1/2  comando dos motores 1 (esq) e 2 (dir), em %
    enc1/2  contagem absoluta dos encoders (pulsos 4X)
    speed1/2 velocidade das rodas calculada no robô (rad/s)
    vbat    tensão da bateria (V)
    t_us    relógio do robô (us), para checar a regularidade da amostragem
e <arquivo>.json com o comando, a data, a nota, os parâmetros do firmware, o commit
do git do firmware e das ferramentas (para saber exatamente qual código gerou os dados)
e estatísticas da transmissão (retransmissões, amostras faltando, jitter).

As amostras perdidas no Wi-Fi são pedidas de novo ao robô, que guarda os últimos 5 s
do ensaio na RAM; ensaios de qualquer duração chegam completos.
"""

import argparse
import csv
import json
import re
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

from robot_protocol import ROBOT_IP, ROBOT_PORT, is_exp_packet, is_telemetry, parse_exp_packet

KEEPALIVE_S = 0.3
REQUEST_PERIOD_S = 0.1   # Período dos pedidos de retransmissão
STATUS_PERIOD_S = 1.0    # Consulta "exp estado" (caso o "exp fim" se perca)
REORDER_MARGIN = 100     # Só pede amostras com esse atraso (podem estar a caminho)
MAX_RANGES_PER_ROUND = 8
FINISH_TIMEOUT_S = 5.0   # Tempo para recuperar o que faltar depois do fim

DATA_DIR = Path(__file__).resolve().parent.parent / "dados"

CSV_COLUMNS = ["t_s", "pwm1", "pwm2", "enc1", "enc2", "speed1", "speed2", "vbat", "t_us"]


class Acquisition:
    def __init__(self, ip, port):
        self.addr = (ip, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(0.1)
        self.alive = True
        threading.Thread(target=self._keepalive, daemon=True).start()

    def _keepalive(self):
        # O robô para os motores (e aborta o ensaio) se ficar 1 s sem comandos
        while self.alive:
            try:
                self.send("ping")
            except OSError:
                pass
            time.sleep(KEEPALIVE_S)

    def send(self, text):
        self.sock.sendto(text.encode(), self.addr)

    def recv(self):
        try:
            data, _ = self.sock.recvfrom(2048)
            return data
        except socket.timeout:
            return None

    def query_info(self):
        """Lê os parâmetros do firmware (comando 'info')."""
        self.send("info")
        deadline = time.time() + 3.0
        while time.time() < deadline:
            data = self.recv()
            if data and not is_telemetry(data):
                text = data.decode(errors="replace")
                if text.startswith("info "):
                    return dict(item.split("=", 1) for item in text[5:].split())
        raise RuntimeError("o robô não respondeu: confira se o PC está na rede FollowLine-Pico")

    def run(self, command):
        """Executa o ensaio e retorna (amostras ordenadas, estatísticas).

        As amostras chegam numeradas. O que se perder no Wi-Fi é pedido de novo ao
        robô ("resend"), que guarda os últimos segundos do ensaio na RAM.
        """
        received = {}
        unavailable = set()
        highest = -1        # Maior índice recebido
        scan_from = 0       # Antes deste índice não falta nada
        exp_id = total = None
        finished_at = None
        result = None
        requests = resent_packets = 0
        last_request = last_status = last_data = start = time.time()

        self.send(f"exp {command}")
        while True:
            data = self.recv()
            now = time.time()
            if data is not None:
                if is_exp_packet(data):
                    pkt_id, pkt_total, samples = parse_exp_packet(data)
                    if exp_id is None:
                        exp_id = pkt_id
                    if pkt_id == exp_id:
                        total = pkt_total
                        if min(samples, default=highest + 1) <= highest:
                            resent_packets += 1
                        received.update(samples)
                        highest = max(highest, max(samples, default=-1))
                        last_data = now
                elif not is_telemetry(data):
                    text = data.decode(errors="replace")
                    fields = dict(f.split("=", 1) for f in text.split() if "=" in f)
                    if text.startswith("resend indisponivel"):
                        first, count = map(int, text.split()[2:4])
                        unavailable.update(range(first, first + count))
                        continue
                    if not text.startswith("exp estado"):
                        print(f"  robô: {text}")
                    if text.startswith(("exp inicio", "exp fim", "exp estado")) and "id" in fields:
                        exp_id = int(fields["id"])
                        total = int(fields["total"])
                    if text.startswith("exp fim") or (text.startswith("exp estado: fim") and exp_id):
                        finished_at = finished_at or now
                        result = "ok"
                    elif text.startswith(("exp erro", "exp abortado", "link perdido")):
                        result = text
                        break

            # Pede o que faltou (fora da margem de reordenação, ou tudo após o fim)
            if exp_id is not None and now - last_request > REQUEST_PERIOD_S:
                last_request = now
                limit = total if finished_at else highest - REORDER_MARGIN
                while scan_from in received or scan_from in unavailable:
                    scan_from += 1
                for first, count in missing_ranges(received, unavailable, scan_from, limit):
                    self.send(f"resend {exp_id} {first} {count}")
                    requests += 1

            if not finished_at and now - last_status > STATUS_PERIOD_S and now - last_data > 0.5:
                last_status = now
                self.send("exp estado")

            if finished_at and total is not None:
                if len(received) + len(unavailable) >= total:
                    break
                if now - finished_at > FINISH_TIMEOUT_S:
                    break
            if total is None and now - start > 5.0:
                result = result or "o robô não iniciou o ensaio"
                break

        samples = [received[i] for i in sorted(received) if total is None or i < total]
        stats = {
            "resultado": result,
            "amostras_previstas": total,
            "amostras_recebidas": len(samples),
            "pedidos_de_retransmissao": requests,
            "pacotes_retransmitidos": resent_packets,
            "amostras_irrecuperaveis": len(unavailable),
        }
        return samples, stats

    def close(self):
        self.alive = False
        self.send("stop")


def missing_ranges(received, unavailable, start, limit):
    """Até MAX_RANGES_PER_ROUND intervalos (início, quantidade) faltando em [start, limit)."""
    ranges = []
    i = start
    while i < limit and len(ranges) < MAX_RANGES_PER_ROUND:
        if i in received or i in unavailable:
            i += 1
            continue
        first = i
        while i < limit and i not in received and i not in unavailable and i - first < 480:
            i += 1
        ranges.append((first, i - first))
    return ranges


def to_rows(samples):
    if not samples:
        return []
    t0 = samples[0]["t_us"]
    rows = []
    for s in samples:
        rows.append([
            f"{((s['t_us'] - t0) & 0xFFFFFFFF) * 1e-6:.6f}",
            s["pwm1_x10"] / 10.0, s["pwm2_x10"] / 10.0,
            s["enc1"], s["enc2"],
            f"{s['speed1']:.4f}", f"{s['speed2']:.4f}",
            s["vbat_mv"] / 1000.0,
            s["t_us"],
        ])
    return rows


def sampling_jitter(samples, period_us):
    """Maior desvio (us) do intervalo entre amostras em relação ao período."""
    worst = 0
    for a, b in zip(samples, samples[1:]):
        dt = (b["t_us"] - a["t_us"]) & 0xFFFFFFFF
        worst = max(worst, abs(dt - period_us))
    return worst


def default_name(command, campaign):
    slug = re.sub(r"[^a-zA-Z0-9]+", "_", command).strip("_")
    folder = DATA_DIR / f"{time.strftime('%Y-%m-%d')}_{campaign}"
    return folder / f"{time.strftime('%H%M%S')}_{slug}.csv"


def git_version():
    """Commit das ferramentas no PC ("-dirty" = alterações não commitadas)."""
    try:
        return subprocess.run(["git", "describe", "--always", "--dirty", "--abbrev=7"],
                              cwd=Path(__file__).resolve().parent, capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "desconhecido"


def save(path, rows, metadata):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(CSV_COLUMNS)
        writer.writerows(rows)
    with open(path.with_suffix(".json"), "w") as f:
        json.dump(metadata, f, indent=2, ensure_ascii=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("ensaio", help='comando do ensaio, entre aspas (ex.: "step 1 40 2000")')
    parser.add_argument("-o", "--saida", type=Path,
                        help="arquivo CSV (padrão: aquisicoes/dados/<data>_<campanha>/<hora>_<ensaio>.csv)")
    parser.add_argument("--campanha", default="geral",
                        help="nome da série de ensaios, vira o nome da pasta (ex.: degraus_suspenso)")
    parser.add_argument("--nota", default="", help="anotação salva no .json (condição do ensaio)")
    parser.add_argument("--repeticoes", type=int, default=1)
    parser.add_argument("--intervalo", type=float, default=5.0, help="pausa entre repetições (s)")
    parser.add_argument("--ip", default=ROBOT_IP)
    parser.add_argument("--port", type=int, default=ROBOT_PORT)
    args = parser.parse_args()

    base = args.saida or default_name(args.ensaio, args.campanha)
    tools_git = git_version()
    acq = Acquisition(args.ip, args.port)
    try:
        info = acq.query_info()
        print(f"robô: {info}")
        fw_git = info.get("git", "desconhecido")
        if fw_git.endswith("-dirty"):
            print("ATENÇÃO: o firmware foi compilado com alterações não commitadas;"
                  " não será possível recuperar exatamente esse código depois.")
        elif fw_git.split("-")[0] != tools_git.split("-")[0]:
            print(f"aviso: firmware ({fw_git}) e ferramentas ({tools_git}) estão em commits diferentes")
        period_us = int(info.get("loop_us", 1000))

        for rep in range(1, args.repeticoes + 1):
            path = base if args.repeticoes == 1 else base.with_name(f"{base.stem}_r{rep}{base.suffix}")
            print(f"[{rep}/{args.repeticoes}] exp {args.ensaio}")
            samples, stats = acq.run(args.ensaio)
            total = stats["amostras_previstas"] or 0
            missing = total - len(samples)
            stats["jitter_max_us"] = sampling_jitter(samples, period_us)

            metadata = {
                "ensaio": args.ensaio,
                "repeticao": rep,
                "data": time.strftime("%Y-%m-%d %H:%M:%S"),
                "nota": args.nota,
                "firmware": info,
                "git_ferramentas": tools_git,
                "amostras": len(samples),
                "amostras_faltando": missing,
                **stats,
            }
            save(path, to_rows(samples), metadata)
            print(f"  {len(samples)} amostras salvas em {path}")
            if stats["resultado"] != "ok":
                print(f"  ATENÇÃO: ensaio não terminou normalmente ({stats['resultado']})")
                break
            if stats["pacotes_retransmitidos"]:
                print(f"  {stats['pacotes_retransmitidos']} pacotes recuperados por retransmissão")
            if missing:
                print(f"  ATENÇÃO: {missing} amostras faltando. Considere repetir o ensaio.")
            if rep < args.repeticoes:
                time.sleep(args.intervalo)
    except KeyboardInterrupt:
        print("\ninterrompido: parando o ensaio")
        acq.send("exp stop")
    finally:
        acq.close()


if __name__ == "__main__":
    sys.exit(main())
