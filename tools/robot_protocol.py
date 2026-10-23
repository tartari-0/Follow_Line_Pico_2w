"""Protocolo UDP do robô. Deve bater com src/telemetry.h."""

import struct

ROBOT_IP = "192.168.4.1"
ROBOT_PORT = 4210

TELEMETRY_MAGIC = 0xA55A
FLAG_MOTORS = 1 << 0
FLAG_EXPERIMENT = 1 << 1

# magic, count, seq, dropped
HEADER = struct.Struct("<HHII")
# t_us, enc[2], speed[2], pwm[2], line_raw[8], side[2], vbat_mv, flags
SAMPLE = struct.Struct("<I2i2f2h8H2HHH")
assert SAMPLE.size == 48

SAMPLE_FIELDS = (["t_us", "enc1", "enc2", "speed1", "speed2", "pwm1_x10", "pwm2_x10"]
                 + [f"line{i}" for i in range(8)]
                 + ["side_left", "side_right", "vbat_mv", "flags"])


def is_telemetry(data):
    return len(data) >= HEADER.size and struct.unpack_from("<H", data)[0] == TELEMETRY_MAGIC


def parse_telemetry(data):
    """Retorna (seq, dropped, [dict por amostra])."""
    _, count, seq, dropped = HEADER.unpack_from(data)
    samples = []
    for i in range(count):
        values = SAMPLE.unpack_from(data, HEADER.size + i * SAMPLE.size)
        samples.append(dict(zip(SAMPLE_FIELDS, values)))
    return seq, dropped, samples
