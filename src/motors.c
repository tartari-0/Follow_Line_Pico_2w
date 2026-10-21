#include "motors.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

typedef struct {
    uint pwm_pin;
    uint in1_pin;
    uint in2_pin;
    bool invert;
} motor_t;

static const motor_t motors[2] = {
    {M1_PWM_PIN, M1_IN1_PIN, M1_IN2_PIN, M1_INVERT},
    {M2_PWM_PIN, M2_IN1_PIN, M2_IN2_PIN, M2_INVERT},
};

static uint16_t pwm_wrap;

static void setup_motor(const motor_t *m) {
    gpio_init(m->in1_pin);
    gpio_init(m->in2_pin);
    gpio_set_dir(m->in1_pin, GPIO_OUT);
    gpio_set_dir(m->in2_pin, GPIO_OUT);
    gpio_put(m->in1_pin, 0);
    gpio_put(m->in2_pin, 0);

    gpio_set_function(m->pwm_pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(m->pwm_pin);
    pwm_set_clkdiv(slice, 1.0f);
    pwm_set_wrap(slice, pwm_wrap);
    pwm_set_gpio_level(m->pwm_pin, 0);
    pwm_set_enabled(slice, true);
}

void motors_init(void) {
    // Calcula o wrap a partir do clock real (150 MHz no RP2350)
    pwm_wrap = (uint16_t)(clock_get_hz(clk_sys) / PWM_FREQ_HZ - 1);

    gpio_init(MOTOR_STBY_PIN);
    gpio_set_dir(MOTOR_STBY_PIN, GPIO_OUT);
    gpio_put(MOTOR_STBY_PIN, 0);

    for (int i = 0; i < 2; i++) setup_motor(&motors[i]);
}

void motors_enable(bool enable) {
    gpio_put(MOTOR_STBY_PIN, enable);
}

void motor_set(int motor, float percent) {
    if (motor < 1 || motor > 2) return;
    const motor_t *m = &motors[motor - 1];

    if (percent > 100.0f) percent = 100.0f;
    if (percent < -100.0f) percent = -100.0f;
    if (m->invert) percent = -percent;

    bool forward = percent >= 0.0f;
    if (!forward) percent = -percent;

    gpio_put(m->in1_pin, forward);
    gpio_put(m->in2_pin, !forward);
    pwm_set_gpio_level(m->pwm_pin, (uint16_t)(percent / 100.0f * pwm_wrap));
}

void motors_set(float left, float right) {
    motor_set(MOTOR_LEFT, left);
    motor_set(MOTOR_RIGHT, right);
}

void motors_brake(void) {
    for (int i = 0; i < 2; i++) {
        gpio_put(motors[i].in1_pin, 1);
        gpio_put(motors[i].in2_pin, 1);
        pwm_set_gpio_level(motors[i].pwm_pin, pwm_wrap);
    }
}
