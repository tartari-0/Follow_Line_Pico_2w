#!/usr/bin/env python3
"""Executa um ensaio em malha aberta no robô e salva os dados em CSV.

O sinal é gerado dentro do robô (loop de 1 kHz); o PC só dispara o ensaio e
recebe a telemetria. Conecte o PC na rede "FollowLine-Pico" antes.

Exemplos:
    python3 tools/acquire.py "step 1 40 2000" --nota "suspenso, bateria cheia"
    python3 tools/acquire.py "stairs ambos 100 10 2000" --repeticoes 3
    python3 tools/acquire.py "prbs 2 50 20 50 20000" -o dados/prbs_m2.csv

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
e <arquivo>.json com o comando, a data, a nota e os parâmetros do firmware.
"""

import argparse
import csv
import json
import re
import socket
import sys
import threading
import time
from pathlib import Path

from robot_protocol import FLAG_EXPERIMENT, ROBOT_IP, ROBOT_PORT, is_telemetry, parse_telemetry

KEEPALIVE_S = 0.3
TRAILING_S = 0.5     # Espera pacotes atrasados depois do "exp fim"
MAX_WAIT_S = 180.0

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
        """Executa o ensaio e retorna (amostras, estatísticas)."""
        samples = []
        expected_seq = None
        lost_packets = 0
        dropped = 0
        finished_at = None
        result = None

        self.send(f"exp {command}")
        start = time.time()
        while True:
            now = time.time()
            if finished_at and now - finished_at > TRAILING_S:
                break
            if now - start > MAX_WAIT_S:
                result = result or "timeout esperando o fim do ensaio"
                break

            data = self.recv()
            if data is None:
                continue
            if is_telemetry(data):
                seq, dropped, packet = parse_telemetry(data)
                if expected_seq is not None and seq != expected_seq:
                    lost_packets += (seq - expected_seq) & 0xFFFFFFFF
                expected_seq = (seq + 1) & 0xFFFFFFFF
                samples.extend(s for s in packet if s["flags"] & FLAG_EXPERIMENT)
                continue

            text = data.decode(errors="replace")
            print(f"  robô: {text}")
            if text.startswith("exp fim"):
                finished_at = time.time()
                result = "ok"
            elif text.startswith(("exp erro", "exp abortado", "link perdido")):
                result = text
                break

        self.send("stream 0")
        return samples, {"resultado": result, "pacotes_perdidos": lost_packets,
                         "amostras_descartadas_no_robo": dropped}

    def close(self):
        self.alive = False
        self.send("stop")


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


def check_sampling(samples, period_us):
    """Conta amostras faltando pelo intervalo entre os carimbos de tempo."""
    missing = 0
    for a, b in zip(samples, samples[1:]):
        dt = (b["t_us"] - a["t_us"]) & 0xFFFFFFFF
        missing += max(0, round(dt / period_us) - 1)
    return missing


def default_name(command):
    slug = re.sub(r"[^a-zA-Z0-9]+", "_", command).strip("_")
    return Path("dados") / f"{time.strftime('%Y%m%d_%H%M%S')}_{slug}.csv"


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
    parser.add_argument("-o", "--saida", type=Path, help="arquivo CSV (padrão: dados/<data>_<ensaio>.csv)")
    parser.add_argument("--nota", default="", help="anotação salva no .json (condição do ensaio)")
    parser.add_argument("--repeticoes", type=int, default=1)
    parser.add_argument("--intervalo", type=float, default=5.0, help="pausa entre repetições (s)")
    parser.add_argument("--ip", default=ROBOT_IP)
    parser.add_argument("--port", type=int, default=ROBOT_PORT)
    args = parser.parse_args()

    base = args.saida or default_name(args.ensaio)
    acq = Acquisition(args.ip, args.port)
    try:
        info = acq.query_info()
        print(f"robô: {info}")
        period_us = int(info.get("loop_us", 1000))

        for rep in range(1, args.repeticoes + 1):
            path = base if args.repeticoes == 1 else base.with_name(f"{base.stem}_r{rep}{base.suffix}")
            print(f"[{rep}/{args.repeticoes}] exp {args.ensaio}")
            samples, stats = acq.run(args.ensaio)
            missing = check_sampling(samples, period_us)

            metadata = {
                "ensaio": args.ensaio,
                "repeticao": rep,
                "data": time.strftime("%Y-%m-%d %H:%M:%S"),
                "nota": args.nota,
                "firmware": info,
                "amostras": len(samples),
                "amostras_faltando": missing,
                **stats,
            }
            save(path, to_rows(samples), metadata)
            print(f"  {len(samples)} amostras salvas em {path}")
            if stats["resultado"] != "ok":
                print(f"  ATENÇÃO: ensaio não terminou normalmente ({stats['resultado']})")
                break
            if missing or stats["pacotes_perdidos"]:
                print(f"  ATENÇÃO: {missing} amostras faltando ({stats['pacotes_perdidos']} pacotes perdidos)."
                      " Considere repetir o ensaio.")
            if rep < args.repeticoes:
                time.sleep(args.intervalo)
    except KeyboardInterrupt:
        print("\ninterrompido: parando o ensaio")
        acq.send("exp stop")
    finally:
        acq.close()


if __name__ == "__main__":
    sys.exit(main())
