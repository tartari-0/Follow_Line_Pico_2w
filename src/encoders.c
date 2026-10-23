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

    // Esvazia leituras velhas da fila e pega a mais recente
    while (!pio_sm_is_rx_fifo_empty(pio, sm)) {
        pio_sm_get(pio, sm);
    }
    int32_t count = (int32_t)pio_sm_get_blocking(pio, sm);
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
    uint32_t now = time_us_32();
    hist_idx = (hist_idx + 1) % HIST_LEN;
    time_hist[hist_idx] = now;
    for (int i = 0; i < 2; i++) count_hist[i][hist_idx] = encoder_get_count(i + 1);
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
