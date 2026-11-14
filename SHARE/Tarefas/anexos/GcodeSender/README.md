# GcodeSender — página `/m` (telemóvel)

Revisão visual das páginas móveis próprias. **Nada disto é a página de desktop** —
o `/grbl-controller` (PC) fica exactamente como está; no telemóvel o
`/grbl-controller` continua a responder 302 → `/m`, e `?desk=1` continua a forçar
a página completa.

## Como está organizado

```
       Send     Preview     Play      Jog     System
       ───      ───────     ────      ───     ──────
      (aba activa em cima, com a linha por baixo; as visitadas ficam azuis)
```

- **5 páginas**, uma por ecrã, **sem scroll de página** (só listas/log têm scroll interno).
- No topo vê-se o **nome do pack** (`GcodeSender-P4` / `-S3`) à esquerda do estado de ligação.
- **Swipe** horizontal em qualquer sítio muda de página (a distância horizontal tem de ganhar;
  um swipe nunca prime um botão).
- **Todos os botões exigem ~180 ms de toque** — um dedo que só roça não acciona nada.
- **OTA e Reiniciar** pedem confirmação **OK/Cancel**; HOME / ZERO X-Y-Z / RESET e o **0,0** do jog
  pedem segundo toque (4 s).
- Muda-se de página com as **setas**, a **deslizar o dedo** (esquerda/direita) ou
  tocando no **nome** em cima.
- Os nomes em cima estão **alinhados com o risco** respectivo; o risco da página
  atual fica azul.

## Ficheiros

| # | Imagem | O que mostra |
|---|---|---|
| 01 | `01_send.png` | Lista do **cartão SD** (pastas, tamanho, `↻`), aba *SD card* / *Phone* (enviar do telemóvel para SD ou PSRAM) |
| 02 | `02_preview.png` | **Preview + FRAME**: percurso 2D no canvas, nº de pontos, área `X[..] Y[..] W x H` e o botão **FRAME** no fundo |
| 03 | `03_play_running.png` | **A executar**: barra, `400/1200 · 33%`, ETA, overrides F/S, PAUSE/RESUME/STOP |
| 04 | `04_play_paused.png` | **Em pausa** (RESUME activo) |
| 05 | `05_play_alarm.png` | **Alarme**: mensagem do GRBL + UNLOCK + RESUME JOB, START bloqueado |
| 06 | `06_jog.png` | **Jog em cruz** (Y+ cima, X−, X+, Y−), centro **0,0**, passos, Z, HOME/UNLOCK/RESET, ZERO X/Y/Z, HOLD/RESUME/CANCEL, MDI |
| 07 | `07_system_summary.png` | **Sistema**: firmware, uptime, heap, PSRAM, SD livre, WiFi + Reiniciar (OK/Cancel) |
| 08 | `08_system_firmware.png` | **Firmware (OTA)**: versão actual, escolher `.bin`, progresso, upload |
| 09 | `09_system_log.png` | **Log** (monitor série) com Pause/Clear/Bottom |

## O que é “próprio” e o que é partilhado

- **Próprio deste pack (CNC):** Send, Preview, Frame, Play, Jog (todo o GRBL).
- **Partilhado, reutilizável por outros packs:** a página **System**
  (`mobile_system_asset.cpp`: Resumo + OTA + Log + reiniciar) e os componentes
  comuns (`mobile_common_asset.cpp`: faixa, tiles, barra, mensagens, botões, log).
  Qualquer pack inclui os dois blocos e só escreve os seus ecrãs.

## Notas / por confirmar

1. **Sem links para as páginas de desktop** — OTA, ficheiros, estado, SD, monitor e
   acesso passaram a ser ecrãs próprios dentro do `/m` (ou parte deles).
2. **OTA** usa `/api/enable-ota` + `/api/firmware-upload` (octet-stream) — falta
   testar num telemóvel real com o `.bin` verdadeiro.
3. **Log** usa `/api/serial-monitor` (`limit`, `no_cmds`, `last_seq`); se o buffer
   do monitor estiver desligado nas Configurações → Acesso, o log fica vazio.
4. **Jog** em máquina de 2 eixos: a coluna/botões Z ficam desactivados.
5. **Bateria/uso**: as páginas 4 e 5 fazem poll a `/api/grbl/status` a cada 1 s; as
   outras não fazem poll (Sistema faz 2 s enquanto está à vista).
