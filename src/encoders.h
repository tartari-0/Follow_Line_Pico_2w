#ifndef ENCODERS_H
#define ENCODERS_H

#include <stdint.h>

void encoders_init(void);

// Contagem absoluta de pulsos (4X) do motor 1 ou 2, lida na hora do PIO
int32_t encoder_get_count(int motor);

// Lê os dois encoders e atualiza a estimativa de velocidade.
// Chamar exatamente uma vez por ciclo do loop de controle.
void encoders_update(void);

// Valores calculados no último encoders_update():
int32_t encoder_count(int motor); // Contagem (pulsos)
float encoder_speed(int motor);   // Velocidade da roda (rad/s), média na janela ENC_SPEED_WINDOW

#endif
