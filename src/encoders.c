#include "encoders.h"
#include "config.h"
#include "hardware/pio.h"
#include "quadrature_encoder.pio.h"

static PIO pio = pio0;
static const uint sm_of[2] = {0, 1};
static const bool invert_of[2] = {M1_ENC_INVERT, M2_ENC_INVERT};

void encoders_init(void) {
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
