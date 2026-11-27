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
| GET | `/api/tdk-lambda-config` | `{poll,poll_s,csv_every,csv,format,mode,cmd_v,cmd_i,cmd_state,map,scale_v,scale_i,off_v,off_i,auto_map,listen,allow_write}` |
| GET | `/api/tdk-lambda-config` extras | `addr`, `inject`, `limits{enabled,v_min,v_max,i_max}` |
| GET | `/api/tdk-lambda-samples?n=` | `{samples:[[age_s,volts,amps],…]}` — ring em RAM, ≤**240** pontos (`n` limitado a 240) |
| GET | `/api/tdk-lambda-csv` | ficheiro do dia (`text/csv`, até **1 MB**); descarrega o buffer primeiro, lê para **PSRAM** (nunca um `String` grande no heap) e, se cortar, corta em fim de linha e avisa no monitor |
| POST | `/api/tdk-lambda-send` | corpo `cmd=…` → `{success,message}` |
| POST | `/api/tdk-lambda-clear` | limpa o log |
| POST | `/api/tdk-lambda-config` | `poll=0|1&poll_s=60..3600&csv_every=1..60&csv=0|1&format=sensor|table` |
| POST | `/api/tdk-lambda-csv-flush` | força a gravação das linhas em buffer (LittleFS) |

---

## 3. Layout

```
div.page.tdk-lambda > div.wrap          ← the asset opens its own page/wrap
├ <style> (.section-box/.section-title + .tdk-tab-* + .tdk-* helpers)
├ div.actions.nav-inicio > a.btn[href='/'] "Home"
├ h1 "⚡ TDK-Lambda / Genesys" + p.tdk-note (ligações) + p.tdk-note "Hover any control…"
├ div#tdkTabBar .tdk-tab-bar            ← botões criados por JS a partir de TDK_TABS
│   Live | Console | CSV | Setup | Commands   (último separador activo fica em localStorage)
├ div.tdk-panel[data-tab=live] data-tab-default
│   ├ div.section-box 1) Status   #tdkStatus badges + #tdkIdn/#tdkRev/#tdkSn + [Identify]
│   ├ div.section-box 2) Live     .tdk-grid com 8 .tdk-val (#vMeas #vSet #iMeas #iSet #tdkMode #tdkOut #tdkOvp #tdkLast)
│   └ div.section-box 3) Trend    #tdkWin + canvas#tdkChart + limites (#tdkLimOn #tdkLimVMin/Max/IMax #tdkAlert)
├ div.tdk-panel[data-tab=console]
│   └ div.section-box 4) Console  #tdkListen #tdkAllowW #tdkGuardMsg + #tdkCmd + botões + pre#tdkLog
├ div.tdk-panel[data-tab=csv]
│   └ div.section-box 5) CSV log
│       ├ .tdk-grid 4 tiles   #csvBackend (SD/LittleFS) · #csvPath · #csvRows · #csvFormat
│       ├ #tdkCsvAuto         (auto/manual + map em uso)
│       ├ linha de controlo   #tdkPoll #tdkPollSec #tdkCsvEvery #tdkCsvOn #tdkFmt [Flush now] [Download CSV]
│       ├ h4.tdk-subtitle "Since boot" + 5 quadros: #sessWh #sessCc #sessLim #sessOff #sessTrans
│       └ h4.tdk-subtitle "Last polls" + tabela #tdkPrevBody — as últimas **8** leituras
│         (hora, V, A) do mesmo buffer do gráfico; nenhum endpoint novo
├ div.tdk-panel[data-tab=profile]
│   └ div.section-box 6) Supply profile  presets + campos + #tdkInject #tdkAuto + [Save profile] [Save the detected]
└ div.tdk-panel[data-tab=commands]
    └ div.section-box 7) Commands
        ├ #tdkSearch (filtro) + linha de envio: #tdkCatSel (menu pendente com o catálogo),
        │  [Send] #tdkCatSendBtn, [Load] e #tdkCatSendMsg
        └ .tdk-tbl 6 colunas (#tdkCat; **a linha inteira** carrega o comando → Console)

**Separadores** (mesma organização de `/access`): o conteúdo é dividido em cinco painéis
`.tdk-panel[data-tab]`; `tdkInitTabs()` cria os botões, activa o painel por omissão
(`data-tab-default`) ou o último usado (`localStorage.tdk_tab`) e `tdkShowTab(key)` permite saltar
de fora (usado pela **linha** do catálogo, que carrega o comando e passa ao Console).

**Envio pelo catálogo**: o menu pendente `#tdkCatSel` lista todos os comandos; **Send** envia
directamente os que terminam em `?` (consultas) e, para um setpoint (`PV`, `PC`, `OUT`…), carrega a
forma com valor para a Consola em vez de disparar algo sem valor — a mensagem diz o que falta.
**Load** (ou clicar na linha) só preenche a consola.

**Catálogo**: seis colunas (Command, Group, Usage, Description, Reply, Notes), `table-layout:fixed`
e `overflow-x:hidden` — **nunca há scroll horizontal**; em ecrãs <720 px escondem-se Group e Notes.
Não há botão por linha (o "use" foi removido): a **linha é a acção** (`cursor:pointer`, realce no
hover) e leva o comando para a consola.

**Separador CSV**: quatro quadros com o estado real do ficheiro (backend, caminho, linhas de hoje,
formato), a nota de perfil automático/manual e a tabela das últimas 8 leituras — tudo do
`/api/tdk-lambda-samples` que o gráfico já usa, por isso o custo em firmware é **zero** e o
separador deixou de ser uma caixa vazia.

**Sobreposições ao `core.css`** (necessárias nesta página): o core dá a **todos** os botões
`width:100%;max-width:420px;margin:6px auto`, o que transformava a barra de separadores num
2+2+1 e cada botão de acção numa pílula de largura total. A página corrige em `.tdk-tab-btn`
(`flex:1 1 0;width:auto`), `.tdk-row > .btn`/`button` (`width:auto;flex:0 0 auto`) e
`.tdk-tbl .btn` (`width:auto`, botão de célula). A tabela do catálogo usa `table-layout:fixed`
com larguras por coluna (a Description estava esmagada).

**Gráfico**: cada série escala ao **seu** min/max (±10 %) e marca os dois extremos — com eixo
desde 0 uma fonte de 600 V com 2 V de ripple era uma linha recta no topo. É **automático**: a
página pede `/api/tdk-lambda-samples` ao carregar e volta a pedir ao ritmo do poll (5…60 s,
`_tdkArmTrend`), sem clique nenhum; o buffer circular tem **240 amostras** (4 h a 1/min, 40 min a
10 s) e a janela é 60/120/240. Três faixas num só canvas — **V, I e P** (P = V×I calculado no
browser, para não crescer o payload), cada uma com a sua escala (min/max ±10 %).
**Custo**: o buffer são 2,9 KB de `.bss` (sem heap), o JSON é ≥ ~10 B/amostra e **nada é escrito
em ficheiro** pelo gráfico; uma aba do browser em segundo plano não faz fetch nenhum
(`document.hidden`, retoma no `visibilitychange`). O LittleFS continua a ser escrito **só** na
janela configurada do CSV (10/30/60 min) ou aos 96 KB. Em `/sensor-charts` o TDK só aparece em builds **com SD**: esse pipeline
(`appendRs232LivePoints`) está dentro de `#if ENABLE_SDCARD==1`, e a linha `TDK …` do inject é
que alimenta esse gráfico.

**Tooltips e descrições**: cada botão, input, select, quadro de valor, coluna da tabela e o
primeiro parágrafo de cada secção tem `title=…` com o texto em inglês e `data-i18n-title=`
(chaves `tdkTip*`/`tdkDesc*`, EN e PT); os dois campos de texto livre usam
`data-i18n-placeholder`. Os controlos dentro de um `<label>` herdam o `title` do label.
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
| Intervalo | `tdkPollSec` | **60…3600 s** entre leituras (piso de 1 minuto: servidor, ConfigManager e input `min='60'`) |
| Linhas por leitura | `tdkCsvEvery` | 1…60: uma linha de CSV a cada N leituras. Com N>1 a linha leva **min/média/máx** e a energia da janela (`samples,v_min,v_max,i_min,i_max,wh`) |
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

### 8.1 Esquema do CSV (v2)

Uma linha por janela de `csv_every` leituras (1 = cada leitura, comportamento antigo):

```
v2 (tabela):  timestamp,v_meas,v_set,i_meas,i_set,p_w,ovp,uvl,mode,out,samples,v_min,v_max,i_min,i_max,wh
v2 (sensor):  ts, vout, 600.22, vmin, ... , wh, 13.060, n, 6, vset, ... , mode, CV, out, ON
v1 (antigo):  timestamp,v_meas,v_set,i_meas,i_set,p_w,ovp,uvl,mode,out
```

- `v_meas`/`i_meas` são a **média** da janela; `p_w` = média × média;
- `v_min/v_max/i_min/i_max` apanham o pico **dentro** da janela (um único sample de 60 s esconde-o);
- `wh` = energia da janela (regra do rectângulo entre amostras);
- `samples` = quantas leituras entraram na linha (1 no esquema antigo).

Contadores de sessão (RAM, desde o arranque) no `/api/tdk-lambda-status` e nos quadros
"Since boot" da página: `sessionWh`, `sessionCcMs`, `sessionLimitMs`, `sessionOffMs`,
`cvccTransitions` (mudanças CV↔CC). `csvRowMs` = cadência nominal da linha (`poll × N`).

### 8.2 Relatório do CSV

`tests/packs/rs232/tdk_csv_report.py` (só stdlib) lê o ficheiro do dia — v1, v2 ou sensor — e
devolve: energia (kWh), potência média/pico, R aparente (mín/média/máx), tempo em CC, tempo no
limite, tempo com saída desligada, standby, transições CV↔CC, deriva entre a primeira e a última
hora **com carga**, tabela hora a hora, as 5 maiores excursões dentro de uma linha e um gráfico SVG.

```bash
python3 tests/packs/rs232/tdk_csv_report.py TDK_27-11-2026.csv --out report.md --svg report.svg
python3 tests/packs/rs232/tdk_csv_report.py --selftest    # v1/v2/sensor + energia/CC/limite/SVG
```


### 8.3 EMAILDAY e alertas por destinatários

- **EMAILDAY** (`emailday` na consola/Telnet, ou agendado como comando) envia os ficheiros do dia
  da pasta `/<sistema>/`; o `TDK_DD-MM-YYYY.csv` está lá e é aceite (`.csv` + sufixo da data), com
  o **corpo e rodapé standard** dos outros emails. Antes do envio, `safeFlushBeforeCriticalCommand()`
  chama `tdkLambdaCsvFlushNow()` — sem isso o anexo sairia até 30 min atrasado (o buffer de 128 KB só
  escreve à janela/96 KB). Sem cartão SD o ficheiro está no LittleFS e o mesmo flush aplica-se.
- **FTP** (`ENABLE_FTP_CLIENT` ligado no `pack_rs232`): o cliente envia os ficheiros de
  `ftp_local_dir` filtrados por glob (`*.csv;*.log`…). Antes de transmitir um `TDK_*.csv` chama
  `tdkLambdaCsvFlushNow()` — só para esse ficheiro, nunca em cada ciclo — e o journal
  (`ftp_sent.log`) passa a registar o **tamanho realmente transmitido**, não o do scan (o flush
  anterior tornava o do scan obsoleto, o que reenviava o mesmo ficheiro no ciclo seguinte).
- **Destinatários / alertas**: os bytes da fonte entram no pipeline RS232 **sem serem consumidos**
  (cópia não destrutiva), por isso as respostas `DVC?` aparecem no `/monitor` como qualquer linha; e
  com `inject` ligado é injectada uma linha `TDK <ts>, vout, …, power, 784.49, mode, CV`. Os alertas
  de limites escrevem `[TDK] ALERT: <motivo> (<V> / <A>)` no monitor. Regras de destinatário
  recomendadas: palavra **`alert`** (alarme da fonte), **`TDK`** (qualquer leitura, com throttle) ou
  uma regra numérica sobre `power`/`vout` do inject.


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
- **OLED (cartões de linha 3)**: num build com `ENABLE_TDK_LAMBDA=1` a **terceira linha** ganha o
  cartão do PSU como **primeiro da rotação** — o primário leva **peso 2** (aparece 2 de cada N
  turnos), com ordem **determinística** de 6 s (era `random`). O 0.96" só tem 3 linhas, por isso a
  rotação é obrigatória para não se perder informação.

  Modelo dos cartões (`chatgpt/src/screen/screen_cards.h`): cada cartão tem **duas formas**
  (`text` completa, `compact` curta), `weight` (turnos por ciclo) e `stale` (leitura velha → prefixo
  `~`). Quem compõe os cartões de cada módulo é `screen/screen_cards_pack.h`; o `screen.cpp` só
  roda a lista, sem `#if` de packs no caminho de estado.

  A forma escolhida vem do **perfil do painel** (`activeDisplayProfile()`), não de `#if SCREEN_TYPE`:
  - **0.96" (SCREEN_TYPE 1)** — as linhas desenham a **texto tamanho 2 → 10 caracteres**; textos
    até 2× a largura são truncados com `~`, só os muito longos fazem marquee. O cartão do PSU usa a
    forma compacta: `600V 1.30A`, ou `listen` / `ALERT` / `C04` / `wait`;
  - **2.42" (SCREEN_TYPE 2)** — desenha a **tamanho 1 → 21 caracteres**: forma completa
    `TDK 600V 1.30A CV`, idem estados. Todas as linhas passam por `fitForPanel()` (largura do
    painel + `~` na truncagem, em vez do antigo `substring(0, 21)` silencioso) e levam o mesmo
    deslocamento anti-burn-in. **Corrigido**: o perfil do 2.42" declarava `OLED_TEXT_SIZE` (2), o
    que fazia o `fitForPanel` cortar todas as linhas a 10 caracteres; o campo passou a ser o
    tamanho real das linhas de estado (1);
  - **Cartão `! OFFLINE`** — quando não há Wi-Fi **nem** Ethernet (fora de modo AP) entra como
    **primeiro** cartão da rotação: sem rede o pack não faz o seu trabalho. É composto no core
    (estado de rede), não no pack. As vistas com layout próprio (HUD do Rambox, grelha fixa do
    Environment) **não imprimem a linha 3**: recebem este cartão (e o `! ALARM`) por
    `ScreenStatusContext::priorityLine` e mostram-no no lugar mais visível;
  - **`~` = dados velhos** — ambiente com leitura >5 min, TDK com resposta mais antiga que 3
    intervalos de poll e **Modbus** quando o ciclo de poll parou há mais de 3 intervalos
    (`ModbusDevice::lastPollMs`) levam o prefixo; um valor parado deixa de parecer vivo.
- `tdkLambdaUpdate()` (loop): ciclo `<cmd_v>` → `<cmd_i>` (só `split`) → `<cmd_state>` a cada intervalo configurado,
  e flush do CSV **no backend ativo** (SD ou LittleFS) na janela de `getSdcardFlushInterval()`
  (10/30/60 min) ou quando o buffer de 128 KB passa dos **96 KB**.
- `TX> <cmd>` é ecoado para o monitor em `loop_impl.h` quando o envio tem sucesso.

## 10. i18n

Página: o texto de fallback é **inglês** (regra do repo) e todas as etiquetas/descrições usam
`data-i18n`/`data-i18n-title`/`data-i18n-placeholder` com chaves `tdkTip*`/`tdkDesc*`/`tdkTab*`
nos dois pacotes (`lang_en_ui.h` + `lang_pt_ui.h`, 3468 chaves, em sincronia — `validate_i18n.py`).
O catálogo e os literais SCPI ficam em inglês, como o manual.

Ecrã: os rótulos dos cartões (`Temp:`/`Hum:`/`P:`/`Sched:`/`dest:`/`tpl:`, `no mods`,
`! OFFLINE`) vêm de `languages/lang_screen.h` — um struct próprio PT/EN, **fora** das tabelas
posicionais de `lang_active.cpp` (onde um campo inserido no sítio errado desalinha tudo em
silêncio).

## 11. Review findings

| # | Finding | Pri | Suggestion |
|---|---|---|---|
| 0 | Página sem teste automático (o asset é um blob PROGMEM) | **Fixed** | `tests/native/test_tdk_lambda_page.cpp`: shell `.page/.wrap`, 7 secções, 5 tabs, tooltip em cada botão/input/select, ids do JS existem no markup, sem `alert()` nativo, sem `d.ok`; `tests/web/test_tdk_tabs.js` (jsdom): 5 botões criados, painel por omissão, último separador em `localStorage`, init idempotente, `tdkShowTab`/`tdkUse`, rótulos e tooltips traduzidos; `tests/web/test_tdk_preview.js` (jsdom): tabela das últimas 8 leituras (mais recente primeiro, 2/3 decimais, hora local, estado vazio, sem `tbody` não rebenta) |
| 1 | Sem screenshot em `PAGES/desktop/` (flag desligada em todos os packs) | **Fixed** | `PAGES/desktop/tdk_lambda.png` (1440 × 1238) — render do próprio asset pelo harness (`build_desktop_shots.py tdk_lambda` + `desktop_smoke_shot.js`, 7/7 verificações); a captura revelou e fechou 5 defeitos de layout (botões do core a 100%, gráfico escalado desde 0, `&middot;` literal, linha dos limites a partir em três, coluna Description colapsada) |
| 2 | Strings da página em **inglês** (regra do repo: HTML fallback EN); sem chaves `LangStrings` | Info | traduzir via i18n só se o pack exigir PT |
| 3 | `csvRows` em LittleFS conta linhas do último flush (não do ficheiro todo) | Info | ler o ficheiro ao abrir a página, se for preciso |
| 4 | Catálogo em inglês (nomes do manual) | Info | manter: são literais SCPI |
| 5 | Modo *listener* não distingue `DVC?` de outras linhas de 6 campos | Low | usar `STT?` como marcador quando o PLC o pedir |
| 6 | `fitForPanel()` usava `OLED_TEXT_SIZE` para todos os painéis → a vista 2.42" cortava a 10 caracteres | **Fixed** | cols do perfil (`activeDisplayProfile().textSize`); 2.42" = 1 → 21 caracteres |
| 7 | 15 construtores de cartões com `#if` de packs dentro do `screen.cpp` | **Fixed** | `screen_cards.h` (modelo) + `screen_cards_pack.h` (construtores); o core só roda |
| 9 | CSV mudou de esquema (v2: `samples`, `v_min…`, `wh`) | Info | o cabeçalho identifica a versão; leitores antigos que indexem colunas por posição devem usar o header |
| 8 | Cartões de proximidade/Modbus/CAN/USB/rádio sem marca de dados velhos | Low | **parcial**: ambiente, TDK e **Modbus** (`lastPollMs` > 3× intervalo → `~`); CAN/USB/rádio não têm timestamp no runtime — precisam de um `lastRxMs` antes de poderem marcar |

## 12. Source map

| Concern | File |
|---|---|
| Módulo (estado, poll, CSV) | `chatgpt/src/tdk_lambda.h`, `chatgpt/src/tdk_lambda.cpp` |
| Parsers (host-tested) | `chatgpt/src/tdk_lambda_scpi.h`, `tests/native/test_tdk_lambda_parser.cpp` |
| Catálogo de comandos | `tdk_lambda.cpp` (`kCatalog`) + `docs/Fonte/COMANDOS.md` §2 |
| Página (HTML+JS) | `chatgpt/src/webpages/assets/page_tdk_lambda_asset.{h,cpp}` |
| Renderer | `chatgpt/src/webpages/renderers/tdk_lambda_page_renderer.{h,cpp}` |
| Ecrã: modelo dos cartões | `chatgpt/src/screen/screen_cards.h` (+ `tests/native/test_screen_cards.cpp`) |
| Ecrã: construtores por módulo | `chatgpt/src/screen/screen_cards_pack.h` |
| Rotas WiFi + APIs | `chatgpt/src/webpages/routes/web_routes_tdk_lambda.{h,cpp}` |
| Handlers (WiFi+Ethernet) | `chatgpt/src/webpages/handlers_unified/tdk_lambda_handler.{h,cpp}` |
| Ethernet page | `ethernet_bootstrap_routes.h` (`/tdk-lambda`) + `web_shared_pages.{h,cpp}` |
| Hooks | `loop_impl.h` (RX copy + TX echo), `setup_impl.h` (`tdkLambdaBegin`) |
| Config | `ConfigManager.h` (`get/setTdkLambda*`, incl. `csv_every`), `ConfigManager.cpp` (defaults) |
| Análise do CSV | `tests/packs/rs232/tdk_csv_report.py` (relatório + SVG, `--selftest`) |
