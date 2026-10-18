# Teste API Pack Radio (Harmonica Box) — plano + relatório

> Objetivo: testar TODAS as paginas/opcoes/botoes do pack Radio via API/curl, encontrar
> problemas ou coisas que nao funcionem conforme a logica. NAO corrigir — so relatar.
> Board alvo: 192.168.1.30 (Harmonica/RADIO). Confirmar por mDNS ou /api/status antes.
> Autenticacao: Basic admin:admin em todos os POST.

## Paginas do pack (confirmadas em selftest_handler.cpp + pack_radio.h — flags ON)
- `/radio-ui` (ENABLE_UDA1334A) — internet radio / local music player (I2S DAC)
- `/uda1334a` (ENABLE_UDA1334A) — pagina/teste do DAC I2S UDA1334A
- `/monitor` (ENABLE_PAGE_MONITOR) — logs seriais (Web Serial Monitor)
- `/serial-monitor` (ENABLE_PAGE_MONITOR) — mesmo que /monitor (alias)
- `/files` (ENABLE_PAGE_FILES) — file browser com controlos de radio (Play/Stop/Random/Next)
- `/ota` (ENABLE_OTA) — pagina de OTA firmware
- `/` (home) — botoes de entrada (Radio UI + File browser)
- Self-test — `/selftest` (manifest: /radio-ui, /uda1334a, /monitor, /serial-monitor, /files, /ota)

Features OFF neste pack (NAO testar): alerts, recipients, rs232, modbus, can,
scheduler, telnet, debugger, proximity, email, access, sdcard-page, commands, structures,
api-reference, ethernet.

## Secoes de teste (ordem)
1. /radio-ui (players + todos os endpoints)
2. /uda1334a (DAC I2S + i2s-poc + mp3-test)
3. /monitor + /serial-monitor
4. /files (file browser + radio controls + download)
5. /ota (pagina + enable-ota + firmware-upload)
6. / (home)
7. /selftest (manifest de paginas)
8. Disco SD + LittleFS (persistencia)

## Metodologia
1. Para cada pagina: GET da pagina (200?) + GET dos endpoints de estado.
2. Para cada botao/acao da UI: mapear para endpoint e testar POST (valido + invalido).
3. Verificar /api/radio/status e /api/status apos acoes.
4. NAO mexer em codigo. So relatar aqui.

---

## 1. PAGINA /radio-ui (internet radio)
Endpoints (de PAGES/radio_ui.md §6):
### Estado / transporte
- [ ] GET /api/radio/status
- [ ] POST /api/radio/stop
- [ ] POST /api/radio/next
- [ ] POST /api/radio/play-url (URL stream valido + invalido)
- [ ] POST /api/radio/play-sd (ficheiro SD)
- [ ] POST /api/radio/volume (0-100, <0, >100)
- [ ] POST /api/radio/seek (0.0-1.0, stream deve dar 400)
- [ ] POST /api/radio/shuffle
- [ ] POST /api/radio/repeat (0/1/All)
- [ ] POST /api/radio/sleep (0/15/30/60/90/120)
- [ ] POST /api/radio/eq (preset)
### Queue
- [ ] GET /api/radio/queue (cap 120)
- [ ] POST /api/radio/queue {items,index} (ate 256)
### Favorites
- [ ] GET /api/radio/favorites
- [ ] POST /api/radio/favorites/add
- [ ] POST /api/radio/favorites/remove
- [ ] POST /api/radio/favorites/play
### Library / search
- [ ] GET /api/radio/library?path=
- [ ] GET /api/radio/library/art
- [ ] GET /api/radio/search (nome ficheiro SD)
### Discover
- [ ] GET /api/radio/stations/search?q=
- [ ] GET /api/radio/stations/tags (dead?)
- [ ] GET /api/radio/stations/countries (dead?)
- [ ] GET /api/radio/stations/default (dead?)
### Playlists (.m3u SD)
- [ ] GET /api/radio/playlists
- [ ] POST /api/radio/playlist/save
- [ ] POST /api/radio/playlist/load
- [ ] POST /api/radio/playlist/delete
### Agenda (schedule)
- [ ] GET /api/radio/schedule
- [ ] POST /api/radio/schedule {"slots":[...]} texto
### Resume
- [ ] GET /api/radio/resume
- [ ] POST /api/radio/resume
### UPnP
- [ ] POST /api/radio/upnp/discover
- [ ] GET /api/radio/upnp/servers
- [ ] GET /api/radio/upnp/browse
### Artwork
- [ ] GET /api/radio/artwork
### Endpoints mortos (testar resposta)
- [ ] POST /api/radio/toggle
- [ ] POST /api/radio/random
- [ ] POST /api/radio/i2s-poc
- [ ] POST /api/radio/mp3-test
- [ ] GET /api/radio/eq

## 2. PAGINA /uda1334a (DAC I2S test)
- [ ] GET /uda1334a (pagina 200?)
- [ ] Endpoints de teste do DAC: procurar em audio_dac_uda1334a / audio_handler
- [ ] POST /api/radio/i2s-poc (teste I2S) — conta como teste desta pagina
- [ ] POST /api/radio/mp3-test (teste mp3) — conta como teste desta pagina

## 3. PAGINA /monitor + /serial-monitor (Web Serial Monitor)
- [ ] GET /monitor (pagina 200?)
- [ ] GET /serial-monitor (alias, 200?)
- [ ] GET /api/monitor-status — estado do monitor (responde? que campos?)
- [ ] Streaming de logs: GET /monitor ou /serial-monitor devolve log em tempo real?
- [ ] POST de mensagem de teste se houver endpoint (verificar aparece no monitor Serial)

## 4. PAGINA /files (file browser + radio controls)
- [ ] GET /files (pagina 200?)
- [ ] GET /api/files ou listagem de ficheiros SD (endpoint real?)
- [ ] Botoes radio no file browser: Play/Stop/Random/Next (mapear para /api/radio/*)
- [ ] Download de ficheiro: GET /api/sd/download?path=/playlists/<nome>.m3u (conteudo bruto)
- [ ] GET /api/sd-status — SD montado?
- [ ] GET /api/sdcard-config — config do SD

## 5. PAGINA /ota (firmware update)
- [ ] GET /ota (pagina 200?)
- [ ] POST /api/enable-ota (ativa modo OTA? devolve ok?)
- [ ] POST /api/firmware-upload (upload de bin — testar com bin valido; NAO fazer OTA real da board .30 sem ordem)
- [ ] GET /api/status apos enable-ota (campo ota_enabled?)

## 6. HOME PAGE + SELF-TEST
### Home
- [ ] GET / (home 200?)
- [ ] Botoes: Radio UI link (-> /radio-ui), File browser link (-> /files) — apontam certo?
### Self-test
- [ ] GET /selftest (manifest de paginas; verificar se /radio-ui, /uda1334a, /monitor,
      /serial-monitor, /files, /ota aparecem como PASS)
- [ ] GET /api/selftest (se existir endpoint)

---

## 7. DISCO SD + LITTLEFS (persistencia real no disco)
> Objetivo: confirmar que o que e suposto estar gravado no disco existe e e valido.
> SD: /playlists/*.m3u (playlists) + biblioteca de musicas.
> LittleFS: /config/radio_queue.json, /config/radio_favorites.json,
>          /config/radio_state.json, /config/radio_schedule.json.

### SD card
- [ ] GET /api/sd-status — SD montado? espaco?
- [ ] GET /api/sdcard-config — config do SD
- [ ] GET /api/radio/library?path= — lista biblioteca (ficheiros/pastas)
- [ ] GET /api/radio/playlists — lista .m3u no SD
- [ ] GET /api/sd/download?path=/playlists/<nome>.m3u — conteudo bruto do ficheiro
- [ ] GET /api/radio/library/art — cover art (se houver)

### LittleFS config (confirmar via GETs de estado — nao ha endpoint de leitura bruta)
- [ ] GET /api/radio/queue — devolve queue gravada em /config/radio_queue.json?
- [ ] GET /api/radio/favorites — devolve favorites em /config/radio_favorites.json?
- [ ] GET /api/radio/schedule — devolve agenda em /config/radio_schedule.json?
- [ ] GET /api/radio/resume — devolve resume em /config/radio_state.json?
- [ ] GET /api/radio/status — devolve volume/eq/sleep/state de /config/radio_state.json?
- [ ] Apos POST (ex.: favorites/add, schedule, volume), refazer GET e confirmar que
      o valor persistiu (prova que o ficheiro LittleFS foi reescrito).

### Teste de corrupcao/validade
- [ ] Gravar agenda (POST /api/radio/schedule) e reler GET — conteudo igual?
- [ ] Se possivel: POST com JSON malformado -> board rejeita ou grava lixo?
      (nao corrigir, so relatar se grava corrompido)

---

## Problemas encontrados (preencher durante teste)
| # | Pagina / Endpoint / Botao | Esperado | Real | Severidade | Notas |
|---|---|---|---|---|---|
|   |   |   |   |   |   |
| 1 | /api/radio/eq (form-urlencoded) | 200 | 501 (so aceita JSON {gains:[...]}) | BAIXA | nao e bug, e formato — handler eq POST usa body accumulator JSON. UI envia JSON. |
| 2 | /api/radio/toggle, /random (doc dizia "mortos") | 404/nao existe | 200 (handlers reais) | N/A | radio_ui.md desatualizado — endpoints NAO estao mortos, funcionam. |

## Notas de logica (divergencias conhecidas)
- radio_ui.md §10 dizia endpoints mortos (/toggle, /random, /i2s-poc, /mp3-test, GET /eq) — TESTE REVELA:
  * /toggle, /random FUNCIONAM (200, handlers reais)
  * /i2s-poc FUNCIONA (200)
  * /mp3-test -> 400 (precisa param?)
  * GET /eq FUNCIONA (200), POST /eq FUNCIONA com JSON (200)
  => radio_ui.md ESTA DESATUALIZADO (nao e bug firmware, e doc stale)
- #2 lista STATIONS hardcoded != /api/radio/stations/default (endpoint default pode estar morto na pratica — testar se devolve 200)
- #8 GET queue cap 120 vs POST 256 (verificar se GET trunca)
- Client swallow errors em volume/shuffle/repeat/seek (nao testavel via API, e UI)


## Notas de logica (divergencias conhecidas, de radio_ui.md §10)
- #2: lista STATIONS hardcoded na pagina != /api/radio/stations/default (endpoint morto na pratica)
- #3: endpoints mortos: /toggle, /random, /i2s-poc, /mp3-test, GET /eq
- #4: Ethernet handlers mortos (pack tem ENABLE_ETHERNET 0)
- #5: 405-vs-404 errados em varios paths radio
- #6: client swallow errors em volume/shuffle/repeat/seek
- #8: GET queue cap 120 vs POST 256
