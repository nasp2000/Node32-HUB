# Rambox32 — página `/m` no telemóvel

Página **própria** do pack (o `/rambox` do desktop fica intacto). No telemóvel,
`/rambox` responde 302 → `/m`; `?desk=1` abre a página completa.

## Organização

```
        Status          Arrivals          Config          System
       ───────         ──────────        ────────        ────────
  (aba activa em cima, risco por baixo; visitadas a azul)
```

- **4 páginas**, uma por ecrã, **sem scroll de página**, conteúdo **centrado**.
- **Swipe** + setas + tocar no nome; **250 ms** por botão (anti-toque acidental).
- Nome do pack no topo (`Rambox32`) e badge online/offline.

## Ficheiros

| # | Imagem | O que mostra |
|---|---|---|
| 01 | `01_status.png` | **Status**: relógio (ligado), columbófilo, prova, nº de chegadas, RS232 (9600 8N1), último poll; **SYNC CLOCK** e **POLL NOW** |
| 02 | `02_arrivals.png` | **Chegadas**: anilha · hora · prova, com `↻` recarregar, **✉ reenviar email** e `✕` limpar (confirmação) |
| 03 | `03_pombal.png` | **Pombal**: pombos registados (anilha + chip) |
| 04 | `04_races.png` | **Concursos**: nome · data · distância · nº de pombos |
| 05 | `05_config.png` | **Config**: **BRICON/UNIKON**, leitura ON/OFF, intervalo, **Série: RELÓGIO / MANUAL**, RS232 ON/OFF, baud, paridade, dados/stop e **SAVE** |
| 06 | `06_system.png` | **Sistema**: firmware, uptime, heap, PSRAM, SD, WiFi + Reiniciar |

## Notas

1. **Bricon ou Unikon**: só um relógio pode estar activo (RS232 partilhado). O
   Config liga o escolhido e desliga o outro no mesmo SAVE.
2. **Intervalo de leitura** vai para o `-config` do relógio activo (`interval`);
   ON/OFF usa `enabled`.
3. **Pombal** e **Concursos** vêm de `/api/<relógio>-pombal` e `-contests` e caem
   para o Unikon quando o Bricon não existe nesse build.
4. **Série**: por omissão o relógio manda no perfil (Bricon **9600 8N2**, Unikon
   **9600 8N1**) e os campos RS232 ficam bloqueados a mostrar esse perfil. Em
   **MANUAL** os campos abrem e são gravados em `/api/config?section=rs232`
   (`rs232_enabled`, `baudrate`, `parity`, `data_bits`, `stop_bits`), com
   `manual=1` no `-config` do relógio; `manual=0` volta ao perfil do relógio.
5. **POLL NOW** (`-poll`) e **SYNC CLOCK** (`-sync-calendar`) são acções pontuais.
6. **✉ reenviar email**: o sistema envia sozinho (chegadas) e o botão pede o
   relatório do pombal da época — `POST /api/bricon-email-pombal` **e**
   `/api/unikon-email-pombal`, os dois lados com o mesmo relatório e layout.
