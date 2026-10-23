#include "exp_log.h"
#include "config.h"
#include "hardware/sync.h"

// Margem para não ler um slot que o outro núcleo pode estar sobrescrevendo
#define READ_MARGIN 100

static exp_sample_t ring[EXP_LOG_LEN];
static volatile uint32_t count;
static volatile uint32_t total;
static volatile uint32_t id;

void exp_log_reset(uint32_t total_samples) {
    count = 0;
    total = total_samples;
    __dmb();
    id++;
}

void exp_log_add(const exp_sample_t *sample) {
    ring[count % EXP_LOG_LEN] = *sample;
    __dmb(); // A amostra precisa estar na RAM antes de o contador avançar
    count++;
}

uint32_t exp_log_id(void) {
    return id;
}

uint32_t exp_log_count(void) {
    return count;
}

uint32_t exp_log_total(void) {
    return total;
}

bool exp_log_get(uint32_t index, exp_sample_t *out) {
    uint32_t n = count;
    if (index >= n) return false;
    if (n > EXP_LOG_LEN - READ_MARGIN && index < n - (EXP_LOG_LEN - READ_MARGIN)) return false;
    *out = ring[index % EXP_LOG_LEN];
    __dmb();
    // Confere se o slot não foi sobrescrito durante a cópia
    n = count;
    return !(n > EXP_LOG_LEN && index < n - EXP_LOG_LEN);
}
