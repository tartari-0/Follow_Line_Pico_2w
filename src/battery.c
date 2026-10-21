#include "battery.h"
#include "config.h"
#include "hardware/adc.h"

void battery_init(void) {
    adc_gpio_init(VBAT_PIN);
}

float battery_read_voltage(void) {
    adc_select_input(VBAT_PIN - 26);

    // Média de algumas amostras para reduzir ruído dos motores
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += adc_read();

    float v_adc = (sum / 16.0f) * 3.3f / 4096.0f;
    return v_adc * VBAT_DIVIDER;
}
