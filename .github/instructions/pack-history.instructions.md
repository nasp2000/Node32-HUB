# Pack History — Regra Obrigatória

## Localização

O histórico de problemas e resoluções de cada pack vive **exclusivamente** em:

```
HISTORIC/<pack>/HISTORICO.md
```

Exemplos:
- `HISTORIC/P4-CNC/HISTORICO.md`
- `HISTORIC/Waveshare/HISTORICO.md`
- `HISTORIC/Harmonica/HISTORICO.md`

## Formato de cada entrada

Cada entrada deve conter:

```markdown
## DD-MM-AAAA — [Título descritivo]

### Sintoma
[Descrição do problema observado]

### Causa raiz
[O que foi identificado como origem do problema]

### Fix aplicado
[Solução implementada]

### Binário/Commit
- Commit: `hash` — mensagem
- Binário: `/tmp/pio-build/<env>/firmware.bin` (vX.Y.Z)

### Como reverter
[Comandos claros para reverter se causar problemas]
```

## Regras

1. **NUNCA** criar ficheiros de histórico em `agents/docs/`, `docs/`, ou outras pastas do repo
2. **SEMPRE** atualizar o `HISTORIC/<pack>/HISTORICO.md` quando investigar ou resolver um problema
3. **SEMPRE** incluir instruções de revert para workarounds
4. **SEMPRE** referenciar o commit e o binário gerado
5. Manter entradas cronológicas (mais recente no topo)

## Exemplo

```markdown
# HISTÓRICO CNC P4

## 03-09-2026 — Crash reboot loop ~9s com GRBL CH340 (WORKAROUND)

### Sintoma
P4 CNC (.7) entra em reboot loop quando CH340 GRBL ligado ao USB Host.

### Causa raiz
IDF 5.5.1 HCD chama esp_cache_msync() com Addr=NULL durante bulk IN transfers.

### Fix aplicado
Wrapper __wrap_esp_cache_msync que retorna ESP_OK se addr==NULL.

### Binário/Commit
- Commit: `72ddea1` — fix(P4): esp_cache_msync wrapper
- Binário: `/tmp/pio-build/esp32p4/firmware.bin` (v1.107)

### Como reverter
```bash
rm chatgpt/src/esp_cache_msync_wrapper.c
# Remover '-Wl,--wrap=esp_cache_msync' de chatgpt/platformio.ini
git revert 72ddea1
```
```
