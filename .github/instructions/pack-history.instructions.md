# Pack History — Mandatory Rule

## Location

Each pack's investigation and resolution history lives **exclusively** at:

```
HISTORIC/<pack>/HISTORICO.md
```

Examples:
- `HISTORIC/P4-CNC/HISTORICO.md`
- `HISTORIC/Waveshare/HISTORICO.md`
- `HISTORIC/Harmonica/HISTORICO.md`

## Entry format

Each entry must contain:

```markdown
## DD-MM-YYYY — [Descriptive title]

### Symptom
[Observed behavior]

### Root cause
[What was identified as the origin]

### Fix applied
[Implemented fix]

### Binary / Commit
- Commit: `hash` — message
- Binary: `/tmp/pio-build/<env>/firmware.bin` (vX.Y.Z)

### How to revert
[Exact commands to revert if it causes issues]
```

## Rules

1. **NEVER** create history files in `agents/docs/`, `docs/`, or any other repo folder
2. **ALWAYS** update `HISTORIC/<pack>/HISTORICO.md` when investigating or resolving an issue
3. **ALWAYS** include revert instructions for workarounds
4. **ALWAYS** reference the commit and the generated binary
5. Keep entries chronological (newest on top)

## Example

```markdown
# CNC P4 HISTORY

## 03-09-2026 — Crash reboot loop ~9s with CH340 GRBL (WORKAROUND)

### Symptom
P4 CNC board enters a crash reboot loop when CH340 GRBL connects via USB Host.

### Root cause
IDF 5.5.1 HCD calls esp_cache_msync with Addr=NULL during bulk IN transfers.

### Fix applied
Wrapper __wrap_esp_cache_msync returning ESP_OK when addr==NULL.

### Binary / Commit
- Commit: `72ddea1` — fix(P4): esp_cache_msync wrapper
- Binary: `/tmp/pio-build/esp32p4/firmware.bin` (v1.107)

### How to revert
```bash
rm chatgpt/src/esp_cache_msync_wrapper.c
# Remove `-Wl,--wrap=esp_cache_msync` from chatgpt/platformio.ini
git revert 72ddea1
```
```
