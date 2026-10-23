# Seguidor de Linha — Raspberry Pi Pico 2 W

Firmware em C (Pico SDK 2.3.0) para um robô seguidor de linha, de acordo com as
[regras RoboCore Line Follower](https://events.robocore.net) (linha branca sobre fundo preto).

## Hardware

| Componente | Pinos |
|---|---|
| Motor 1 — driver U9 (TB6612FNG, canais em paralelo) | PWM GP21, IN1 GP19, IN2 GP20 |
| Motor 2 — driver U4 (TB6612FNG, canais em paralelo) | PWM GP22, IN1 GP1, IN2 GP0 |
| STBY dos dois drivers | GP18 |
| Encoder motor 1 | GP16 / GP17 (PIO0 SM0) |
| Encoder motor 2 | GP2 / GP3 (PIO0 SM1) |
| Barra QRE-8D (leitura RC) | GP12, GP11, GP10, GP9, GP8, GP7, GP6, GP5 — LED_ON GP4 |
| Sensor lateral esquerdo (marcas de curva) | GP26 / ADC0 |
| Sensor lateral direito (partida/chegada) | GP27 / ADC1 |
| Tensão da bateria 3S (divisor 10k/3.3k) | GP28 / ADC2 |
| LED de status | GP13 |

Esquemático completo: [`docs/esquematico_rev1.0.pdf`](docs/esquematico_rev1.0.pdf).
Toda a pinagem fica centralizada em [`src/config.h`](src/config.h).

## Arquitetura

```
 Núcleo 1 — loop de controle a 1 kHz           Núcleo 0 — comunicação
 ┌──────────────────────────────────┐   filas  ┌──────────────────────────┐
 │ comandos -> motores (PWM)        │ <─────── │ Wi-Fi AP + DHCP + UDP    │ <──> PC
 │ lê encoders / barra / ADC        │ ───────> │ empacota telemetria      │
 │ gera amostras de telemetria      │          └──────────────────────────┘
 └──────────────────────────────────┘
   ▲ leituras prontas em segundo plano, sem CPU:
   │  PIO0 SM0/SM1 — encoders em quadratura 4X
   │  PIO + DMA    — barra QRE-8D (8 sensores RC amostrados a cada 1 us)
   │  ADC + DMA    — laterais e bateria (round-robin contínuo, 10 kSa/s por canal)
```

```
src/
  config.h               pinos e parâmetros do hardware
  motors.c/h             dois TB6612FNG (PWM 20 kHz)
  encoders.c/h           quadratura 4X via PIO e estimativa de velocidade
  quadrature_encoder.pio
  line_sensor.c/h        barra QRE-8D via PIO + DMA, calibração e posição
  line_rc.pio
  analog.c/h             ADC contínuo via DMA (laterais e bateria)
  comm.c/h               Wi-Fi, UDP e terminal USB no núcleo 0, filas entre os núcleos
  experiment.c/h         geração dos sinais de ensaio (degrau, escada, rampa, PRBS)
  telemetry.h            formato binário da telemetria
  lwipopts.h             configuração da pilha TCP/IP
  main.c                 loop de 1 kHz (núcleo 1) e interpretador de comandos
lib/dhcpserver/          servidor DHCP (MIT, do pico-examples)
cmake/git_version.cmake  embute o commit do git no firmware (comando info)
aquisicoes/
  ferramentas/           scripts do PC: terminal, aquisição, plot, protocolo
  dados/                 ensaios gravados (CSV + JSON + PNG), uma pasta por campanha
tests/                   programas de teste usados no desenvolvimento
docs/                    esquemático
```

## Comunicação sem fio

O robô cria a própria rede Wi-Fi (não precisa de roteador):

- **Rede:** `FollowLine-Pico`, senha `seguidor2026` (definida em `config.h`)
- **Robô:** `192.168.4.1`, porta UDP `4210`

```sh
python3 aquisicoes/ferramentas/robot_link.py
```

Comandos (também funcionam pelo terminal USB — o terminal precisa enviar Enter/fim de linha):

| Comando | Função |
|---|---|
| `status` | bateria, encoders, PWM, estatísticas do loop |
| `line` / `side` / `enc` / `bat` | leitura de cada sensor |
| `motor <1\|2> <-100..100>` | PWM de um motor em malha aberta (%) |
| `motors <esq> <dir>` | PWM dos dois motores |
| `stop` | para os motores |
| `stream <N>` | telemetria a cada N ms (`stream 1` = 1 kHz, `stream 0` desliga) |
| `cal start` / `cal stop` | calibração da barra (passe a barra sobre linha e fundo) |
| `timeout <us>` | tempo máximo de leitura RC da barra |
| `exp ...` / `exp stop` | ensaios em malha aberta (ver abaixo) |
| `info` | parâmetros do firmware |

Comandos locais do `robot_link.py`: `:log arquivo.csv`, `:stoplog`, `:quit`.

**Segurança:** se o robô ficar 1 s sem receber nada, ele para os motores. O `robot_link.py`
envia `ping` a cada 300 ms para manter o link.

## Compilando

Pela extensão **Raspberry Pi Pico** do VS Code: *Import Project* → selecionar esta pasta → *Compile*.

Ou pelo terminal (com o toolchain instalado pela extensão):

```sh
export PATH=$HOME/.pico-sdk/cmake/v4.3.4/bin:$HOME/.pico-sdk/ninja/v1.13.2:$HOME/.pico-sdk/toolchain/15_2_Rel1/bin:$PATH
cmake -S . -B build -G Ninja
ninja -C build
```

Grave `build/seguidor_de_linha.uf2` segurando BOOTSEL ao conectar o USB.

## Ensaios em malha aberta

Ferramentas, dados e instruções dos ensaios ficam em [`aquisicoes/`](aquisicoes/README.md),
separados do firmware.

## Roteiro de teste do hardware

1. **Só USB, sem bateria:** grave o firmware e abra o monitor serial. Devem aparecer a mensagem
   do Wi-Fi e o LED da placa aceso. Teste `line`, `side` e `enc` (girando a roda com a mão).
2. **Só bateria, sem USB:** conecte o PC na rede `FollowLine-Pico` e rode `robot_link.py`.
   Use `status` e `bat`, e compare com a tensão medida num multímetro.
3. **Motores (robô suspenso!):** `motor 1 20`, depois `stop`, depois `motor 2 20`, depois `stop`.
   Anote qual roda gira e para que lado, e se o encoder aumenta. Corrija `MOTOR_LEFT/RIGHT`
   e os `*_INVERT` em `config.h`.
4. **Barra:** `cal start`, passe a barra sobre a linha, `cal stop`. Depois `line` com a linha
   sob cada sensor: confira se o sensor 0 é o da esquerda.
5. **Telemetria:** `:log teste.csv`, `stream 1`, aguarde alguns segundos, `stream 0`, `:stoplog`.
   O script informa quantos pacotes foram perdidos; o `status` mostra os atrasos do loop.

## Roadmap

- [x] Drivers de hardware
- [x] Leitura dos sensores em segundo plano (PIO + DMA)
- [x] Comunicação Wi-Fi: comandos e telemetria
- [x] Validar todo o hardware (roteiro acima)
- [x] Ferramentas de aquisição em malha aberta
- [ ] Aquisições dos motores em malha aberta
- [ ] Modelagem dos motores
- [ ] Controle de velocidade dos motores
- [ ] Seguimento de linha (PID / MPC)
- [ ] Detecção das marcas laterais (curvas e partida/chegada) e parada automática
- [ ] Mapeamento da pista e perfil de velocidade
