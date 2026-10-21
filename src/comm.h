#ifndef COMM_H
#define COMM_H

#include <stdbool.h>
#include <stddef.h>
#include "telemetry.h"

#define COMM_CMD_MAX_LEN 64

// Inicia o Wi-Fi (Access Point + UDP) no núcleo 1.
// Chamar depois de iniciar os PIOs do robô (o driver do Wi-Fi também usa um PIO).
void comm_init(void);

// Funções chamadas pelo núcleo 0 (todas não bloqueantes):

// Próximo comando de texto recebido por Wi-Fi, se houver
bool comm_get_command(char *buf, size_t len);

// Envia uma resposta de texto pelo Wi-Fi e pela USB (formato printf)
void comm_reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Enfileira uma amostra de telemetria. Retorna false se a fila estiver cheia.
bool comm_push_sample(const telemetry_sample_t *sample);

uint32_t comm_dropped_samples(void);

#endif
