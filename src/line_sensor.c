#include "line_sensor.h"
#include <string.h>
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "line_rc.pio.h"

#define MAX_SAMPLES 4096

static PIO pio;
static uint sm;
static int dma_ch;

// Cada byte é uma amostra dos 8 pinos (bit 0 = GP5 ... bit 7 = GP12)
static uint32_t sample_buf[MAX_SAMPLES / 4];
static volatile uint16_t num_samples; // múltiplo de 4
static uint16_t active_samples;        // da leitura em andamento

static uint16_t latest_raw[LINE_NUM_SENSORS];
static volatile uint32_t latest_seq;

static uint16_t cal_min[LINE_NUM_SENSORS];
static uint16_t cal_max[LINE_NUM_SENSORS];
static int32_t last_position;

// O sensor i está no GPIO (LINE_FIRST_SENSOR_PIN - i), ou seja, no bit (7 - i)
static inline int bit_to_sensor(int bit) {
    return LINE_NUM_SENSORS - 1 - bit;
}

static void __not_in_flash_func(start_reading)(void) {
    uint16_t n = active_samples = num_samples;
    dma_channel_set_trans_count(dma_ch, n / 4, false);
    dma_channel_set_write_addr(dma_ch, sample_buf, true);
    pio_sm_put(pio, sm, n - 1);
}

// Converte as amostras no tempo em que cada pino foi a zero
static void __not_in_flash_func(process_samples)(uint16_t n) {
    const uint8_t *s = (const uint8_t *)sample_buf;
    uint16_t raw[LINE_NUM_SENSORS];
    uint8_t pending = 0xFF;

    for (int i = 0; i < LINE_NUM_SENSORS; i++) raw[i] = n;

    for (uint16_t t = 0; t < n && pending; t++) {
        uint8_t fell = pending & ~s[t];
        if (fell) {
            for (int bit = 0; bit < 8; bit++) {
                if (fell & (1u << bit)) raw[bit_to_sensor(bit)] = t;
            }
            pending &= ~fell;
        }
    }

    memcpy(latest_raw, raw, sizeof(raw));
    latest_seq++;
}

// Na RAM: tempo de execução previsível (sem espera pela flash)
static void __not_in_flash_func(line_dma_irq_handler)(void) {
    if (!dma_channel_get_irq0_status(dma_ch)) return;
    dma_channel_acknowledge_irq0(dma_ch);

    process_samples(active_samples);
    start_reading();
}

void line_init(void) {
    gpio_init(LINE_LED_ON_PIN);
    gpio_set_dir(LINE_LED_ON_PIN, GPIO_OUT);
    gpio_put(LINE_LED_ON_PIN, 1); // Liga os emissores IR da barra

    uint offset;
    bool ok = pio_claim_free_sm_and_add_program(&line_rc_program, &pio, &sm, &offset);
    hard_assert(ok);
    line_rc_program_init(pio, sm, offset, LINE_LAST_SENSOR_PIN, LINE_NUM_SENSORS);

    dma_ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
    dma_channel_configure(dma_ch, &c, sample_buf, &pio->rxf[sm], 0, false);

    dma_channel_set_irq0_enabled(dma_ch, true);
    irq_add_shared_handler(DMA_IRQ_0, line_dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);

    line_calibrate_reset();
    line_set_timeout_us(LINE_RC_TIMEOUT_US);
    start_reading();
}

uint32_t line_get_raw(uint16_t raw[LINE_NUM_SENSORS]) {
    uint32_t irq_state = save_and_disable_interrupts();
    memcpy(raw, latest_raw, sizeof(latest_raw));
    uint32_t seq = latest_seq;
    restore_interrupts(irq_state);
    return seq;
}

void line_set_timeout_us(uint16_t timeout_us) {
    if (timeout_us < 4) timeout_us = 4;
    if (timeout_us > MAX_SAMPLES) timeout_us = MAX_SAMPLES;
    num_samples = timeout_us & ~3u; // Aplicado na próxima leitura
}

uint16_t line_get_timeout_us(void) {
    return num_samples;
}

void line_calibrate_reset(void) {
    for (int i = 0; i < LINE_NUM_SENSORS; i++) {
        cal_min[i] = UINT16_MAX;
        cal_max[i] = 0;
    }
}

void line_calibrate_update(const uint16_t raw[LINE_NUM_SENSORS]) {
    for (int i = 0; i < LINE_NUM_SENSORS; i++) {
        if (raw[i] < cal_min[i]) cal_min[i] = raw[i];
        if (raw[i] > cal_max[i]) cal_max[i] = raw[i];
    }
}

void line_get_calibration(uint16_t min[LINE_NUM_SENSORS], uint16_t max[LINE_NUM_SENSORS]) {
    memcpy(min, cal_min, sizeof(cal_min));
    memcpy(max, cal_max, sizeof(cal_max));
}

void line_normalize(const uint16_t raw[LINE_NUM_SENSORS], uint16_t out[LINE_NUM_SENSORS]) {
    // Linha branca reflete mais IR -> descarga rápida -> tempo MENOR
    for (int i = 0; i < LINE_NUM_SENSORS; i++) {
        int32_t range = (int32_t)cal_max[i] - cal_min[i];
        int32_t v = 0; // Sensor não calibrado
        if (range > 0) {
            v = ((int32_t)cal_max[i] - raw[i]) * 1000 / range;
            if (v < 0) v = 0;
            if (v > 1000) v = 1000;
        }
        out[i] = (uint16_t)v;
    }
}

bool line_position(const uint16_t norm[LINE_NUM_SENSORS], int32_t *position) {
    int32_t weighted = 0;
    int32_t sum = 0;
    bool on_line = false;

    for (int i = 0; i < LINE_NUM_SENSORS; i++) {
        if (norm[i] > 200) on_line = true;
        if (norm[i] > 50) { // Ignora ruído
            weighted += (int32_t)norm[i] * (i * 1000);
            sum += norm[i];
        }
    }

    if (on_line && sum > 0) {
        last_position = weighted / sum - (LINE_NUM_SENSORS - 1) * 500;
    }
    *position = last_position;
    return on_line;
}
