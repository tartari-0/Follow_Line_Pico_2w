#include "analog.h"
#include "config.h"
#include "hardware/adc.h"
#include "hardware/dma.h"

#define NUM_CHANNELS 3

static volatile uint16_t adc_buf[NUM_CHANNELS];
static volatile uint16_t *adc_buf_addr = adc_buf;

void analog_init(void) {
    adc_init();
    adc_gpio_init(SIDE_LEFT_PIN);
    adc_gpio_init(SIDE_RIGHT_PIN);
    adc_gpio_init(VBAT_PIN);

    adc_select_input(0);
    adc_set_round_robin((1u << NUM_CHANNELS) - 1);
    adc_fifo_setup(true, true, 1, false, false);
    // 48 MHz / (1 + div) = taxa total; dividida entre os 3 canais
    adc_set_clkdiv(48000000.0f / (ADC_RATE_PER_CHANNEL_HZ * NUM_CHANNELS) - 1.0f);

    // Canal de dados: FIFO do ADC -> adc_buf[0..2]
    int data_ch = dma_claim_unused_channel(true);
    // Canal de controle: reinicia o canal de dados no início do buffer
    int ctrl_ch = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(data_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_ADC);
    channel_config_set_chain_to(&c, ctrl_ch);
    dma_channel_configure(data_ch, &c, adc_buf, &adc_hw->fifo, NUM_CHANNELS, false);

    c = dma_channel_get_default_config(ctrl_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure(ctrl_ch, &c, &dma_hw->ch[data_ch].al2_write_addr_trig,
                          &adc_buf_addr, 1, false);

    adc_fifo_drain();
    dma_channel_start(data_ch);
    adc_run(true);
}

uint16_t analog_get(uint channel) {
    return channel < NUM_CHANNELS ? adc_buf[channel] : 0;
}

uint16_t side_left(void) {
    return analog_get(SIDE_LEFT_PIN - 26);
}

uint16_t side_right(void) {
    return analog_get(SIDE_RIGHT_PIN - 26);
}

float battery_voltage(void) {
    return analog_get(VBAT_PIN - 26) * (3.3f / 4096.0f) * VBAT_DIVIDER;
}
