#include "encoders.h"
#include "config.h"
#include <math.h>
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "quadrature_encoder.pio.h"

static PIO pio = pio0;
static const uint sm_of[2] = {0, 1};
static const bool invert_of[2] = {M1_ENC_INVERT, M2_ENC_INVERT};

void encoders_init(void) {
    // Reserva as SMs para que o driver do Wi-Fi (que também usa PIO) não as pegue.
    // Deve ser chamado antes de iniciar o Wi-Fi: o programa precisa do offset 0.
    pio_sm_claim(pio, sm_of[0]);
    pio_sm_claim(pio, sm_of[1]);

    // Um único programa carregado, compartilhado pelas duas máquinas de estado
    uint offset = pio_add_program(pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio, sm_of[0], offset, M1_ENC_BASE_PIN);
    quadrature_encoder_program_init(pio, sm_of[1], offset, M2_ENC_BASE_PIN);
}

int32_t encoder_get_count(int motor) {
    if (motor < 1 || motor > 2) return 0;
    uint sm = sm_of[motor - 1];

    // O PIO coloca a contagem na fila continuamente. Lê as N entradas que já estão
    // lá mais uma, que é garantidamente recente. O número de leituras é limitado:
    // esvaziar "até ficar vazia" podia ficar centenas de us correndo atrás do PIO
    // (mesma solução do quadrature_encoder do pico-examples).
    uint n = pio_sm_get_rx_fifo_level(pio, sm) + 1;
    uint32_t value = 0;
    while (n--) value = pio_sm_get_blocking(pio, sm);
    int32_t count = (int32_t)value;
    return invert_of[motor - 1] ? -count : count;
}

// --- Estimativa de velocidade por diferença de contagens numa janela ---
// Resolução: 1 pulso na janela = 2*pi / (ENC_PULSES_PER_REV * janela).
// Ex.: 815 pulsos/volta e janela de 10 ms -> 0,77 rad/s. Atraso médio: meia janela.

#define HIST_LEN (ENC_SPEED_WINDOW + 1)

static int32_t count_hist[2][HIST_LEN];
static uint32_t time_hist[HIST_LEN];
static uint32_t hist_idx;
static uint32_t hist_filled;
static float speed[2];

void encoders_update(void) {
    hist_idx = (hist_idx + 1) % HIST_LEN;
    for (int i = 0; i < 2; i++) count_hist[i][hist_idx] = encoder_get_count(i + 1);
    uint32_t now = time_us_32();
    time_hist[hist_idx] = now;
    if (hist_filled < HIST_LEN) hist_filled++;

    if (hist_filled < HIST_LEN) return;
    uint32_t oldest = (hist_idx + 1) % HIST_LEN;
    float dt = (now - time_hist[oldest]) * 1e-6f;
    for (int i = 0; i < 2; i++) {
        int32_t dc = count_hist[i][hist_idx] - count_hist[i][oldest];
        speed[i] = dc * (2.0f * (float)M_PI / ENC_PULSES_PER_REV) / dt;
    }
}

int32_t encoder_count(int motor) {
    if (motor < 1 || motor > 2) return 0;
    return count_hist[motor - 1][hist_idx];
}

float encoder_speed(int motor) {
    if (motor < 1 || motor > 2) return 0.0f;
    return speed[motor - 1];
}
