# CAM (Cam-M / Cam-P4) — página `/m` no telemóvel

Página **própria** do pack (não é o `/camera` do desktop). No telemóvel,
`/camera` responde 302 → `/m`; `?desk=1` continua a abrir a página completa.

## Organização

```
        Live            Files           Camera          FTP           System
       ─────           ───────         ──────          ───           ──────
  (aba activa em cima, com o risco por baixo; visitadas ficam azuis)
```

- **5 páginas**, uma por ecrã, **sem scroll de página**; conteúdo **centrado**.
- **Swipe** horizontal (esquerda/direita), setas, ou tocar no nome em cima.
- **Todos os botões exigem 250 ms** de toque (um dedo que roça não acciona) e
  escurecem enquanto estão premidos.
- Nome do pack no topo (`Cam-M` / `Cam-P4`) e badge `online/offline`.

## Ficheiros

| # | Imagem | O que mostra |
|---|---|---|
| 01 | `01_live.png` | **Live**: vista MJPEG em caixa 16:9, info (`fps · resolução · SD livre · viewers`), **PHOTO**, **REC** e **⛶ ecrã inteiro** |
| 02 | `02_live_fullscreen.png` | **Ecrã inteiro**: a vista ocupa o ecrã todo, **✕** para sair |
| 03 | `03_files.png` | **Files**: gravações do SD, **⬇** descarrega para o telemóvel, **›** reproduz, **✕** apaga (com confirmação) |
| 04 | `04_camera_modes.png` | **Camera**: modo (Off/Foto/Vídeo/Movimento/Lapse), resolução, FPS, qualidade, brilho/contraste/saturação, espelho/vertical/luz, movimento (detectar/sensibilidade/reiniciar), timelapse e **SAVE** |
| 05 | `05_ftp.png` | **FTP**: servidor, pasta remota, último envio, enviados/falhas, **ENVIAR AGORA**, **TESTAR** e lista dos últimos envios |
| 06 | `06_system_summary.png` | **Sistema**: firmware, uptime, heap, PSRAM, SD livre, WiFi + Reiniciar (OK/Cancel) |
| 07 | `07_system_firmware.png` | **Firmware (OTA)**: versão actual, escolher `.bin`, progresso |
| 08 | `08_system_log.png` | **Log** (monitor série) |

## O que é próprio e o que é comum

| Peça | Onde |
|---|---|
| Ecrãs **Live**, **Files**, **Camera** e **FTP** | `page_cam_mobile_asset.cpp` (só deste pack) |
| Ecrã **Sistema** (Resumo/OTA/Log/reiniciar) | `mobile_system_asset.cpp` — **comum a todos os packs** |
| Faixa de páginas, tiles, barra, mensagens, botões, log, protecção de toque | `mobile_common_asset.cpp` — **comum a todos os packs** |

## Endpoints usados

`/cam/stream` · `/cam/playback` · `/api/camera/status` · `/api/camera/photo` ·
`/api/camera/recording/start` · `/api/camera/recording/stop` ·
`/api/camera/files` · `/api/camera/delete`
(e o bloco Sistema: `/api/version`, `/api/health`, `/api/status`,
`/api/sd-status`, `/api/enable-ota`, `/api/firmware-upload`,
`/api/serial-monitor`, `/api/restart`).

## Notas

1. **Live** abre `/cam/stream`; tocar na imagem volta a ligar (se a ligação cair).
2. **REC** inicia/pára a gravação no cartão SD; o estado vem do `status`.
3. **Files** lista `.avi` e `.264`; **⬇** força o download para o telemóvel
   (`/cam/playback` com `download`), **›** abre a reprodução noutra aba — se o
   browser não tocar `.avi`/`.264`, usa o **⬇**.
4. **Apagar** pede confirmação (`customConfirm`).
5. **Reiniciar** é recusado enquanto estiver a gravar.
6. **Camera** grava com `/api/camera/config` (resolução da lista `resList` do
   sensor, FPS até `fpsMax`, qualidade 0–63, brilho/contraste/saturação −2…+2,
   espelho/vertical e luz quando existe). Em máquinas sem `resList` a lista de
   resolução cai para os `framesize` clássicos (QQVGA…UXGA).
7. O log usa `/api/serial-monitor`; se o monitor série estiver desligado nas
   Configurações → Acesso, fica vazio.
