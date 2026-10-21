#ifndef ENCODERS_H
#define ENCODERS_H

#include <stdint.h>

void encoders_init(void);

// Contagem absoluta de pulsos (4X) do motor 1 ou 2
int32_t encoder_get_count(int motor);

#endif
