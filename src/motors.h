#ifndef MOTORS_H
#define MOTORS_H

#include <stdbool.h>

void motors_init(void);

// Liga/desliga os drivers (pino STBY)
void motors_enable(bool enable);

// Velocidade de -100.0 a 100.0 (%). Negativo = ré.
void motor_set(int motor, float percent);
void motors_set(float left, float right);

// Freio ativo (curto nos terminais do motor)
void motors_brake(void);

#endif
