# Changelog

## Unreleased — TouchMeteo-7 (Waveshare ESP32-S3-Touch-LCD-7)
- **Novo pack `S3_TOUCH7` / TouchMeteo-7 🌤️** (env `s3_touch7`, board `TARGET_BOARD_S3_TOUCH7`, `docs/waveshareLCDTouch7.md`): Node32-HUB nativo no LCD 7" 800x480 RGB+touch — estação meteo + viewer de peers + frases locais. Packs existentes intocados.
- **Display RGB:** perfil `DISPLAY_S3_TOUCH7_800x480` (SCREEN_TYPE 3), `Touch7Display` (ESP32_Display_Panel + LVGL v8.4, tudo em PSRAM), 3 temas azuis Claro/Médio/Escuro, botões 3D, relógio digital×3 + analógico×2, screensaver.
- **Sensores:** BH1750 (lux + auto-brilho) via Wire1; DS3231 e INA226 migrados para Wire1 neste board (display é dono do I2C0).
- **Viewer peers 2+2:** snapshot-first (`/cam/snapshot` + `/api/camera/status`), decode JPEG→RGB565, PIR dispara snapshot (F3b); previsão Open-Meteo.
- **Som:** frases WAV via PWM GPIO6 → PAM8403; página `/falas` (listar/ouvir/apagar + peers JSON).
- **Testes:** `test_touch7_logic` (9 casos) + `test_touch7.js`; suite nativa 75/75.
- **Pendente de HW (não compila aqui):** primeiro `pio run -e s3_touch7`, scan I2C, timings RGB, GPIO do PIR, caminho TF-SPI (ver `PAGES/BACKLOG.md` T7-*).

## Unreleased (2026-08-03) — Radio / Harmonica-BOX
- **Radio-ui — Vinil destacado:** disco mais luminoso com sulcos concêntricos subtis, reflexo radial e aro neon cyan/magenta mais vivo; halo suave atrás do vinil; label central e ícone musical mais visíveis
- **Radio-ui — Fundo:** navy escuro ligeiramente mais luminoso com gradiente radial subtil atrás do vinil (centro mais claro, extremidades escuras)
- **Radio-ui — Título:** branco quase puro, glow cyan subtil e pequeno halo magenta/cyan elegante (sem exagero neon)
- **Radio-ui:** revisão completa do visual — paleta retro-music escura (creme quente + âmbar/cobre), página como painel com moldura sobre fundo de vinil (separação vertical), botões de baixo subidos (fix da barra do Windows/taskbar), layout compacto sem scroll; cabeçalho "🎵 Harmonica-BOX" (sem botão home)
- **Radio-ui — Playback por defeito:** abre no painel Play (antes Library)
- **Radio-ui — Presets de EQ:** Flat/Rock/Jazz/Pop/Voice/Classical (aplica os 5 bandas via `/api/radio/eq`)
- **Radio-ui — Sleep timer:** botão ⏰ Off/15/30/45/60 min com countdown, desliga o stream ao fim
- **Radio-ui — Tocadas recentemente:** últimas 10 estações/ficheiros (localStorage) com play rápido
- **Radio-ui — Diretório de estações:** 12 estações PT pré-carregadas (Antena 1/2/3, RFM, Comercial, Cidade, M80, Smooth, Observador, TSF, Renascença...)
- **Radio-ui — Sinal WiFi:** ícone 📶 no topo (RSSI) via novo campo `wifi_rssi` no `/api/radio/status`
- **Radio-ui — Visualizer:** barras de EQ animadas sob a capa quando a tocar
- **Rádios online guardadas nas playlists:** secção "Saved online radios" no painel Playlist com play (reutiliza favoritos)
- **Sound test:** o botão "Teste de Som" agora toca mesmo — melodia de 5 tons ascendentes estéreo via `UDA1334ADAC::playTestSound()` (task separado, não bloqueia)
- **Pack radio:** OLED desativado (`ENABLE_LCD_SCREEN 0`); pinos UDA1334A explícitos (BCLK=41, WSEL=42, DIN=19); 3ª linha do OLED (screen.cpp) com estado do rádio quando ativo noutros packs
- **Página `/uda1334a`:** painel de ligações removido; texto "Information" alinhado à esquerda

## Unreleased (2026-08-03)
- **GRBL Overrides:** bytes real-time corrigidos (feed `0x90/0x93/0x94`, spindle/laser `0x99/0x9C/0x9D`) — aplicar override agora muda mesmo a potência do laser e a velocidade; retry silencioso do POST durante stream; só marca como enviado após sucesso (elimina `ERR /api/grbl/override` falso)
- **GRBL Frame:** todos os movimentos levam `F{frame_feed}` explícito (aproximação ao primeiro canto, trace e retorno) — sem depender de feed modal
- **GRBL Overrides:** sliders deixam de ser persistidos em `localStorage` (reset a 100% no reload)
- **GRBL Preview:** fix `SD LOAD WORKSPACE silent fail-read` — reload silencioso redundante antes de iniciar stream saltado quando a workspace já tem o ficheiro (`state.workspacePath` dedup)
- **GRBL Streaming:** compensação height-map adicionada ao caminho staged (fallback PSRAM); `sendLineFromPsramLocked` (código morto) removido
- **GRBL Jog:** botão central `btn_jog_xy_center` estava sem handler — agora faz `$J` para o centro do percurso máximo (`runJogXYCenter`)
- **Pack Radio:** pinos UDA1334A explícitos no pack (BCLK=41, WSEL=42, DIN=19 — a board não expõe 25/26/27); `Docs/PINOUT_HARDWARE_CURRENT.html` atualizado com ligação módulo UDA1334A ↔ ESP32

## v1.092 (2026-08-03)
- **GRBL Overrides:** sliders só atualizam a UI; "Apply Override" envia via bytes real-time GRBL (novo `POST /api/grbl/override`, `0x96-0x9F`) — M220/M221 (rejeitados por alguns firmwares) deixam de ser enviados, eliminando erros `FILE_STREAM` e a dessincronização sent/ack durante o job
- **GRBL Height Map / Z-probe:** compensação de altura finalmente aplicada ao stream (era código morto — `grblApplyHeightMapToLine` nunca era chamada); sinal corrigido (`newZ = zVal + surface`); interpolação serpentine corrigida (linhas ímpares liam cantos trocados); `safeZ` usado na travessia; novo campo "Clear Z" no widget (antes hardcoded); captura da superfície em work coords; tracking de XY comandado para linhas Z-only
- **GRBL Preview 2D:** durante o job mostra em tempo real o já cortado (dim) vs. restante (brilhante) + segmento atual destacado com head dot; Cut/Rapid/Engrave movidos para a mesma linha dos selectors (Auto/Top/Iso/Side)
- **GRBL Streaming:** drain da prepared queue corrigido no pipeline staged (fallback quando PSRAM falha) — o stream nunca drenava e não terminava
- **GRBL:** `quickWireframe` — fix de dupla-contagem de X/Y/Z em modo incremental (G91)
- **Web:** fix dos charts de sensores (ReferenceError `opts` em `fetchChartJson` → charts vazios); `cache:no-store` nas páginas de config; mojibake (dupla codificação CP1252) limpo em ~13 ficheiros de páginas + handlers; chaves GRBL novas em `lang_en_ui.h`/`lang_pt_ui.h`
- **Testes:** testes 3D obsoletos removidos (preview é 2D); suites web novas/atualizadas (override apply-on-demand, render job-aware 2D, `test_probe_grid` serpentine); testes nativos novos (atomic_write, auth, config_*, handler_*, littlefs, modbus_registers, route_registry, scheduler_commands, sensor_*) + stubs LittleFS/ArduinoJson enriquecidos
- **Acumulado desde v1.091:** packs (cam, environment, p4, psram, radio, rambox, s3_cnc, waveshare), handlers/config/sensors/languages e infra de testes nativos refinados

## v1.089 (2026-07-12)
- **Fase C (UX padrão) concluída:** loading overlay em 33 handlers, bottom nav mobile, save pattern unificado, `alert()` nativo eliminado
- **Fase D (mobile-first) parcial:** tabelas em `.table-wrapper` (8 ficheiros), abas em `/rs232` e `/can`, `auto.js` injetado nas monitor pages, sidebar Edit.GC style no GRBL controller
- **Fix:** página `/printer` — `strstr()` em PROGMEM falhava no P4; partido em fragmentos
- **Fix:** `auto.js` bottom nav — `DOMContentLoaded` para evitar `appendChild` em body null
- **Fix:** `/camera` e `/radio-ui` já usam `streamPageShellStart` + `streamAutoJsFooter`

## v1.088 (2026-07-09)
- **Testes nativos:** 3 mocks de interface (IConfigStore, IEmailService, ILogger) + 2 suites (config_utils, email_builder)
- **Fix:** build errors (forward decls, buildMessage overload, SD scope)
- **Fix:** `volatile++` deprecation warnings em loop_impl.h

## v1.087 (2026-07-07)
- **Testes C++:** 171 casos, 848 asserts. 3 suites compilam produção, 4 usam headers produção
- **ConfigManager refactor:** NvsStore + LittleFsStore + ConfigValidator
- **NtpManager extraído** do ConfigManager com `/config/ntp.json` próprio
- **Packs:** P4 CNC: `--wait-for-usb` opcional; S3 CNC packs; Waveshare 8DI8DO refinado
- **Scripts:** `validate_feature_flags.py`, `fix_includes.py`, `rename_firmware.py`
- **USB Host:** revertido para v1.06 (sem delays, sem power cycles)

## v1.086 (2026-07-05)
- **19 interfaces** definidas em `src/interfaces/` (IConfigStore, IEmailService, etc.)
- **OTA security:** RSA-2048+SHA-256 + chip magic + rollback automático
- **Quality scripts:** `format_check.py`, `lint_comments.py`, `scan_secrets.py`, `smoke_build.py`, `size_report.py`
- **Pre-commit hooks:** scan_secrets, lint_comments, validate_i18n, validate_packs, format_check
- **Telnet desativado** por defeito em todos os packs de produção
- **Endpoint audit:** nenhum endpoint devolve passwords sem auth

## v1.085 (2026-07-02)
- ConfigManager single-slot + allocator policy + cleanup
- Fix: ethernet_bootstrap_routes.h #if guard restaurado
- Fix: /printer page sempre disponível (core page)

## v1.084 (2026-06-28)
- **Renomeação massiva** de identificadores PT para EN
- **Camera UI:** redesign completo com streaming, presets, motion detection, DashCam
- **Radio UI:** redesign estilo rAudio com álbum art, equalizador, favoritos
- **S3 CNC packs:** USB + UART variants
- **Packs:** S3 CNC packs, P4 CNC refinado
- **GitHub Actions desativados** (repo privado)
- **BOM fix:** 31 source files sem byte order mark

## v1.083 (2026-06-20)
- **Ethernet parity completa:** 12 camera handlers + 11 routes, 284 WiFi↔Ethernet gaps fechados
- Route registry: ROUTE__api_unikon_sync_calendar adicionado
- AGENTS.md: language rules

## v1.082 (2026-06-18)
- G-code sender: WebSocket GRBL client + macro widget + outline preview spatial clustering
- Modal G-code suppression + PSRAM progress bar
- Frame widget: cap max speed at 3000 mm/min

## v1.081 (2026-06-15)
- USB Host: revertido para v1.06 (remove delays, root port power cycle)
- eFuse: USB download disable option no GcodeSender UI (S3 only)
- i18n: eFuse burn UI keys (PT+EN)
- AP desliga imediatamente quando STA conecta

## v1.08 (2026-06-10)
- SD upload: FileReader (fixes empty_content), preview convex hull + point-in-polygon
- Releases table com GcodeSender link no README

## v1.07 (2026-06-05)
- **Pack system:** packs/pack_*.h com validação build-time
- **Feature flags:** validate_feature_flags pre-script
- P4 RISC-V toolchain fixes
- Printer emulation: #if ENABLE_* guards

## v1.06 (2026-05-28)
- **Route registry:** sistema de registo unificado de rotas WiFi+Ethernet
- handlers_unified/ criado
- MAX_API_ROUTES=256

## v1.05 (2026-05-20)
- **Native C++ tests:** doctest framework, Makefile, stubs
- test_modbus_crc, test_charset, test_string_utils
- run_native_tests.py pre-script

## v1.04 (2026-05-12)
- **Web UI 4-layer split:** assets/ → renderers/ → handlers_unified/ → routes/
- i18n: lang_pt_ui.h/lang_en_ui.h com 1275 keys cada
- validate_i18n.py build-time sync check

## v1.03 (2026-05-05)
- Dual-storage config: NVS (boot-critical) + LittleFS (slots)
- ConfigManager com atomic write pattern
- Sensor config managers independentes

## v1.02 (2026-04-28)
- Bottom navigation bar mobile
- Loading overlay + customAlert/customConfirm
- Home button em todas as páginas

## v1.01 (2026-04-20)
- Packs: N16R8, Waveshare 8DI8DO, P4 CNC
- Module guard rule (#if ENABLE_X==1)
- build_src_filter para exclusão de .pio/ e managed_components/

## v1.00 (2026-04-12)
- Primeiro release funcional
- RS232, Modbus RTU/TCP, CAN, sensores, email, scheduler
- Web management over WiFi + Ethernet (W5500)
