#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>
#include "config.h"
#include "exp_log.h"

// Formato binário (little-endian) enviado por UDP. Precisa bater com aquisicoes/ferramentas/robot_protocol.py.

#define TELEMETRY_MAGIC 0xA55A

#define TELEMETRY_FLAG_MOTORS     (1u << 0)
#define TELEMETRY_FLAG_EXPERIMENT (1u << 1)

typedef struct __attribute__((packed)) {
    uint32_t t_us;                       // Instante da amostra (desde o boot)
    int32_t  enc[2];                     // Contagem absoluta dos encoders 1 e 2 (pulsos 4X)
    float    speed[2];                   // Velocidade das rodas 1 e 2 (rad/s)
    int16_t  pwm[2];                     // Comando dos motores 1 e 2 (-1000..1000 = -100..100 %)
    uint16_t line_raw[LINE_NUM_SENSORS]; // Tempo de descarga RC (us)
    uint16_t side[2];                    // ADC dos sensores laterais (esq, dir)
    uint16_t vbat_mv;                    // Bateria (mV, filtro passa-baixas de ~5 ms)
    uint16_t flags;                      // bit 0: motores habilitados, bit 1: ensaio em andamento
} telemetry_sample_t;

#define TELEMETRY_SAMPLES_PER_PACKET 28

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint16_t count;
    uint32_t seq;      // Nº do pacote, para detectar perdas
    uint32_t dropped;  // Amostras descartadas por fila cheia (acumulado)
    telemetry_sample_t samples[TELEMETRY_SAMPLES_PER_PACKET];
} telemetry_packet_t;

_Static_assert(sizeof(telemetry_sample_t) == 48, "atualize aquisicoes/ferramentas/robot_protocol.py");
_Static_assert(sizeof(telemetry_packet_t) <= 1472, "pacote maior que o MTU UDP");

// --- Pacote de ensaio (confiável: o PC pede retransmissão do que faltar) ---

#define EXP_PACKET_MAGIC 0xA55B
#define EXP_SAMPLES_PER_PACKET 48

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint16_t count;
    uint32_t exp_id;       // Identificador do ensaio
    uint32_t first_index;  // Índice da primeira amostra do pacote (0 = início do ensaio)
    uint32_t total;        // Total de amostras do ensaio
    exp_sample_t samples[EXP_SAMPLES_PER_PACKET];
} exp_packet_t;

_Static_assert(sizeof(exp_sample_t) == 28, "atualize aquisicoes/ferramentas/robot_protocol.py");
_Static_assert(sizeof(exp_packet_t) <= 1472, "pacote maior que o MTU UDP");

#endif
