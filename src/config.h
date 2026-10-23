#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
//  MAPEAMENTO DE PINOS (Esquemático "Seguidor de Linha" REV 1.0)
// =====================================================================

// --- Driver U9 (TB6612FNG, canais A e B em paralelo) -> Motor 1 (U2) ---
#define M1_PWM_PIN   21
#define M1_IN1_PIN   19
#define M1_IN2_PIN   20

// --- Driver U4 (TB6612FNG, canais A e B em paralelo) -> Motor 2 (U3) ---
#define M2_PWM_PIN   22
#define M2_IN1_PIN   1
#define M2_IN2_PIN   0

// STBY dos dois drivers está ligado no mesmo pino
#define MOTOR_STBY_PIN 18

// --- Encoders (o pino B é obrigatoriamente BASE + 1, exigência do PIO) ---
#define M1_ENC_BASE_PIN 16 // GP16 / GP17
#define M2_ENC_BASE_PIN 2  // GP2  / GP3

// --- Barra de sensores QRE-8D (leitura RC via PIO) ---
// Os 8 pinos precisam ser consecutivos (GP5..GP12). Sensor 0 = GP12 ... sensor 7 = GP5.
// TODO: confirmar se o sensor 0 é o da esquerda do robô.
#define LINE_LED_ON_PIN 4
#define LINE_NUM_SENSORS 8
#define LINE_FIRST_SENSOR_PIN 12
#define LINE_LAST_SENSOR_PIN  5

// --- Sensores laterais (QTR analógicos) ---
#define SIDE_LEFT_PIN   26 // ADC0 -> marcações de curva (lado esquerdo)
#define SIDE_RIGHT_PIN  27 // ADC1 -> marcações de partida/chegada (lado direito)

// --- Bateria 3S (divisor R2 = 10k / R1 = 3.3k) ---
#define VBAT_PIN 28 // ADC2
#define VBAT_DIVIDER ((10.0f + 3.3f) / 3.3f)

// --- LED de status ---
#define STATUS_LED_PIN 13

// =====================================================================
//  PARÂMETROS DO HARDWARE
// =====================================================================

// Conferido no robô: motor 1 = roda esquerda, motor 2 = roda direita;
// PWM positivo = para frente, e o encoder conta positivo.
#define MOTOR_LEFT  1
#define MOTOR_RIGHT 2
#define M1_INVERT   false
#define M2_INVERT   true
#define M1_ENC_INVERT true
#define M2_ENC_INVERT false

#define PWM_FREQ_HZ 20000 // 20 kHz: fora da faixa audível, TB6612 aceita até 100 kHz

// Leitura 4X, por volta da roda (medido girando 10 voltas, igual nas duas rodas)
#define ENC_PULSES_PER_REV 815.0f

// Clock das SMs dos encoders. Define o filtro de ruído: uma transição só conta se
// ficar estável por 32 ciclos (5 MHz -> 6,4 us). As bordas reais ficam > 30 us
// uma da outra na velocidade máxima (~100 rad/s).
#define ENC_PIO_FREQ_HZ 5000000

// Janela da estimativa de velocidade, em ciclos do loop (ms)
#define ENC_SPEED_WINDOW 10

// Tempo máximo de descarga do RC (us). Superfície preta = timeout.
// Período de leitura da barra ≈ 15 us (carga) + timeout. Ajustável em tempo real.
#define LINE_RC_TIMEOUT_US 1000

// Taxa de amostragem de cada canal do ADC (laterais e bateria)
#define ADC_RATE_PER_CHANNEL_HZ 10000

// Tensão mínima da bateria 3S antes de alertar (3.3 V/célula)
#define VBAT_MIN_V 9.9f

// =====================================================================
//  CONTROLE E COMUNICAÇÃO
// =====================================================================

// Período do loop de controle (núcleo 1)
#define LOOP_PERIOD_US 1000

// Parada de emergência por travamento: se um motor receber |PWM| >= STALL_PWM_MIN
// e a roda ficar abaixo de STALL_SPEED_MIN por STALL_TIME_MS, os motores são cortados.
// Motor provavelmente de 6 V (R ~ 4-5 ohm): travado a 12,6 V puxaria ~2,8 A.
#define STALL_PWM_MIN   25.0f  // % (~3 V; travado abaixo disso a corrente é < 0,7 A)
#define STALL_SPEED_MIN 2.0f   // rad/s na roda (~19 RPM)
#define STALL_TIME_MS   300    // A partida normal sai dessa condição em dezenas de ms

// Para os motores se nenhum comando chegar nesse intervalo
#define LINK_TIMEOUT_MS 1000

// Ensaios em malha aberta: tempo parado antes e depois do sinal, e duração máxima
#define EXP_PRE_MS  500
#define EXP_POST_MS 1500
#define EXP_MAX_MS  600000

// Buffer circular das amostras de ensaio para retransmissão (5 s a 1 kHz, 28 bytes cada)
#define EXP_LOG_LEN 5000

// Interrupções desligadas nesta janela antes do instante de amostragem (us),
// para que nenhuma interrupção atrase a leitura dos encoders
#define IRQ_GUARD_US 300

// Rede Wi-Fi criada pelo robô (modo Access Point). IP do robô: 192.168.4.1
#define WIFI_SSID     "FollowLine-Pico"
#define WIFI_PASSWORD "seguidor2026"
#define UDP_PORT      4210

#endif
