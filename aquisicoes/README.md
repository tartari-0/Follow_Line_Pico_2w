# Aquisições

Tudo que é usado para fazer e analisar os ensaios do robô, separado do firmware (que fica na
raiz do repositório).

```
aquisicoes/
  ferramentas/
    acquire.py         dispara um ensaio no robô e grava CSV + JSON
    plot_exp.py        plota os ensaios
    robot_link.py      terminal interativo (comandos + log livre de telemetria)
    robot_protocol.py  formato binário da telemetria (igual a src/telemetry.h)
  dados/
    AAAA-MM-DD_<campanha>/
      HHMMSS_<ensaio>.csv   dados
      HHMMSS_<ensaio>.json  metadados
      HHMMSS_<ensaio>.png   gráfico (plot_exp.py --salvar)
```

Uma **campanha** é uma série de ensaios feitos nas mesmas condições (ex.: `degraus_suspenso`,
`prbs_chao`). Use `--campanha` no `acquire.py`; o padrão é `geral`. Os scripts funcionam a
partir de qualquer pasta.

## Transmissão sem perdas

As amostras do ensaio são numeradas e o robô guarda os últimos 5 s na RAM
(`EXP_LOG_LEN`). O `acquire.py` pede de novo (`resend`) o que se perder no Wi-Fi, então
ensaios de qualquer duração chegam completos, mesmo com quedas de até ~5 s. O `.json`
registra quantos pacotes foram recuperados e se sobrou alguma amostra faltando.

A amostragem é feita com as interrupções desligadas no instante exato do ciclo, então
`t_us` avança exatamente 1000 us por amostra (`jitter_max_us` no `.json`).

## Rastreabilidade

Cada `.json` guarda:
- `firmware.git`: commit do firmware gravado no robô (lido pelo comando `info`);
- `git_ferramentas`: commit dos scripts do PC;
- o comando do ensaio, a data, a nota e os parâmetros do firmware (pulsos/volta, período do loop...).

O sufixo `-dirty` indica que havia alterações não commitadas; nesse caso não dá para recuperar
o código exato depois. **Antes de uma campanha, faça commit, compile e grave o firmware.** O
`acquire.py` avisa quando o firmware está `-dirty`.

Com o commit é possível voltar ao código exato de qualquer ensaio:
`git checkout <commit>`.

## Fluxo de trabalho

1. Commit das alterações → compilar → gravar o firmware.
2. Robô na bateria, PC na rede `FollowLine-Pico`.
3. Rodar os ensaios com `acquire.py --campanha <nome> --nota "<condições>"`.
4. `plot_exp.py <pasta>/*.csv --salvar` e conferir os gráficos.
5. Commit da pasta da campanha (`git add aquisicoes/dados/<pasta>`).

## Ensaios

O sinal é gerado **dentro do robô**, no loop de 1 kHz, então o Wi-Fi não interfere no tempo
do sinal aplicado. O PC dispara o ensaio e grava a telemetria. Todo ensaio tem 0,5 s parado
antes e 1,5 s parado depois do sinal (`EXP_PRE_MS` / `EXP_POST_MS`).

```sh
python3 aquisicoes/ferramentas/acquire.py "step 1 40 2000" --campanha degraus_suspenso --nota "bateria 12.4 V"
python3 aquisicoes/ferramentas/acquire.py "stairs ambos 100 10 2000" --campanha estatico --repeticoes 3
python3 aquisicoes/ferramentas/plot_exp.py aquisicoes/dados/2026-10-23_degraus_suspenso/*.csv --salvar
```

| Ensaio | Parâmetros | Uso |
|---|---|---|
| `step <alvo> <pwm> <ms>` | degrau | constante de tempo e ganho |
| `stairs <alvo> <pwm_max> <n> <ms_degrau>` | escada que sobe até `pwm_max` e desce | zona morta, curva estática, histerese |
| `ramp <alvo> <pwm_ini> <pwm_fim> <ms>` | rampa linear | curva estática |
| `prbs <alvo> <pwm_medio> <amp> <ms_bit> <ms>` | PRBS de 16 bits, semente fixa | identificação |

Alvo: `1` (esq), `2` (dir), `ambos` (mesmo sinal) ou `rot` (motor 2 com o sinal invertido).
PWM negativo = ré.

Cada ensaio gera um `.csv` e um `.json` (comando, nota, parâmetros e commit do firmware, commit
das ferramentas e contagem de amostras perdidas). Colunas do CSV:

| Coluna | Descrição |
|---|---|
| `t_s` | tempo desde o início do ensaio (s) |
| `pwm1`, `pwm2` | comando dos motores (%) |
| `enc1`, `enc2` | contagem absoluta dos encoders (pulsos 4X, 815 por volta da roda) |
| `speed1`, `speed2` | velocidade da roda calculada no robô (rad/s, janela de 10 ms) |
| `vbat` | bateria (V, filtro de ~5 ms) |
| `t_us` | relógio do robô (us) |

**Observações para a modelagem:**
- Durante o ensaio o driver fica sempre habilitado: PWM 0 é *freio* (terminais em curto), ou
  seja, 0 V aplicado. Fora dos ensaios, `stop` desabilita o driver e o motor fica em roda livre.
- A tensão aplicada ao motor pode ser estimada por `pwm/100 * vbat`.
- `speed` tem resolução de 0,77 rad/s e atraso médio de 5 ms. Para outros estimadores, use
  `enc` e `t_s`.

