#ifndef EXPERIMENT_H
#define EXPERIMENT_H

#include <stdbool.h>

// Ensaios em malha aberta gerados dentro do loop de controle (1 amostra por ms).
// Todo ensaio tem EXP_PRE_MS com PWM 0, o sinal, e EXP_POST_MS com PWM 0.
//
// Comando: exp <tipo> <alvo> <parâmetros>
//   alvo: 1 | 2 | ambos (mesmo sinal nos dois) | rot (motor 2 recebe o sinal invertido)
//   step   <alvo> <pwm> <duração_ms>
//   stairs <alvo> <pwm_max> <n_degraus> <ms_por_degrau>   (sobe até pwm_max e desce)
//   ramp   <alvo> <pwm_inicial> <pwm_final> <duração_ms>
//   prbs   <alvo> <pwm_médio> <amplitude> <ms_por_bit> <duração_ms>
// PWM em % (-100..100).

// Inicia um ensaio a partir dos argumentos (argv[0] = tipo).
// Retorna NULL se iniciou, ou uma mensagem de erro.
const char *exp_start(int argc, char **argv);

void exp_stop(void);
bool exp_running(void);

// Avança 1 ciclo do loop e escreve o PWM (%) dos motores 1 e 2.
// Retorna false quando o ensaio terminou.
bool exp_update(float *pwm1, float *pwm2);

// Descrição do ensaio em andamento/último (para log)
const char *exp_description(void);

#endif
