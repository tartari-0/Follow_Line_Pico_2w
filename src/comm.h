#ifndef COMM_H
#define COMM_H

#include <stdbool.h>
#include <stddef.h>
#include "telemetry.h"

#define COMM_CMD_MAX_LEN 64

// Cria as filas entre os núcleos. Chamar antes de iniciar o núcleo de controle.
void comm_init(void);

// Tarefa de comunicação (núcleo 0, nunca retorna): Wi-Fi (Access Point + UDP)
// e terminal USB. Chamar depois de iniciar os PIOs do robô (o driver do Wi-Fi
// também usa um PIO). O cyw43 precisa ser iniciado no núcleo 0: no núcleo 1
// o chip não responde (do_ioctl timeout / CLM load failed).
void comm_run(void);

// Funções chamadas pelo núcleo de controle (todas não bloqueantes):

// Próximo comando de texto recebido (Wi-Fi ou USB), se houver
bool comm_get_command(char *buf, size_t len);

// Envia uma resposta de texto pelo Wi-Fi e pela USB (formato printf)
void comm_reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Enfileira uma amostra de telemetria. Retorna false se a fila estiver cheia.
bool comm_push_sample(const telemetry_sample_t *sample);

uint32_t comm_dropped_samples(void);

#endif
