#include <stdio.h>
#include "pico/stdlib.h"
#include "config.h"
#include "motors.h"
#include "encoders.h"
#include "sensors.h"
#include "battery.h"

static bool key_pressed(void) {
    return getchar_timeout_us(0) != PICO_ERROR_TIMEOUT;
}

static void print_help(void) {
    printf("\n=== Seguidor de Linha - Pico 2W ===\n");
    printf(" s : sensores de linha (bruto + calibrado + posicao)\n");
    printf(" l : sensores laterais\n");
    printf(" e : encoders\n");
    printf(" m : teste dos motores\n");
    printf(" c : calibrar barra de sensores (10 s)\n");
    printf(" b : tensao da bateria\n");
    printf(" h : ajuda\n");
    printf("(qualquer tecla interrompe os modos continuos)\n\n");
}

static void test_line_sensors(void) {
    while (!key_pressed()) {
        uint16_t raw[LINE_NUM_SENSORS], cal[LINE_NUM_SENSORS];
        int32_t pos;

        line_read_raw(raw);
        line_read_calibrated(cal);
        bool on_line = line_read_position(&pos);

        printf("RAW:");
        for (int i = 0; i < LINE_NUM_SENSORS; i++) printf(" %4u", raw[i]);
        printf(" | CAL:");
        for (int i = 0; i < LINE_NUM_SENSORS; i++) printf(" %4u", cal[i]);
        printf(" | POS: %5ld %s\n", (long)pos, on_line ? "" : "(sem linha)");
        sleep_ms(100);
    }
}

static void test_side_sensors(void) {
    while (!key_pressed()) {
        printf("Esq: %4u | Dir: %4u\n", side_read_left(), side_read_right());
        sleep_ms(100);
    }
}

static void test_encoders(void) {
    int32_t last1 = encoder_get_count(1), last2 = encoder_get_count(2);
    while (!key_pressed()) {
        sleep_ms(100);
        int32_t c1 = encoder_get_count(1), c2 = encoder_get_count(2);
        float rpm1 = (c1 - last1) / ENC_PULSES_PER_REV * 600.0f;
        float rpm2 = (c2 - last2) / ENC_PULSES_PER_REV * 600.0f;
        printf("M1: %8ld (%7.1f RPM) | M2: %8ld (%7.1f RPM)\n",
               (long)c1, rpm1, (long)c2, rpm2);
        last1 = c1;
        last2 = c2;
    }
}

// Gira cada motor para frente e para trás. Use para conferir
// MOTOR_LEFT/RIGHT e os *_INVERT em config.h (robô suspenso!).
static void test_motors(void) {
    const float speed = 25.0f;
    motors_enable(true);
    for (int m = 1; m <= 2; m++) {
        int32_t start = encoder_get_count(m);
        printf("Motor %d (%s) para FRENTE...\n", m, m == MOTOR_LEFT ? "esquerdo" : "direito");
        motor_set(m, speed);
        sleep_ms(1000);
        motor_set(m, 0);
        printf("  encoder: %+ld pulsos (deve ser positivo)\n", (long)(encoder_get_count(m) - start));
        sleep_ms(300);

        start = encoder_get_count(m);
        printf("Motor %d para TRAS...\n", m);
        motor_set(m, -speed);
        sleep_ms(1000);
        motor_set(m, 0);
        printf("  encoder: %+ld pulsos (deve ser negativo)\n", (long)(encoder_get_count(m) - start));
        sleep_ms(300);
    }
    motors_enable(false);
    printf("Teste concluido.\n");
}

static void calibrate_line(void) {
    printf("Calibrando por 10 s: passe a barra sobre a linha e o fundo...\n");
    line_calibrate_reset();
    absolute_time_t end = make_timeout_time_ms(10000);
    while (!time_reached(end)) {
        line_calibrate_step();
        gpio_put(STATUS_LED_PIN, (to_ms_since_boot(get_absolute_time()) / 100) % 2);
    }
    gpio_put(STATUS_LED_PIN, 0);
    line_print_calibration();
}

int main(void) {
    stdio_init_all();

    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);

    motors_init();
    encoders_init();
    sensors_init();
    battery_init();

    print_help();

    while (true) {
        int c = getchar_timeout_us(500000);
        if (c == PICO_ERROR_TIMEOUT) {
            gpio_xor_mask(1u << STATUS_LED_PIN); // Pisca enquanto ocioso
            continue;
        }

        switch (c) {
            case 's': test_line_sensors(); break;
            case 'l': test_side_sensors(); break;
            case 'e': test_encoders(); break;
            case 'm': test_motors(); break;
            case 'c': calibrate_line(); break;
            case 'b': {
                float v = battery_read_voltage();
                printf("Bateria: %.2f V %s\n", v, v < VBAT_MIN_V ? "(BAIXA!)" : "");
                break;
            }
            case 'h': print_help(); break;
            case '\r':
            case '\n': break;
            default: printf("Comando desconhecido: '%c' (h = ajuda)\n", c); break;
        }
    }
}
