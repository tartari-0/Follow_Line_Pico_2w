#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/pio.h" // <<< BIBLIOTECA DO PIO
#include "encoder1.pio.h"  // <<< CABEÇALHO GERADO AUTOMATICAMENTE

// --- MAPEAMENTO DE PINOS ---
#define ENC_BASE_PIN 16 // Define o pino A. O pino B será obrigatoriamente o próximo (17)
#define PWM_PIN  21
#define IN1_PIN  19
#define IN2_PIN  20
#define STBY_PIN 18

// Leitura 4X (Borda de subida e descida de 2 canais = 200 * 4)
#define PULSOS_POR_VOLTA 820.0 

// Frequência do PWM e resolução
#define PWM_WRAP 12500 // Gera 10 kHz com clock base de 125MHz

// Selecionamos qual bloco e qual núcleo do PIO usaremos
PIO pio = pio0;
uint sm = 0;

// --- FUNÇÃO DE LEITURA BLINDADA DO ENCODER ---
long ler_encoder_pio() {
    // O PIO está enviando a contagem a todo instante. 
    // Nós esvaziamos a fila de mensagens velhas rapidamente...
    while (!pio_sm_is_rx_fifo_empty(pio, sm)) {
        pio_sm_get(pio, sm);
    }
    // ... e pegamos a leitura fresquinha exata deste milissegundo!
    return (long)pio_sm_get_blocking(pio, sm);
}

// --- CONFIGURAÇÃO DO MOTOR E PWM ---
void setup_motor() {
    gpio_init(IN1_PIN);
    gpio_init(IN2_PIN);
    gpio_init(STBY_PIN);
    gpio_set_dir(IN1_PIN, GPIO_OUT);
    gpio_set_dir(IN2_PIN, GPIO_OUT);
    gpio_set_dir(STBY_PIN, GPIO_OUT);

    gpio_put(STBY_PIN, 1);
    gpio_put(IN1_PIN, 1);
    gpio_put(IN2_PIN, 0);

    gpio_set_function(PWM_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(PWM_PIN);
    
    pwm_set_clkdiv(slice_num, 1.0); 
    pwm_set_wrap(slice_num, PWM_WRAP); 
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(PWM_PIN), 0);
    pwm_set_enabled(slice_num, true);
}

// --- ATUALIZAÇÃO DO PWM ---
void set_pwm_percent(float percent) {
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;
    
    uint slice_num = pwm_gpio_to_slice_num(PWM_PIN);
    uint16_t level = (uint16_t)((percent / 100.0) * PWM_WRAP);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(PWM_PIN), level);
}

int main() {
    stdio_init_all();

    // 1. Inicializa o Motor
    setup_motor();

    // 2. Inicializa o PIO para o Encoder
    uint offset = pio_add_program(pio, &encoder1_program); // Carrega o Assembly
    encoder_program_init(pio, sm, offset, ENC_BASE_PIN);  // Dá a partida

    long last_count = 0;
    uint32_t last_time = to_ms_since_boot(get_absolute_time());
    
    char input_buffer[10];
    int buffer_index = 0;
    float current_pwm = 0.0;
    bool is_typing = false;

    printf("\n--- Teste de Motor Iniciado (Via PIO 4X) ---\n");
    printf("Digite um valor de 0 a 100 e pressione Enter para alterar o PWM.\n");

    while (true) {
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        uint32_t dt = current_time - last_time;

        // 1. CÁLCULO E IMPRESSÃO DO RPM A CADA 100ms
        if (dt >= 100) {
            
            // A leitura agora pega a contagem absoluta direto do hardware do PIO!
            // Não precisamos mais pausar as interrupções.
            long current_count = ler_encoder_pio(); 
            
            long delta_count = current_count - last_count;
            float rpm = ((float)delta_count / PULSOS_POR_VOLTA) * (60000.0 / (float)dt);

            if (!is_typing) {
                printf("[PWM: %3.1f%%] | RPM: %7.2f | Pulsos: %ld\n", current_pwm, rpm, current_count);
            }

            last_count = current_count;
            last_time = current_time;
        }

        // 2. LEITURA NÃO-BLOQUEANTE DO TERMINAL
        int c = getchar_timeout_us(0); 
        if (c != PICO_ERROR_TIMEOUT) {
            if ((c >= '0' && c <= '9') || c == '.') {
                if (!is_typing) {
                    is_typing = true;
                    printf("\n>>> Digite o novo PWM: "); 
                }
                
                if (buffer_index < 9) {
                    input_buffer[buffer_index++] = (char)c;
                    putchar(c);
                }
            } 
            else if (c == '\n' || c == '\r') {
                if (buffer_index > 0) {
                    input_buffer[buffer_index] = '\0'; 
                    current_pwm = atof(input_buffer);  
                    set_pwm_percent(current_pwm);      
                    
                    printf("\n -> PWM alterado para: %.1f%%\n\n", current_pwm);
                    buffer_index = 0; 
                }
                is_typing = false;
            }
            else if (c == ' ' && is_typing) {
                printf("\n -> Cancelado.\n\n");
                buffer_index = 0;
                is_typing = false;
            }
        }
    }
}