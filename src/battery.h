#ifndef BATTERY_H
#define BATTERY_H

// Requer adc_init() (feito em sensors_init)
void battery_init(void);

// Tensão da bateria em volts
float battery_read_voltage(void);

#endif
