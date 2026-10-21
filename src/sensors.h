#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

void sensors_init(void);

// Tempos brutos de descarga RC (us) da barra QRE-8D
void line_read_raw(uint16_t raw[LINE_NUM_SENSORS]);

// Calibração: chame várias vezes passando o robô sobre a linha e o fundo
void line_calibrate_reset(void);
void line_calibrate_step(void);
void line_print_calibration(void);

// Valores calibrados: 0 = fundo preto, 1000 = linha branca
void line_read_calibrated(uint16_t values[LINE_NUM_SENSORS]);

// Posição da linha: -3500 (extrema esquerda) a +3500 (extrema direita), 0 = centro.
// Retorna false se nenhum sensor vê a linha (mantém a última posição).
bool line_read_position(int32_t *position);

// Sensores laterais (ADC 12 bits, 0..4095)
uint16_t side_read_left(void);
uint16_t side_read_right(void);

#endif
