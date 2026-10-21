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

## Estrutura

```
src/
  config.h               pinos e parâmetros do hardware
  motors.c/h             controle dos dois TB6612FNG (PWM 20 kHz)
  encoders.c/h           leitura de quadratura 4X via PIO
  quadrature_encoder.pio programa PIO do encoder
  sensors.c/h            barra QRE-8D (RC), calibração, posição da linha, laterais
  battery.c/h            leitura da tensão da bateria
  main.c                 menu de testes pela serial USB
tests/                   programas de teste usados no desenvolvimento
docs/                    esquemático
```

## Compilando

Pela extensão **Raspberry Pi Pico** do VS Code: *Import Project* → selecionar esta pasta → *Compile*.

Ou pelo terminal (com o toolchain instalado pela extensão):

```sh
export PATH=$HOME/.pico-sdk/cmake/v4.3.4/bin:$HOME/.pico-sdk/ninja/v1.13.2:$HOME/.pico-sdk/toolchain/15_2_Rel1/bin:$PATH
cmake -S . -B build -G Ninja
ninja -C build
```

Grave `build/seguidor_de_linha.uf2` segurando BOOTSEL ao conectar o USB.

## Uso (v0.1 — modo de testes)

Abra o monitor serial (USB) e use os comandos:

| Tecla | Função |
|---|---|
| `s` | sensores de linha: bruto, calibrado e posição |
| `l` | sensores laterais |
| `e` | encoders (contagem e RPM) |
| `m` | teste dos motores — **deixe o robô suspenso** |
| `c` | calibração da barra (10 s) |
| `b` | tensão da bateria |
| `h` | ajuda |

## Roadmap

- [x] Drivers de hardware e menu de testes
- [ ] Confirmar mapeamento esquerdo/direito e sentido dos motores
- [ ] Controle PID de seguimento de linha
- [ ] Detecção das marcas laterais (curvas e partida/chegada) e parada automática
- [ ] Controle de velocidade das rodas com encoder
- [ ] Ajuste de parâmetros via Wi-Fi/Bluetooth (CYW43) entre tentativas
- [ ] Mapeamento da pista e perfil de velocidade
