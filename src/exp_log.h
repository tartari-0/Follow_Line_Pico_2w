#ifndef EXP_LOG_H
#define EXP_LOG_H

#include <stdint.h>
#include <stdbool.h>

// Registro das amostras de um ensaio em um buffer circular na RAM.
// O núcleo de controle escreve; o núcleo de comunicação transmite e retransmite
// as amostras que o PC não recebeu. Cada amostra tem um índice (0 = início do ensaio).

typedef struct __attribute__((packed)) {
    uint32_t t_us;      // Instante da amostra (desde o boot)
    int32_t  enc[2];    // Contagem absoluta dos encoders 1 e 2 (pulsos 4X)
    float    speed[2];  // Velocidade das rodas 1 e 2 (rad/s)
    int16_t  pwm[2];    // Comando dos motores 1 e 2 (-1000..1000 = -100..100 %)
    uint16_t vbat_mv;   // Bateria (mV)
    uint16_t flags;     // Igual à telemetria
} exp_sample_t;

// Núcleo de controle:
void exp_log_reset(uint32_t total_samples); // Início de um novo ensaio
void exp_log_add(const exp_sample_t *sample);

// Qualquer núcleo:
uint32_t exp_log_id(void);    // Identificador do ensaio atual (muda a cada reset)
uint32_t exp_log_count(void); // Amostras já registradas
uint32_t exp_log_total(void); // Total previsto de amostras do ensaio

// Copia a amostra de índice `index`. Retorna false se ela ainda não existe
// ou já foi sobrescrita no buffer circular.
bool exp_log_get(uint32_t index, exp_sample_t *out);

#endif
