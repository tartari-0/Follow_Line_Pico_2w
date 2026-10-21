#ifndef LINE_SENSOR_H
#define LINE_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// Inicia a leitura contínua da barra QRE-8D em segundo plano (PIO + DMA).
void line_init(void);

// Copia a leitura mais recente: tempo de descarga de cada sensor em us
// (LINE_RC_TIMEOUT = não descarregou, fundo preto).
// Retorna o número de sequência da leitura (incrementa a cada nova leitura).
uint32_t line_get_raw(uint16_t raw[LINE_NUM_SENSORS]);

// Tempo máximo de amostragem (us). Define também a taxa de leitura:
// período ≈ 15 us (carga) + timeout.
void line_set_timeout_us(uint16_t timeout_us);
uint16_t line_get_timeout_us(void);

// Calibração: min = tempo sobre o branco, max = tempo sobre o preto
void line_calibrate_reset(void);
void line_calibrate_update(const uint16_t raw[LINE_NUM_SENSORS]);
void line_get_calibration(uint16_t min[LINE_NUM_SENSORS], uint16_t max[LINE_NUM_SENSORS]);

// Normaliza: 0 = fundo preto, 1000 = linha branca
void line_normalize(const uint16_t raw[LINE_NUM_SENSORS], uint16_t out[LINE_NUM_SENSORS]);

// Posição da linha: -3500 (sensor 0) a +3500 (sensor 7), 0 = centro.
// Retorna false se nenhum sensor vê a linha (mantém a última posição).
bool line_position(const uint16_t norm[LINE_NUM_SENSORS], int32_t *position);

#endif
