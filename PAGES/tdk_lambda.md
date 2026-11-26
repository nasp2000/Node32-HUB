# TDK-Lambda / Genesys — `/tdk-lambda`

> Console SCPI para fontes **TDK-Lambda Genesys (GEN/GENH)** e registo CSV da
> tensão/corrente. Módulo autónomo (não pertence a nenhum pack); a página só existe
> com `ENABLE_TDK_LAMBDA=1`. Referência técnica da fonte: `docs/Fonte/COMANDOS.md`.

> Screenshot: `desktop/tdk_lambda.png` — pendente (flag desligada em todos os packs; capturar
> com um build `ENABLE_TDK_LAMBDA=1`, 1440 × 900, dados realistas).

---

## 1. Objective

Enviar comandos SCPI para a fonte (`DVC?`, `MV?`, `MODE?`, `STT?`…), ver as respostas em
directo, e gravar uma linha temporal de tensão/corrente num **CSV diário** com o
standard de telemetria do repo (SD e, sem SD, LittleFS no mesmo caminho).

As respostas **não** são capturadas por uma UART própria: o byte entra no pipeline
RS232 normal (`bufferDados` → `/monitor`), e o módulo recebe uma **cópia**. Com o flag
ligado, o comando enviado também é ecoado no monitor (`TX> …`).

---

## 2. Access & routing

| Item | Value |
|---|---|
| Page URL | `GET /tdk-lambda` |
| Feature flag | `ENABLE_TDK_LAMBDA` (`chatgpt/user_config.h`, opt-in comentado; **desligado por omissão**, nenhum pack o liga) |
| Pack | **`pack_rs232`** (2026-11-26) — `ENABLE_TDK_LAMBDA 1`; sem SD nesse build, o CSV usa o espelho LittleFS. Off em todos os outros packs |
| Dependência | exige **`ENABLE_RS232=1`** no build (usa a fila `enqueueRS232Command` e a UART2); sem RS232 o POST `send` devolve `success:false` |
| WiFi page | `web_routes_tdk_lambda.cpp` → `registerPageGet("/tdk-lambda", …)`, chunked de 3 segmentos, `ETag: FIRMWARE_BUILD`, `Cache-Control: private, max-age=15` |
| Ethernet (W5500) | `ethernet_bootstrap_routes.h` → `streamSharedTdkLambdaPage(chunked)` → `streamTdkLambdaPageToPrint()` |
| APIs | `registerApiRoute()` (WiFi **e** Ethernet via `findApiRouteHandler`) |
| Auth | Basic auth na página e em todas as APIs |
| Never-kick | **A** (`streamNeverExpireFlag()` incondicional — página de processamento local) |
| Renderer order | shell → `streamNeverExpireFlag` → `PAGE_TDK_LAMBDA` (HTML+JS) → auto.js |

Endpoints (todos registados uma vez, servidos nas duas interfaces):

| Método | Path | Devolve |
|---|---|---|
| GET | `/api/tdk-lambda-available` | `{available,message}` (nunca 404 → a página detecta "desligado") |
| GET | `/api/tdk-lambda-status` | `status{…}` + `values{…}` |
| GET | `/api/tdk-lambda-commands` | catálogo (`cmd,group,usage,desc,reply,notes`) |
| GET | `/api/tdk-lambda-log` | últimas 32 linhas (`dir,ageMs,text`) |
| GET | `/api/tdk-lambda-config` | `{poll,poll_s,csv,format,mode,cmd_v,cmd_i,cmd_state,map,scale_v,scale_i,off_v,off_i,auto_map,listen,allow_write}` |
| GET | `/api/tdk-lambda-config` extras | `addr`, `inject`, `limits{enabled,v_min,v_max,i_max}` |
| GET | `/api/tdk-lambda-samples?n=` | `{samples:[[age_s,volts,amps],…]}` — ring in RAM, ≤120 pontos |
| GET | `/api/tdk-lambda-csv` | ficheiro do dia (`text/csv`, ≤220 KB); usada pelo botão Download |
| POST | `/api/tdk-lambda-send` | corpo `cmd=…` → `{success,message}` |
| POST | `/api/tdk-lambda-clear` | limpa o log |
| POST | `/api/tdk-lambda-config` | `poll=0|1&poll_s=5..3600&csv=0|1` |
| POST | `/api/tdk-lambda-csv-flush` | força a gravação das linhas em buffer (LittleFS) |

---

## 3. Layout

```
div.page.tdk-lambda > div.wrap          ← the asset opens its own page/wrap
├ <style> (.section-box/.section-title + .tdk-* helpers)
├ div.actions.nav-inicio > a.btn[href='/'] "Home"
├ h1 "⚡ TDK-Lambda / Genesys" + p.tdk-note
├ div.section-box 1) Status      #tdkStatus badges + #tdkIdn/#tdkRev/#tdkSn + [Identify]
├ div.section-box 2) Live values .tdk-grid com 8 .tdk-val (#vMeas #vSet #iMeas #iSet #tdkMode #tdkOut #tdkOvp #tdkLast)
├ div.section-box 3) Trend       #tdkWin + canvas#tdkChart + limites (#tdkLimOn #tdkLimVMin/Max/IMax #tdkAlert)
├ div.section-box 4) Console     #tdkListen #tdkAllowW #tdkGuardMsg + #tdkCmd + botões + pre#tdkLog
├ div.section-box 5) CSV log     #tdkCsv + #tdkPoll #tdkPollSec #tdkCsvOn #tdkFmt [Flush now] [Download CSV]
├ div.section-box 6) Supply profile presets + campos + #tdkInject #tdkAuto + [Save profile] [Save the detected]
└ div.section-box 7) Commands    #tdkSearch + .tdk-tbl (#tdkCat)
```

---

## 4. Inputs

| Input | ID | Notas |
|---|---|---|
| Comando | `tdkCmd` | livre; max 32 caracteres (o handler corta); desactivado em modo escuta |
| Modo escuta | `tdkListen` | **não transmite nada** (poll parado, `send` devolve 409) — para quando o PLC é mestre |
| Permitir escrita | `tdkAllowW` | sem isto, só passa a **allow-list de leitura** (`MV? MC? DVC? STT? STAT? FLT? MODE? OUT? IDN? …`); `PV/PC/OUT/OVP/RMT/SAV/G*` → 403 |
| Filtro do catálogo | `tdkSearch` | filtra por comando/grupo/descrição/notas |
| Polling | `tdkPoll` | checkbox; grava na hora (`POST /api/tdk-lambda-config`) |
| Intervalo | `tdkPollSec` | 5…3600 s (servidor volta a limitar) |
| Gravar CSV | `tdkCsvOn` | checkbox |
| Formato CSV | `tdkFmt` | `sensor` (chave, valor — igual aos sensores de temperatura) ou `table` (colunas) |
| Modo do perfil | `tdkMode2` | `dvc` (uma pergunta devolve N campos) ou `split` (V e I separadas) |
| Perguntas | `tdkCmdV` `tdkCmdI` `tdkCmdSt` | comandos a enviar no ciclo de poll |
| Mapa de campos | `tdkMap` | índices (0-based) de V medida, V set, I medida, I set, OVP, UVL na resposta |
| Escalas/offsets | `tdkSv` `tdkSi` `tdkOv` `tdkOi` | aplicados depois do mapa (mV/mA → V/A, calibração) |
| Auto-detecção | `tdkAuto` | liga/desliga; **só corrige em runtime, nunca grava** o perfil |
| ➕ Gravar o detetado | botão | copia o mapa/escalas em uso para o perfil gravado (passa a manual fixo) |

## 5. Buttons

| Botão | Acção |
|---|---|
| 🏠 Início | link `/` |
| ➤ Enviar | `POST /api/tdk-lambda-send` com `tdkCmd` |
| `DVC?` `STT?` `STAT?` `MODE?` | atalhos: preenchem e enviam |
| 🗑 Limpar registo | `POST /api/tdk-lambda-clear` |
| 💾 Gravar agora | `POST /api/tdk-lambda-csv-flush` |
| `usar` (na tabela) | copia o `usage` do catálogo para o input |
| ➕ Gravar o detetado | lê `mapUsed/scaleVUsed/scaleIUsed` do status e grava como perfil |
| Presets do perfil | `GENH600-1.3` (dvc + mapa 0..5), `MV?+MC?` (split), `DVC? em mV/mA` (escala 0,001) — preenchem e gravam |
| 💾 Gravar perfil | `POST /api/tdk-lambda-config` só com os campos do perfil |

---

## 6. Client logic

- `tdkStatus()` a cada **5 s**: `GET /api/tdk-lambda-status` → badges + 8 cartões de valores;
  `values.valid=false` mostra `--` (nunca 0, para não parecer leitura real).
- `tdkLog()` a cada **3 s**: `GET /api/tdk-lambda-log` → `pre#tdkLog` (auto-scroll).
- `tdkLoadCfg()` no arranque: preenche os três controlos de CSV/polling.
- `tdkLoadCatalog()` no arranque: catálogo uma vez; `tdkRenderCatalog()` filtra no cliente.
- Sem arrow functions; `d.success`; `fetch` com `cache:'no-store'`.

---

## 7. Server API — comportamento

- `status`: inclui `lastLine`, `lastCommand`, `csvPath`, `csvOnSd`, `csvRows`, `lastRxAgeMs`.
  `values`: `vMeas,vSet,iMeas,iSet,ovp,uvl,mode,outOn,valid`.
- `send`: **recusa em modo escuta** (409) e recusa comandos fora da allow-list
  quando `allow_write` está off (403). Só depois enfileira no **pipeline RS232** (`enqueueRS232Command`) — respeita baud, XOFF/XON,
  template e pacing do RS232. Não abre UART própria.
- `config`: `setTdkLambda()` persiste (config doc, `markDirtyAndScheduleSave`) e aplica em
  runtime (`tdkLambdaSetPoll/SetCsv`).
- `csv-flush`: força a escrita do buffer (útil antes de desligar); normalmente é a janela de 30 min ou a marca de água de 96 KB que grava.

## 8. Persistence

| Dado | Onde |
|---|---|
| `poll`, `poll_s`, `csv`, `csv_format` | doc de config, secção `tdklambda` (defaults em `ensureAllFieldsExist`, leitura com fallback seguro) |
| **perfil da fonte** | mesma secção: `mode`, `cmd_v`, `cmd_i`, `cmd_state`, `map`, `scale_v`, `scale_i`, `off_v`, `off_i` — gravado por unidade e aplicado no boot (`tdkLambdaApplyProfile`) |
| CSV | `/<sistema>/TDK_DD-MM-AAAA.csv` — **SD** quando `sdcardShouldUseSD()`, senão **LittleFS no mesmo caminho** (root dinâmico `/<sys>/` já coberto por `flash_fallback_cleanup.h`) |
| Log TX/RX | RAM (32 linhas); o histórico completo vive no pipeline RS232 (`/monitor`, `.chart`, CSV de texto) |

**Dois formatos** (`csv_format` na config, default `sensor`):

| Formato | Linha gravada |
|---|---|
| `sensor` (default) | `2026-11-26 10:00:00, vout, 599.99, vset, 600.00, iout, 0.500, iset, 1.300, power, 300.00, ovp, 660.0, uvl, 0.0, mode, CV, out, ON` — mesma forma chave/valor dos sensores de temperatura (`SensorConfigManager::formatCSV`), sem linha de cabeçalho |
| `table` | cabeçalho `timestamp,v_meas,v_set,i_meas,i_set,p_w,ovp,uvl,mode,out` + uma linha por amostra |

Ao mudar de formato com linhas em buffer (LittleFS), o buffer é gravado primeiro — nunca
se misturam os dois formatos no mesmo ficheiro.

## 8b. Gráfico (decisão de desenho)

Desenhado **no browser** (canvas 2D), com os dados de `/api/tdk-lambda-samples`:

| Aspecto | Escolha | Custo |
|---|---|---|
| Anel de amostras | `s_ring[120]` (12 B cada) em **BSS**, não heap | 1,44 KB fixos, só com o flag ligado |
| Resposta | 60 pontos ≈ **1,2 KB** de JSON; `HttpResponse.body` faz uma cópia → ~2,5 KB transitórios, libertados no fim do pedido | zero PSRAM, zero parse de ficheiro |
| Desenho | canvas do browser, sem Chart.js nem libs | 0 B no dispositivo |
| Actualização | 30 s; **só redesenha** quando a assinatura (nº + última amostra) muda | evita repaint e pedidos inúteis |
| Alternativa rejeitada | servir o `.chart` com Chart.js (infra de sensores) | mais heap (parse do ficheiro) e duplicava o CSV que já escrevemos |

**E a página `/sensor-charts`?** Depende do build:

- **`pack_rs232` (sem SD)**: não mostra o TDK. O pipeline do gráfico RS232
  (`appendRs232LivePoints` → `logDataToSdcard`) está **dentro de `#if ENABLE_SDCARD==1`**;
  sem cartão não há pontos, e o separador RS232 fica vazio. O gráfico deste pack é o
  **canvas da própria página** (§8b), que não depende de SD.
- **Builds com SD** (ex.: Waveshare) e `ENABLE_TDK_LAMBDA=1`: as respostas `DVC?` entram
  no separador **RS232** como `rs232_col1..6` (V med, V set, I med, I set, OVP, UVL), com
  nomes genéricos — não há API para renomear colunas RS232.
- A linha de `inject` leva o prefixo **`TDK `** exactamente para o parser numérico do
  gráfico a ignorar (sem o prefixo, a data ISO seria lida como `2026,-11,-26,10,0,0` e
  criava uma linha de lixo no gráfico).

## 9. Runtime engine

- `tdkLambdaBegin()` (setup): lê a config; o módulo não abre UART.
- `tdkLambdaOnRxByte()` em `loop_impl.h`: **cópia** de cada byte RS232; acumula até `CR` e
  despacha a linha pelo tipo de pergunta pendente (`DVC?` → 6 floats; `MODE?` → CV/CC/OFF;
  `OUT?` → ON/OFF). Em modo *listener* (PLC dono da linha) aceita linhas `DVC?` espontâneas.
- **Auto-detecção (primeira resposta)**: na 1.ª resposta com ≥4 números, `tdkSuggestMap()`
  testa as 8 disposições documentadas (V,V,I,I) × 4 escalas (1 / 0,1 / 0,01 / 0,001) contra
  os **ratings da unidade** (`rate_v` 600 / `rate_i` 1,3) e adopta a que encaixa; regista em
  `/monitor` (`[TDK] perfil ajustado automaticamente: …`). É **transitória**: nunca escreve
  na config, por isso o perfil gravado (modo manual) mantém-se e volta no próximo boot.
  Se a resposta for ambígua (0 V, pares empatados) **não mexe** — fica o perfil gravado.
  Botão "Gravar o detetado" promove a detecção a perfil manual.
- **OLED (`screen.cpp`)**: num build com `ENABLE_TDK_LAMBDA=1`, a **terceira linha** ganha o
  cartão do PSU como **primeiro da rotação** — e o primário leva **peso 2** (aparece 2 de cada
  N turnos), com ordem **determinística** de 6 s (era `random`). O 0.96" tem 3 linhas, por isso
  a rotação é obrigatória para não se perder informação. Formato consoante o painel:
  - **0.96" (SCREEN_TYPE 1)** — as linhas passaram a usar a **largura toda (21 caracteres)**;
    textos até 2× a largura são truncados com `~`, só os muito longos fazem marquee. O cartão do
    PSU usa a forma compacta: `600V 1.30A`, ou `listen` / `ALERT` / `C04` / `wait`;
  - **Cartão `! OFFLINE`** — quando não há Wi-Fi **nem** Ethernet (fora de modo AP) entra
    como **primeiro** cartão da rotação: sem rede o pack não faz o seu trabalho.
  - **2.42" (SCREEN_TYPE 2)** — forma completa: `TDK 600V 1.30A CV`, idem estados. Todas as
    linhas da vista 2.42 passaram a usar `ScreenManager::fitForPanel()` (largura do painel +
    `~` na truncagem, em vez do antigo `substring(0, 21)` silencioso) e levam o mesmo
    deslocamento anti-burn-in.
- `tdkLambdaUpdate()` (loop): ciclo `<cmd_v>` → `<cmd_i>` (só `split`) → `<cmd_state>` a cada intervalo configurado,
  e flush do CSV **no backend ativo** (SD ou LittleFS) na janela de `getSdcardFlushInterval()`
  (10/30/60 min) ou quando o buffer de 128 KB passa dos **96 KB**.
- `TX> <cmd>` é ecoado para o monitor em `loop_impl.h` quando o envio tem sucesso.

## 10. i18n

Strings da página em pt/en inline (padrão das páginas de módulo); sem chaves novas no
`LangStrings` nesta versão (a página é técnica e mostra o catálogo em inglês, como o
manual). A revisão de i18n fica registada como pendência.

## 11. Review findings

| # | Finding | Pri | Suggestion |
|---|---|---|---|
| 1 | Sem screenshot em `PAGES/desktop/` (flag desligada em todos os packs) | Low | capturar com build `ENABLE_TDK_LAMBDA=1` |
| 2 | Strings da página em **inglês** (regra do repo: HTML fallback EN); sem chaves `LangStrings` | Info | traduzir via i18n só se o pack exigir PT |
| 3 | `csvRows` em LittleFS conta linhas do último flush (não do ficheiro todo) | Info | ler o ficheiro ao abrir a página, se for preciso |
| 4 | Catálogo em inglês (nomes do manual) | Info | manter: são literais SCPI |
| 5 | Modo *listener* não distingue `DVC?` de outras linhas de 6 campos | Low | usar `STT?` como marcador quando o PLC o pedir |

## 12. Source map

| Concern | File |
|---|---|
| Módulo (estado, poll, CSV) | `chatgpt/src/tdk_lambda.h`, `chatgpt/src/tdk_lambda.cpp` |
| Parsers (host-tested) | `chatgpt/src/tdk_lambda_scpi.h`, `tests/native/test_tdk_lambda_parser.cpp` |
| Catálogo de comandos | `tdk_lambda.cpp` (`kCatalog`) + `docs/Fonte/COMANDOS.md` §2 |
| Página (HTML+JS) | `chatgpt/src/webpages/assets/page_tdk_lambda_asset.{h,cpp}` |
| Renderer | `chatgpt/src/webpages/renderers/tdk_lambda_page_renderer.{h,cpp}` |
| Rotas WiFi + APIs | `chatgpt/src/webpages/routes/web_routes_tdk_lambda.{h,cpp}` |
| Handlers (WiFi+Ethernet) | `chatgpt/src/webpages/handlers_unified/tdk_lambda_handler.{h,cpp}` |
| Ethernet page | `ethernet_bootstrap_routes.h` (`/tdk-lambda`) + `web_shared_pages.{h,cpp}` |
| Hooks | `loop_impl.h` (RX copy + TX echo), `setup_impl.h` (`tdkLambdaBegin`) |
| Config | `ConfigManager.h` (`get/setTdkLambda*`), `ConfigManager.cpp` (defaults) |
