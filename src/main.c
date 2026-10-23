#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "config.h"
#include "motors.h"
#include "encoders.h"
#include "line_sensor.h"
#include "analog.h"
#include "comm.h"
#include "experiment.h"
#include "git_version.h"
#include "exp_log.h"
#include "hardware/sync.h"

// --- Estado do loop de controle (núcleo 1) ---
static float pwm_cmd[2];          // Comando atual dos motores 1 e 2 (%)
static bool motors_on;
static bool calibrating;
static uint32_t stream_div;       // Envia 1 amostra a cada N ciclos (0 = desligado)
static uint32_t loop_count;
static uint32_t overruns;         // Ciclos que passaram do período
static uint32_t max_loop_us;
static absolute_time_t last_cmd_time;
static float vbat_filtered;
static bool exp_sample;           // A amostra deste ciclo pertence a um ensaio
static bool exp_just_finished;
static uint32_t stall_ms[2];      // Tempo (ms) de cada motor na condição de travamento
static uint32_t emergency_stops;

static void set_motor(int m, float percent) {
    if (percent > 100.0f) percent = 100.0f;
    if (percent < -100.0f) percent = -100.0f;
    pwm_cmd[m - 1] = percent;
    if (!motors_on && percent != 0.0f) {
        motors_on = true;
        motors_enable(true);
    }
    motor_set(m, percent);
}

// Habilita o driver mesmo com PWM 0. No TB6612, PWM 0 com o driver habilitado
// é freio (terminais em curto), ou seja, 0 V aplicado: comportamento linear,
// adequado para os ensaios. Desabilitado, o motor fica em roda livre.
static void enable_motors(void) {
    if (!motors_on) {
        motors_on = true;
        motors_enable(true);
    }
}

static void stop_motors(void) {
    pwm_cmd[0] = pwm_cmd[1] = 0.0f;
    motor_set(1, 0);
    motor_set(2, 0);
    motors_enable(false);
    motors_on = false;
}

static void print_line(void) {
    uint16_t raw[LINE_NUM_SENSORS], norm[LINE_NUM_SENSORS];
    int32_t pos;
    line_get_raw(raw);
    line_normalize(raw, norm);
    bool on_line = line_position(norm, &pos);

    comm_reply("raw: %u %u %u %u %u %u %u %u", raw[0], raw[1], raw[2], raw[3],
               raw[4], raw[5], raw[6], raw[7]);
    comm_reply("norm: %u %u %u %u %u %u %u %u | pos: %ld%s", norm[0], norm[1], norm[2],
               norm[3], norm[4], norm[5], norm[6], norm[7], (long)pos, on_line ? "" : " (sem linha)");
}

static void print_calibration(void) {
    uint16_t mn[LINE_NUM_SENSORS], mx[LINE_NUM_SENSORS];
    line_get_calibration(mn, mx);
    comm_reply("cal min (branco): %u %u %u %u %u %u %u %u",
               mn[0], mn[1], mn[2], mn[3], mn[4], mn[5], mn[6], mn[7]);
    comm_reply("cal max (preto):  %u %u %u %u %u %u %u %u",
               mx[0], mx[1], mx[2], mx[3], mx[4], mx[5], mx[6], mx[7]);
}

static void print_help(void) {
    comm_reply("ping | info | status | line | side | enc | bat");
    comm_reply("motor <1|2> <-100..100> | motors <esq> <dir> | stop");
    comm_reply("stream <N> (1 amostra a cada N ms, 0 = desliga)");
    comm_reply("cal start | cal stop | cal | timeout <us>");
    comm_reply("exp step <alvo> <pwm> <ms> | exp stairs <alvo> <pwm_max> <n> <ms_degrau>");
    comm_reply("exp ramp <alvo> <pwm_ini> <pwm_fim> <ms> | exp prbs <alvo> <pwm_medio> <amp> <ms_bit> <ms>");
    comm_reply("exp coast <alvo> <pwm> <ms_ligado> <ms_livre>");
    comm_reply("exp stop | alvo: 1, 2, ambos, rot");
}

static void handle_command(char *line) {
    char *argv[8];
    int argc = 0;
    for (char *tok = strtok(line, " \t\r\n"); tok && argc < 8; tok = strtok(NULL, " \t\r\n")) {
        argv[argc++] = tok;
    }
    if (argc == 0) return;

    last_cmd_time = get_absolute_time();
    const char *cmd = argv[0];

    if (!strcmp(cmd, "ping")) {
        // Só mantém o link vivo (sem resposta para não poluir)
    } else if (!strcmp(cmd, "help")) {
        print_help();
    } else if (!strcmp(cmd, "info")) {
        // Lido pelo aquisicoes/ferramentas/acquire.py e salvo junto com os dados
        comm_reply("info fw=%s git=%s ppr=%.1f loop_us=%d speed_window=%d pre_ms=%d post_ms=%d pwm_hz=%d",
                   PICO_PROGRAM_VERSION_STRING, GIT_VERSION, (double)ENC_PULSES_PER_REV, LOOP_PERIOD_US,
                   ENC_SPEED_WINDOW, EXP_PRE_MS, EXP_POST_MS, PWM_FREQ_HZ);
    } else if (!strcmp(cmd, "status")) {
        comm_reply("bat: %.2f V | enc: %ld %ld | vel: %.2f %.2f rad/s | pwm: %.1f %.1f | motores %s",
                   vbat_filtered, (long)encoder_count(1), (long)encoder_count(2),
                   encoder_speed(1), encoder_speed(2), pwm_cmd[0], pwm_cmd[1], motors_on ? "ON" : "OFF");
        comm_reply("loop: %lu ciclos, %lu atrasos, max %lu us | descartadas: %lu | timeout barra: %u us",
                   (unsigned long)loop_count, (unsigned long)overruns, (unsigned long)max_loop_us,
                   (unsigned long)comm_dropped_samples(), line_get_timeout_us());
        comm_reply("paradas de emergencia (travamento): %lu", (unsigned long)emergency_stops);
        max_loop_us = 0;
    } else if (!strcmp(cmd, "line")) {
        print_line();
    } else if (!strcmp(cmd, "side")) {
        comm_reply("side esq: %u | dir: %u", side_left(), side_right());
    } else if (!strcmp(cmd, "enc")) {
        comm_reply("enc: %ld %ld | vel: %.2f %.2f rad/s", (long)encoder_count(1), (long)encoder_count(2),
                   encoder_speed(1), encoder_speed(2));
    } else if (!strcmp(cmd, "bat")) {
        float v = vbat_filtered;
        comm_reply("bat: %.2f V%s", v, v < VBAT_MIN_V ? " (BAIXA!)" : "");
    } else if (!strcmp(cmd, "exp") && argc >= 2) {
        if (!strcmp(argv[1], "stop")) {
            if (exp_running()) {
                exp_stop();
                stop_motors();
                comm_reply("exp abortado");
            }
        } else if (!strcmp(argv[1], "estado")) {
            // Consultado pelo acquire.py caso a mensagem "exp fim" se perca
            comm_reply("exp estado: %s id=%lu n=%lu total=%lu", exp_running() ? "rodando" : "fim",
                       (unsigned long)exp_log_id(), (unsigned long)exp_log_count(),
                       (unsigned long)exp_log_total());
        } else {
            const char *err = exp_start(argc - 1, &argv[1]);
            if (err) {
                comm_reply("exp erro: %s", err);
            } else {
                exp_log_reset(exp_total_samples());
                enable_motors();
                comm_reply("exp inicio: %s id=%lu total=%u", exp_description(),
                           (unsigned long)exp_log_id(), exp_total_samples());
            }
        }
    } else if (exp_running() && (!strcmp(cmd, "motor") || !strcmp(cmd, "motors"))) {
        comm_reply("ensaio em andamento: use 'exp stop' antes");
    } else if (!strcmp(cmd, "motor") && argc == 3) {
        int m = atoi(argv[1]);
        if (m == 1 || m == 2) {
            set_motor(m, atof(argv[2]));
            comm_reply("motor %d = %.1f %%", m, pwm_cmd[m - 1]);
        }
    } else if (!strcmp(cmd, "motors") && argc == 3) {
        float l = atof(argv[1]), r = atof(argv[2]);
        set_motor(MOTOR_LEFT, l);
        set_motor(MOTOR_RIGHT, r);
        comm_reply("esq = %.1f %% | dir = %.1f %%", l, r);
    } else if (!strcmp(cmd, "stop")) {
        exp_stop();
        stop_motors();
        comm_reply("motores parados");
    } else if (!strcmp(cmd, "stream") && argc == 2) {
        stream_div = (uint32_t)atoi(argv[1]);
        comm_reply("stream: %s", stream_div ? argv[1] : "desligado");
    } else if (!strcmp(cmd, "cal")) {
        if (argc == 2 && !strcmp(argv[1], "start")) {
            line_calibrate_reset();
            calibrating = true;
            comm_reply("calibrando: passe a barra sobre a linha e o fundo, depois 'cal stop'");
        } else if (argc == 2 && !strcmp(argv[1], "stop")) {
            calibrating = false;
            print_calibration();
        } else {
            print_calibration();
        }
    } else if (!strcmp(cmd, "timeout") && argc == 2) {
        line_set_timeout_us((uint16_t)atoi(argv[1]));
        comm_reply("timeout barra: %u us", line_get_timeout_us());
    } else {
        comm_reply("comando desconhecido: %s (help)", cmd);
    }
}

// Parte crítica do ciclo, executada com as interrupções desligadas no instante
// exato da amostragem: lê os encoders e aplica o PWM do ensaio.
static void loop_sample(void) {
    encoders_update();

    exp_sample = exp_running();
    if (exp_sample) {
        float u1, u2;
        exp_update(&u1, &u2);
        if (exp_coasting()) {
            if (motors_on) stop_motors(); // Roda livre: driver desligado, sem freio
        } else {
            set_motor(1, u1);
            set_motor(2, u2);
        }
        if (!exp_running()) exp_just_finished = true;
    }
}

// Corta os motores se algum ficar travado: PWM alto com a roda parada.
// Protege motor e driver (travado a 12,6 V, a corrente passaria de 2,5 A).
static void check_stall(void) {
    if (!motors_on) {
        stall_ms[0] = stall_ms[1] = 0;
        return;
    }
    for (int m = 1; m <= 2; m++) {
        bool stalled = fabsf(pwm_cmd[m - 1]) >= STALL_PWM_MIN && fabsf(encoder_speed(m)) < STALL_SPEED_MIN;
        stall_ms[m - 1] = stalled ? stall_ms[m - 1] + 1 : 0;
        if (stall_ms[m - 1] >= STALL_TIME_MS) {
            float pwm = pwm_cmd[m - 1];
            if (exp_running()) {
                exp_stop();
                comm_reply("exp abortado: motor travado");
            }
            stop_motors();
            emergency_stops++;
            comm_reply("PARADA DE EMERGENCIA: motor %d travado (PWM %.0f %%, roda parada por %d ms)",
                       m, pwm, STALL_TIME_MS);
            stall_ms[0] = stall_ms[1] = 0;
            return;
        }
    }
}

// Restante do ciclo, com interrupções ligadas
static void loop_background(uint32_t t_us) {
    vbat_filtered += 0.2f * (battery_voltage() - vbat_filtered); // tau ~ 5 ms

    // 1. Registro da amostra do ensaio (transmitida e retransmitida pelo núcleo 0)
    if (exp_sample) {
        exp_sample_t e;
        e.t_us = t_us;
        e.enc[0] = encoder_count(1);
        e.enc[1] = encoder_count(2);
        e.speed[0] = encoder_speed(1);
        e.speed[1] = encoder_speed(2);
        e.pwm[0] = (int16_t)(pwm_cmd[0] * 10.0f);
        e.pwm[1] = (int16_t)(pwm_cmd[1] * 10.0f);
        e.vbat_mv = (uint16_t)(vbat_filtered * 1000.0f);
        e.flags = (motors_on ? TELEMETRY_FLAG_MOTORS : 0) | TELEMETRY_FLAG_EXPERIMENT;
        exp_log_add(&e);
    }
    if (exp_just_finished) {
        exp_just_finished = false;
        stop_motors();
        comm_reply("exp fim: %s id=%lu total=%lu", exp_description(),
                   (unsigned long)exp_log_id(), (unsigned long)exp_log_count());
    }

    // 2. Comandos
    char cmd[COMM_CMD_MAX_LEN];
    while (comm_get_command(cmd, sizeof(cmd))) handle_command(cmd);

    // 3. Segurança: sem comandos por muito tempo -> para os motores
    if (motors_on && absolute_time_diff_us(last_cmd_time, get_absolute_time()) > LINK_TIMEOUT_MS * 1000) {
        if (exp_running()) {
            exp_stop();
            comm_reply("exp abortado: link perdido");
        }
        stop_motors();
        comm_reply("link perdido: motores parados");
    }

    // 4. Parada de emergência: motor com PWM alto e roda parada (travado)
    check_stall();

    // 5. Barra de sensores (já lida em segundo plano pelo PIO/DMA)
    static uint32_t last_line_seq;
    uint16_t raw[LINE_NUM_SENSORS];
    uint32_t seq = line_get_raw(raw);
    if (calibrating && seq != last_line_seq) line_calibrate_update(raw);
    last_line_seq = seq;

    // 6. Telemetria livre (visualização; sem garantia de entrega)
    if (stream_div && loop_count % stream_div == 0) {
        telemetry_sample_t s;
        s.t_us = t_us;
        s.enc[0] = encoder_count(1);
        s.enc[1] = encoder_count(2);
        s.speed[0] = encoder_speed(1);
        s.speed[1] = encoder_speed(2);
        s.pwm[0] = (int16_t)(pwm_cmd[0] * 10.0f);
        s.pwm[1] = (int16_t)(pwm_cmd[1] * 10.0f);
        memcpy(s.line_raw, raw, sizeof(raw));
        s.side[0] = side_left();
        s.side[1] = side_right();
        s.vbat_mv = (uint16_t)(vbat_filtered * 1000.0f);
        s.flags = (motors_on ? TELEMETRY_FLAG_MOTORS : 0) | (exp_sample ? TELEMETRY_FLAG_EXPERIMENT : 0);
        comm_push_sample(&s);
    }

    // LED de status: pisca a 1 Hz
    gpio_put(STATUS_LED_PIN, (loop_count / 500) % 2);
}

// Alinhada em 8 bytes: exigência da ABI do ARM (senão printf com float quebra)
static uint32_t control_stack[8192 / 4] __attribute__((aligned(8)));

// Núcleo 1: loop de controle de período fixo. Não faz nenhuma E/S de comunicação
// diretamente (só filas), então o Wi-Fi e a USB não interferem no tempo do loop.
static void control_core_main(void) {
    // As interrupções de DMA da barra ficam neste núcleo
    line_init();
    analog_init();
    multicore_fifo_push_blocking(1); // Avisa o núcleo 0 que os PIOs estão alocados

    last_cmd_time = get_absolute_time();
    vbat_filtered = battery_voltage();
    absolute_time_t next = delayed_by_us(get_absolute_time(), LOOP_PERIOD_US);

    while (true) {
        // Espera com interrupções ligadas até pouco antes do instante de amostragem;
        // depois as desliga para que nada atrase a leitura dos encoders.
        busy_wait_until(from_us_since_boot(to_us_since_boot(next) - IRQ_GUARD_US));
        uint32_t irq_state = save_and_disable_interrupts();
        busy_wait_until(next);
        uint32_t t_us = time_us_32();
        loop_sample();
        restore_interrupts(irq_state);

        loop_background(t_us);
        loop_count++;

        uint32_t elapsed = time_us_32() - t_us;
        if (elapsed > max_loop_us) max_loop_us = elapsed;

        next = delayed_by_us(next, LOOP_PERIOD_US);
        if (absolute_time_diff_us(get_absolute_time(), next) < IRQ_GUARD_US) {
            overruns++;
            // Não tenta "recuperar" ciclos perdidos
            next = delayed_by_us(get_absolute_time(), LOOP_PERIOD_US);
        }
    }
}

// Núcleo 0: comunicação (Wi-Fi + USB)
int main(void) {
    stdio_init_all();

    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);

    // Os PIOs do robô precisam ser alocados antes do Wi-Fi
    motors_init();
    encoders_init();
    comm_init();

    multicore_launch_core1_with_stack(control_core_main, control_stack, sizeof(control_stack));
    multicore_fifo_pop_blocking();

    comm_run();
}
