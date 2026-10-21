#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/timer.h"

const uint LED_PIN = 13;
const uint BAR_LED_ON = 4;
const uint DIGITAIS[] = {12, 11, 10, 9, 8, 7, 6, 5};
const uint ADC_ESQ = 26;
const uint ADC_DIR = 27;
const int LIMIAR = 150;

// Função para ler o barramento RC
void ler_sensores_rc(uint32_t tempos_descarga[]) {
    // 1. Carrega os capacitores
    for (int i = 0; i < 8; i++) {
        gpio_set_dir(DIGITAIS[i], GPIO_OUT);
        gpio_put(DIGITAIS[i], 1);
    }
    
    sleep_us(15); 

    // 2. Altera para entrada e prepara para cronometrar
    for (int i = 0; i < 8; i++) {
        gpio_set_dir(DIGITAIS[i], GPIO_IN);
    }

    uint32_t tempo_inicial = time_us_32();
    uint32_t tempo_atual = 0;
    int sensores_lidos = 0;

    for(int i = 0; i < 8; i++) tempos_descarga[i] = 0; // Reseta array

    // 3. Mede o tempo (Timeout de 3000us para superfícies que não refletem IR)
    while (sensores_lidos < 8 && tempo_atual < 3000) {
        tempo_atual = time_us_32() - tempo_inicial;

        for (int i = 0; i < 8; i++) {
            if (gpio_get(DIGITAIS[i]) == 0 && tempos_descarga[i] == 0) {
                tempos_descarga[i] = tempo_atual;
                sensores_lidos++;
            }
        }
    }

    // 4. Preenche quem não leu (timeout) com o valor máximo
    for (int i = 0; i < 8; i++) {
        if (tempos_descarga[i] == 0) {
            tempos_descarga[i] = 3000;
        }
    }
}

int main() {
    // Inicializa entrada/saída padrão para o terminal serial
    stdio_init_all();

    // Inicializa módulo ADC e prepara os pinos analógicos
    adc_init();
    adc_gpio_init(ADC_ESQ);
    adc_gpio_init(ADC_DIR);

    // Inicializa e configura LEDs
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    
    gpio_init(BAR_LED_ON);
    gpio_set_dir(BAR_LED_ON, GPIO_OUT);
    gpio_put(BAR_LED_ON, 1); // Liga os emissores IR do barramento

    // Inicializa pinos dos sensores digitais
    for (int i = 0; i < 8; i++) {
        gpio_init(DIGITAIS[i]);
        gpio_set_dir(DIGITAIS[i], GPIO_IN);
    }

    while (1) {
        gpio_put(LED_PIN, 1); // Acende o LED

        adc_select_input(0); // ADC0 mapeado para GP26
        uint16_t val_esq = adc_read();

        adc_select_input(1); // ADC1 mapeado para GP27
        uint16_t val_dir = adc_read();

        uint32_t tempos_rc[8];
        ler_sensores_rc(tempos_rc);


        printf("ADC Esq (GP26): %d | ADC Dir (GP27): %d | Digitais: ", val_esq, val_dir);
        for(int i = 0; i < 8; i++) {
            if (tempos_rc[i] >= LIMIAR)
            printf("%d ", 0);
            else
            printf("%d ", 1);
        }

        printf("\n");

        printf("Tempos RC (us): ");
        for(int i = 0; i < 8; i++) {
            printf("%u ", tempos_rc[i]);
        }
        printf("\n");

        sleep_ms(250);
        gpio_put(LED_PIN, 0); // Apaga o LED
        sleep_ms(250);
    }
}
