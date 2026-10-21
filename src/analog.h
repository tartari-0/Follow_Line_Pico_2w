#ifndef ANALOG_H
#define ANALOG_H

#include "pico/types.h"

// ADC em modo contínuo (round-robin ADC0..ADC2) com DMA.
// Os valores mais recentes ficam sempre disponíveis, sem custo de CPU.
void analog_init(void);

// Último valor de 12 bits (0..4095) do canal ADC0..ADC2
uint16_t analog_get(uint channel);

uint16_t side_left(void);
uint16_t side_right(void);
float battery_voltage(void);

#endif
