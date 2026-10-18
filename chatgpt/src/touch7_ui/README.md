# `touch7_ui/` — pasta de troca com o EEZ Studio (TouchMeteo-7)

Esta pasta é o **ponto de troca** entre o editor gráfico LVGL (no teu PC x86_64)
e este repo. Põe aqui o projeto do editor **e** o C exportado.

## O que já está aqui

| Item | Uso no editor |
|---|---|
| `icons/*.png` | 26 ícones de meteorologia (MyAuraPT / google-weather-icons v2), 100x100 RGBA. **Importa-os no EEZ** (Assets → Images). São a fonte dos ícones do ecrã. |
| `palette.json` | Cores do design RSK World "Weather Dashboard" (gradiente `#667eea→#764ba2`, cartões claros/escuros, texto). Copia os hex para os estilos do editor. |
| `lvgl_editor_workflow.md` | (em `docs/`) contrato de integração com `touch7_display.cpp`. |

## Passos no PC (EEZ Studio, x86_64)

1. `File → New Project`, tipo **LVGL**, versão **9.5** (o device corre 9.6).
2. Display **800x480**, landscape, **16 bpp (RGB565)**.
3. Importa os ícones de `icons/` e cria os estilos com os hex de `palette.json`.
4. Guarda o projeto nesta pasta, ex.: `touch7_ui/touch7.eez-project`.
5. Exporta o C para `touch7_ui/` (ex.: `ui.c`/`ui.h` + `screens/`).

> Se o EEZ guardar o projeto num formato próprio, mantém-no nesta pasta — é o
> "ficheiro de configuração" que importas/abres.

## Regras de integração (resumo)

- Os ícones são **RGB565A8**; no device são os `.c` de `src/touch7_icons/`
  (mapeamento WMO em `touch7_icons.h`). Se desenhares ícones novos no editor,
  avisa que eu regenero os assets em C.
- Os `.c` exportados que incluam `lvgl.h` têm de ficar dentro do guard
  `#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1` (como os
  ícones) para não quebrar os outros builds.
- Não copiar o `lv_conf.h` do editor — usamos `src/lv_conf.h`.
- Eventos/dados ligam-se aos nossos callbacks (`evtTab`, `evtTheme`,
  `evtBright`, `setAgora`, `setForecast`, `setPeerStatus`, `/api/touch7-*`).

## Ecrãs atuais (para desenhar com equivalência)

`AGORA` (ícone meteo + temperatura grande + linha meta + linha sistema),
`PREVISAO` (hoje + 5 dias), `PEERS` (grelha 2x2 com snapshot + nome),
`CFG` (tema, relógio, brilho, dim/saver, wifi, reiniciar) + barra de 4 botões
no fundo (y≈418, 188x56 cada) + barra de estado no topo (32 px).
