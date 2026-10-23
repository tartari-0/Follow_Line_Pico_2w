# Diagnóstico: ruído no encoder (2026-10-23)

**Sintoma:** em ensaios de escada, a contagem do encoder alternava −1/+1 sem avançar em
trechos de até vários segundos (velocidade medida ~0 com a roda girando), sobretudo entre
35% e 55% de PWM, e com quedas pontuais em todas as faixas. A bateria não caía nesses
trechos (sem travamento real).

**Evidências:**
- Filtro digital no PIO (estado estável por 6,4 us) eliminou parte dos eventos, não todos.
- Ensaio `coast` (degrau a 100% e depois driver desligado): 78 ms anômalos em 2000 ms com o
  motor acionado e 0 em roda livre. O ruído vem do acionamento; a 100% o PWM quase não
  chaveia, então a fonte principal é o faiscamento das escovas.

**Correção:** capacitor cerâmico de 100 nF (104) soldado entre os terminais de cada motor.

| `coast 1 100 3000 5000` | antes (092728) | com 100 nF (101129) |
|---|---|---|
| ms anômalos (acionado) | 78 (3,9%) | 0 (0%) |
| desvio da velocidade em regime | 90,4 RPM (9,9%) | 3,6 RPM (0,4%) |
| velocidade média em regime | 918 RPM | 940 RPM |

O desvio com o capacitor é menor que a quantização da medida (7,4 RPM com janela de 10 ms).
A média subiu porque as perdas de contagem enviesavam a velocidade para baixo: os ensaios
feitos antes da correção têm erro sistemático e não devem ser usados para modelagem.

Gráficos: `comparacao_capacitor_100nF.png` e um PNG por ensaio.
