#!/usr/bin/env python3
"""Terminal de comunicação com o robô via Wi-Fi (UDP).

Conecte o PC na rede "FollowLine-Pico" e rode:
    python3 tools/robot_link.py

Digite comandos do robô (help, status, motor 1 30, stream 1, ...) ou comandos locais:
    :log <arquivo.csv>   grava a telemetria recebida em CSV
    :stoplog             para a gravação
    :quit                sai (para os motores antes)
"""

import argparse
import csv
import socket
import struct
import sys
import threading
import time

ROBOT_IP = "192.168.4.1"
ROBOT_PORT = 4210
KEEPALIVE_S = 0.3

# Deve bater com src/telemetry.h
TELEMETRY_MAGIC = 0xA55A
HEADER = struct.Struct("<HHII")
SAMPLE = struct.Struct("<I2i2h8H2HHH")
CSV_COLUMNS = (["t_us", "enc1", "enc2", "pwm1", "pwm2"]
               + [f"line{i}" for i in range(8)]
               + ["side_left", "side_right", "vbat_mv", "flags"])


class RobotLink:
    def __init__(self, ip, port):
        self.addr = (ip, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(0.5)
        self.running = True
        self.lock = threading.Lock()
        self.csv_file = None
        self.csv_writer = None
        self.samples_logged = 0
        self.expected_seq = None
        self.lost_packets = 0

    def send(self, text):
        self.sock.sendto(text.encode(), self.addr)

    def start_log(self, path):
        with self.lock:
            self.stop_log()
            self.csv_file = open(path, "w", newline="")
            self.csv_writer = csv.writer(self.csv_file)
            self.csv_writer.writerow(CSV_COLUMNS)
            self.samples_logged = 0
            self.lost_packets = 0
            self.expected_seq = None
        print(f"[gravando em {path}]")

    def stop_log(self):
        if self.csv_file:
            self.csv_file.close()
            print(f"[gravacao encerrada: {self.samples_logged} amostras, "
                  f"{self.lost_packets} pacotes perdidos]")
            self.csv_file = None
            self.csv_writer = None

    def handle_telemetry(self, data):
        magic, count, seq, dropped = HEADER.unpack_from(data)
        if self.expected_seq is not None and seq != self.expected_seq:
            self.lost_packets += (seq - self.expected_seq) & 0xFFFFFFFF
        self.expected_seq = (seq + 1) & 0xFFFFFFFF

        with self.lock:
            if not self.csv_writer:
                return
            for i in range(count):
                self.csv_writer.writerow(SAMPLE.unpack_from(data, HEADER.size + i * SAMPLE.size))
            self.samples_logged += count

    def receiver(self):
        while self.running:
            try:
                data, _ = self.sock.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                break
            if len(data) >= HEADER.size and struct.unpack_from("<H", data)[0] == TELEMETRY_MAGIC:
                self.handle_telemetry(data)
            else:
                print(f"\r< {data.decode(errors='replace')}\n> ", end="", flush=True)

    def keepalive(self):
        # Mantém o link vivo: o robô para os motores se ficar 1 s sem comandos
        while self.running:
            try:
                self.send("ping")
            except OSError:
                pass
            time.sleep(KEEPALIVE_S)

    def run(self):
        threading.Thread(target=self.receiver, daemon=True).start()
        threading.Thread(target=self.keepalive, daemon=True).start()
        self.send("help")
        try:
            while True:
                line = input("> ").strip()
                if not line:
                    continue
                if line == ":quit":
                    break
                elif line.startswith(":log"):
                    parts = line.split(maxsplit=1)
                    self.start_log(parts[1] if len(parts) > 1 else time.strftime("log_%Y%m%d_%H%M%S.csv"))
                elif line == ":stoplog":
                    with self.lock:
                        self.stop_log()
                else:
                    self.send(line)
        except (KeyboardInterrupt, EOFError):
            print()
        finally:
            self.send("stop")
            self.send("stream 0")
            self.running = False
            with self.lock:
                self.stop_log()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ip", default=ROBOT_IP)
    parser.add_argument("--port", type=int, default=ROBOT_PORT)
    args = parser.parse_args()
    RobotLink(args.ip, args.port).run()


if __name__ == "__main__":
    sys.exit(main())
