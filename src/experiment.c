#include "experiment.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "config.h"

typedef enum { EXP_STEP, EXP_STAIRS, EXP_RAMP, EXP_PRBS } exp_type_t;
typedef enum { TARGET_M1, TARGET_M2, TARGET_BOTH, TARGET_ROT } exp_target_t;

static bool running;
static exp_type_t type;
static exp_target_t target;
static float p[4];          // Parâmetros numéricos do ensaio
static uint32_t signal_ms;  // Duração do sinal (sem pré/pós)
static uint32_t t_ms;       // Tempo desde o início do ensaio
static uint16_t lfsr;
static char description[64];

static float clamp_pwm(float v) {
    if (v > 100.0f) return 100.0f;
    if (v < -100.0f) return -100.0f;
    return v;
}

// PRBS de comprimento máximo com LFSR de 16 bits (x^16 + x^14 + x^13 + x^11 + 1).
// Semente fixa: o mesmo comando gera sempre a mesma sequência.
static int prbs_next_bit(void) {
    uint16_t bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1u;
    lfsr = (lfsr >> 1) | (bit << 15);
    return lfsr & 1u;
}

const char *exp_start(int argc, char **argv) {
    if (running) return "ja existe um ensaio em andamento";
    if (argc < 3) return "uso: exp <step|stairs|ramp|prbs> <1|2|ambos|rot> <parametros>";

    const char *t = argv[0];
    int nparams;
    if (!strcmp(t, "step")) { type = EXP_STEP; nparams = 2; }
    else if (!strcmp(t, "stairs")) { type = EXP_STAIRS; nparams = 3; }
    else if (!strcmp(t, "ramp")) { type = EXP_RAMP; nparams = 3; }
    else if (!strcmp(t, "prbs")) { type = EXP_PRBS; nparams = 4; }
    else return "tipo desconhecido (step, stairs, ramp, prbs)";

    const char *tg = argv[1];
    if (!strcmp(tg, "1")) target = TARGET_M1;
    else if (!strcmp(tg, "2")) target = TARGET_M2;
    else if (!strcmp(tg, "ambos")) target = TARGET_BOTH;
    else if (!strcmp(tg, "rot")) target = TARGET_ROT;
    else return "alvo desconhecido (1, 2, ambos, rot)";

    if (argc != 2 + nparams) return "numero de parametros errado (help exp)";
    for (int i = 0; i < nparams; i++) p[i] = (float)atof(argv[2 + i]);

    switch (type) {
        case EXP_STEP:
            signal_ms = (uint32_t)p[1];
            break;
        case EXP_STAIRS:
            if (p[1] < 1 || p[2] < 1) return "n_degraus e ms_por_degrau devem ser >= 1";
            signal_ms = (uint32_t)((2 * (int)p[1] - 1) * p[2]);
            break;
        case EXP_RAMP:
            signal_ms = (uint32_t)p[2];
            break;
        case EXP_PRBS:
            if (p[2] < 1) return "ms_por_bit deve ser >= 1";
            signal_ms = (uint32_t)p[3];
            break;
    }
    if (signal_ms == 0) return "duracao deve ser > 0";
    if (signal_ms + EXP_PRE_MS + EXP_POST_MS > EXP_MAX_MS) return "ensaio longo demais";

    snprintf(description, sizeof(description), "%s %s", t, tg);
    for (int i = 0; i < nparams; i++) {
        size_t n = strlen(description);
        snprintf(description + n, sizeof(description) - n, " %g", (double)p[i]);
    }

    lfsr = 0xACE1u;
    t_ms = 0;
    running = true;
    return NULL;
}

void exp_stop(void) {
    running = false;
}

bool exp_running(void) {
    return running;
}

const char *exp_description(void) {
    return description;
}

// Valor do sinal no instante t (ms) contado a partir do fim do período pré
static float signal_at(uint32_t t) {
    switch (type) {
        case EXP_STEP:
            return p[0];
        case EXP_STAIRS: {
            int n = (int)p[1];
            int i = (int)(t / (uint32_t)p[2]);         // Índice do degrau: 0 .. 2n-2
            int level = i < n ? i + 1 : 2 * n - 1 - i; // Sobe 1..n e desce n-1..1
            return p[0] * level / n;
        }
        case EXP_RAMP:
            return p[0] + (p[1] - p[0]) * t / signal_ms;
        case EXP_PRBS: {
            static int bit;
            if (t % (uint32_t)p[2] == 0) bit = prbs_next_bit();
            return bit ? p[0] + p[1] : p[0] - p[1];
        }
    }
    return 0.0f;
}

bool exp_update(float *pwm1, float *pwm2) {
    if (!running) return false;

    float u = 0.0f;
    if (t_ms >= EXP_PRE_MS && t_ms < EXP_PRE_MS + signal_ms) {
        u = clamp_pwm(signal_at(t_ms - EXP_PRE_MS));
    }

    *pwm1 = (target == TARGET_M2) ? 0.0f : u;
    switch (target) {
        case TARGET_M1:   *pwm2 = 0.0f; break;
        case TARGET_M2:   *pwm2 = u;    break;
        case TARGET_BOTH: *pwm2 = u;    break;
        case TARGET_ROT:  *pwm2 = 0.0f - u; break; // 0 - u evita "-0"
    }

    t_ms++;
    if (t_ms >= EXP_PRE_MS + signal_ms + EXP_POST_MS) running = false;
    return true;
}
