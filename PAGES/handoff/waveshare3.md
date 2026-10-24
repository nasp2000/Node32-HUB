# HANDOFF — workstream `waveshare3` (board .29 / Equip15 / pack WAVESHARE)

Sessão OpenCode: `ses_ed9ed6f70ffeqnn8q688zRMu3o` · Repo: `/home/nasp/Code/Node32-HUB`
Última atualização: 2026-10-23 · Board: 192.168.1.29, fw v1.120 (pack `waveshare_8di8do` / `WAVESHARE`)

> Este ficheiro é a memória do workstream. O brief de cada tarefa é um `opencode_task_*.md` separado e efémero.

---

## 1. Objetivo do workstream

Manter o pack **WAVESHARE / Node32-OEE** (board .29 "Equip15") fiável no que diz respeito a **email, OEE/gráficos e FTP**, com diagnóstico suficiente para se perceber uma falha remotamente, sem depender do monitor série (que perde o buffer a cada reboot).

## 2. Estado

### Feito e entregue (build + OTA na .29 em 2026-10-23 12:32)

| Commit | O que resolve |
|---|---|
| `6d316ac` | `EMAIL_PIPELINE_FAIL` passa a levar `err=<motivo do transporte>`; dedupe+trim de destinatários; `.chart` gravado pela data alinhada ao turno e endpoints a usarem a janela de produção 22:00→22:00 |
| `75cd1df` | Estado dos últimos envios FTP persistido em `/config/ftp_status.json` (LittleFS), sobrevive a reboot; `GET /api/ftp-status` ganha `recent`, `last_time_str`, `last_epoch` (chaves antigas mantidas) |
| `ca24290` | Coluna "Result" da tabela de envios mostra **OK / FAIL** por extenso (i18n EN/PT), em vez de só ✔/✘ |

Verificação feita: **112/112 suites nativas**, **74/74 testes web FTP**, `audit_i18n` 177 chaves (sem dívida nova). OTA: `UPGRADE OK`, config sobreviveu **14/14**.

### Confirmado em hardware
- `POST /api/ftp-upload-now` → `{"success":true,"uploaded":10}`; `/api/ftp-status` devolveu `ok_count:10`, `last_time_str:"23-10-2026 12:35:03"` e `recent[]` com ficheiro/tamanho/data-hora/resultado. Ficheiro conferido no servidor: `turno1_2026-10-17_05-59.txt`, 802 B (igual ao reportado).
- `err=` provou a causa do silêncio do email: **`auth-pass 535-5.7.8 Username and Password not accepted`** — o Gmail rejeita a credencial de `nasp2000@gmail.com`.

### Pendente / bloqueado
1. **Email bloqueado por credencial (não é código).** A *app password* de `nasp2000@gmail.com` deixou de valer depois de 22-10 22:00. Precisa de app password nova no Google e colada em *Configurar Email* na .29. Alternativa: pôr a .29 a enviar por `joana2escura@gmail.com` (a .24 já usa e funciona).
2. Persistência do estado FTP **por reboot** ainda não observada em hardware (o reboot diário das 22:00 comprova).
3. Anomalias anotadas, sem fix: ver §5.

## 3. Decisões tomadas (e becos sem saída)

- **Diagnóstico em vez de adivinhação**: o log persistido passou a levar o motivo do SMTP. Sem isso, qualquer análise morre porque o ring do monitor série não sobrevive ao reboot.
- **FTP**: `interval_min=720` (12 h) e `max_files=10` **ficam como estão** — por turno há ≤3 ficheiros novos (≤9/dia), logo o teto nunca aperta. Não aumentar a frequência: não foi pedido e só gera escritas.
- **`.chart` por dia de produção**: alinha o eixo do gráfico com os totais/donut (`22:00→22:00` com turnos 6/14/22), que era a origem da "sensação de contas trocadas". Os `.chart` antigos, nomeados pelo dia civil, ficam órfãos no cartão — aceitável, a rotação limpa em ~2 dias.
- **Não tocar** na máscara de password do LittleFS (`********` por design; a real vive em NVS) nem no formato do `/config/ftp_sent.log` (é o journal do `only_new`).

## 4. Mapa de ficheiros

- `chatgpt/src/smtp_core_impl.h` — envio; `smtpSendEmail` ~1098, último erro do transporte (base do `err=`)
- `chatgpt/src/email_event_fields.h` — formatação dos detalhes do evento (novo)
- `chatgpt/src/email_recipients.h` — normalize + dedupe de destinatários (novo)
- `chatgpt/src/observability.h` — `observabilityLogEmailPipeline()` com parâmetro `error`
- `chatgpt/src/ftp_status_store.h` — store persistido do estado FTP (novo); `ftpStatusFormatTs()` → `DD-MM-YYYY HH:MM:SS`
- `chatgpt/src/ftp_client.cpp` — `ftpLoadSentList`/`ftpTrimSentLog` ~706/~726 (journal `only_new`, **não mexer no formato**)
- `chatgpt/src/webpages/handlers_unified/ftp_handler.cpp` — payload do `/api/ftp-status` (~95 e ~118)
- `chatgpt/src/webpages/assets/page_ftp_asset.cpp` — tabela de envios (bloco `#ftp-status`, render ~226–320)
- `chatgpt/src/sensors/proximity_manager.{h,cpp}` — `proximityShiftDayElapsedSec`/`proximityShiftDayAlignedDate`/`proxShiftDayStartFromCivilMidnight`
- `chatgpt/src/webpages/handlers_unified/proximity_handler.cpp` — `handleProximityChartDataGet` (600) e OEE (~735)
- `chatgpt/src/webpages/handlers/rs232_history_handler.cpp` — endpoint legacy que serve o gráfico via WiFi (registado primeiro)
- `PAGES/email.md`, `PAGES/ftp.md`, `PAGES/proximity_dashboard.md` — documentação atualizada

## 5. Gotchas / anomalias conhecidas

- **A página de ficheiros mostra 10 por página** (`PAGE_SIZE=10` em `files_page_handler.cpp:227`, paginação por `?p=`). Contar ficheiros pela página 1 dá resultados errados — foi o que me fez subestimar os pendentes do FTP.
- `GET /api/proximity-chart-data` usa **`?date=DD-MM-YYYY`**, não `?file=`. Com o parâmetro errado devolve o dia atual, o que parece "ignora o ficheiro".
- Monitor série devolve `ts` **+1 h** (fuso aplicado duas vezes). Ainda por investigar.
- Log série às 23:02 de 22-10: `Prox: CSV append failed (partial) row DROPPED (torn write, card broken)` — uma linha perdida; suspeita de cartão SD a precisar de reseat/fsck.
- Cadência do `.chart` mudou de 31 min para 1 min por volta das 06:59 de 23-10 — por explicar.

## 6. Regras do agente (não negociáveis)

- **NÃO** correr `build_firmware.sh`, **não** assinar, **não** fazer OTA — isso é da Joana.
- Correr sempre os testes nativos; se mexer em assets web, `tests/web/extract.py` antes.
- **Toda** a escrita em LittleFS tem de passar por `storNoteLfsWrite(...)` — escrita não contada é bug.
- Mudança mínima, sem bump de versão (só com ordem explícita).
- Compatibilidade do payload das APIs: acrescentar chaves, nunca remover.

## 7. Próximos passos sugeridos

1. Desbloquear o email (app password nova) e confirmar um `ShiftEnd` real a chegar.
2. Confirmar a persistência do estado FTP após o reboot das 22:00.
3. Investigar o `ts` +1h do monitor série (barato e chato de ver).
4. Decidir se o cartão SD da .29 precisa de reseat/verificação.
