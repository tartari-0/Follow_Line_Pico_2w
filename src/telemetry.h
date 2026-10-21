#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>
#include "config.h"

// Formato binário (little-endian) enviado por UDP. Precisa bater com tools/robot_link.py.

#define TELEMETRY_MAGIC 0xA55A

typedef struct __attribute__((packed)) {
    uint32_t t_us;                       // Instante da amostra (desde o boot)
    int32_t  enc[2];                     // Contagem absoluta dos encoders 1 e 2
    int16_t  pwm[2];                     // Comando dos motores 1 e 2 (-1000..1000 = -100..100 %)
    uint16_t line_raw[LINE_NUM_SENSORS]; // Tempo de descarga RC (us)
    uint16_t side[2];                    // ADC dos sensores laterais (esq, dir)
    uint16_t vbat_mv;                    // Bateria (mV)
    uint16_t flags;                      // bit 0: motores habilitados
} telemetry_sample_t;

#define TELEMETRY_SAMPLES_PER_PACKET 32

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint16_t count;
    uint32_t seq;      // Nº do pacote, para detectar perdas
    uint32_t dropped;  // Amostras descartadas por fila cheia (acumulado)
    telemetry_sample_t samples[TELEMETRY_SAMPLES_PER_PACKET];
} telemetry_packet_t;

_Static_assert(sizeof(telemetry_sample_t) == 40, "atualize tools/robot_link.py");
_Static_assert(sizeof(telemetry_packet_t) <= 1472, "pacote maior que o MTU UDP");

#endif
