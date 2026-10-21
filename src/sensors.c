#include "sensors.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

static const uint line_pins[LINE_NUM_SENSORS] = LINE_SENSOR_PINS;
static uint32_t line_mask;

// Linha branca reflete mais IR -> descarga rápida -> tempo MENOR
static uint16_t cal_min[LINE_NUM_SENSORS]; // tempo sobre o branco
static uint16_t cal_max[LINE_NUM_SENSORS]; // tempo sobre o preto

static int32_t last_position = 0;

void sensors_init(void) {
    line_mask = 0;
    for (int i = 0; i < LINE_NUM_SENSORS; i++) {
        gpio_init(line_pins[i]);
        gpio_set_dir(line_pins[i], GPIO_IN);
        line_mask |= 1u << line_pins[i];
    }

    gpio_init(LINE_LED_ON_PIN);
    gpio_set_dir(LINE_LED_ON_PIN, GPIO_OUT);
    gpio_put(LINE_LED_ON_PIN, 1); // Liga os emissores IR da barra

    adc_init();
    adc_gpio_init(SIDE_LEFT_PIN);
    adc_gpio_init(SIDE_RIGHT_PIN);

    line_calibrate_reset();
}

void line_read_raw(uint16_t raw[LINE_NUM_SENSORS]) {
    // 1. Carrega os capacitores (todos os pinos de uma vez)
    gpio_set_dir_out_masked(line_mask);
    gpio_set_mask(line_mask);
    sleep_us(LINE_RC_CHARGE_US);

    // 2. Vira entrada e cronometra a descarga
    gpio_set_dir_in_masked(line_mask);
    uint32_t start = time_us_32();
    uint32_t pending = line_mask;

    for (int i = 0; i < LINE_NUM_SENSORS; i++) raw[i] = LINE_RC_TIMEOUT_US;

    uint32_t elapsed = 0;
    while (pending && elapsed < LINE_RC_TIMEOUT_US) {
        elapsed = time_us_32() - start;
        uint32_t low = ~gpio_get_all() & pending;
        if (low) {
            for (int i = 0; i < LINE_NUM_SENSORS; i++) {
                if (low & (1u << line_pins[i])) raw[i] = (uint16_t)elapsed;
            }
            pending &= ~low;
        }
    }
}

void line_calibrate_reset(void) {
    for (int i = 0; i < LINE_NUM_SENSORS; i++) {
        cal_min[i] = LINE_RC_TIMEOUT_US;
        cal_max[i] = 0;
    }
}

void line_calibrate_step(void) {
    uint16_t raw[LINE_NUM_SENSORS];
    line_read_raw(raw);
    for (int i = 0; i < LINE_NUM_SENSORS; i++) {
        if (raw[i] < cal_min[i]) cal_min[i] = raw[i];
        if (raw[i] > cal_max[i]) cal_max[i] = raw[i];
    }
}

void line_print_calibration(void) {
    printf("Calibracao (us)  min/branco: ");
    for (int i = 0; i < LINE_NUM_SENSORS; i++) printf("%5u ", cal_min[i]);
    printf("\n                 max/preto:  ");
    for (int i = 0; i < LINE_NUM_SENSORS; i++) printf("%5u ", cal_max[i]);
    printf("\n");
}

void line_read_calibrated(uint16_t values[LINE_NUM_SENSORS]) {
    uint16_t raw[LINE_NUM_SENSORS];
    line_read_raw(raw);
    for (int i = 0; i < LINE_NUM_SENSORS; i++) {
        int32_t range = (int32_t)cal_max[i] - cal_min[i];
        int32_t v;
        if (range <= 0) {
            v = 0; // Sensor não calibrado
        } else {
            v = ((int32_t)cal_max[i] - raw[i]) * 1000 / range;
            if (v < 0) v = 0;
            if (v > 1000) v = 1000;
        }
        values[i] = (uint16_t)v;
    }
}

bool line_read_position(int32_t *position) {
    uint16_t v[LINE_NUM_SENSORS];
    line_read_calibrated(v);

    int32_t weighted = 0;
    int32_t sum = 0;
    bool on_line = false;

    for (int i = 0; i < LINE_NUM_SENSORS; i++) {
        if (v[i] > 200) on_line = true;
        if (v[i] > 50) { // Ignora ruído
            weighted += (int32_t)v[i] * (i * 1000);
            sum += v[i];
        }
    }

    if (on_line && sum > 0) {
        last_position = weighted / sum - (LINE_NUM_SENSORS - 1) * 500;
    }
    *position = last_position;
    return on_line;
}

uint16_t side_read_left(void) {
    adc_select_input(SIDE_LEFT_PIN - 26);
    return adc_read();
}

uint16_t side_read_right(void) {
    adc_select_input(SIDE_RIGHT_PIN - 26);
    return adc_read();
}
