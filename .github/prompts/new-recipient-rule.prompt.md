---
agent: agent
description: Checklist for adding a new recipient rule operator (matching type). Covers the 6 files that must be updated in sync.
---

# New Recipient Rule Operator

Adding a new operator touches **6 files**.

## Operators reference

| Code | Int | Behaviour |
|---|---|---|
| `<`, `>`, `!=`, `=`, `>=`, `<=` | 1-7 | Numeric threshold, stateless, per-line |
| `V` | 8 | Variation, fires when value deviates from last seen |
| `C` | 9 | Count, fires after N consecutive matches |
| `P` | 10 | Presence watchdog, fires when keyword absent > threshold |
| `Cw` | 11 | Count per window, N matches within T seconds |
| `Hy` | 12 | Hysteresis, fires above high threshold, re-arms below low |

## Steps

### 1. `recipient_matching_impl.h` - `opStrToInt()`
Add `if (op == "Xy") return 13;` (next integer).

### 2. State variables (stateful operators only)
In `chatgpt.ino`, add per-slot maps:
```cpp
static std::map<int, int>           recipientXyValue;
static std::map<int, unsigned long> recipientXyTimestamp;
```

### 3. Match logic in `collectMatchedRulesFromContent()`
Add case in the switch. Read config via `configManager.getRecipientSlotXxx(slotIdx)`. Return `true` only when alert should fire.

### 4. Reset in `finalizeRecipientNotificationResult()`
Erase state maps after successful send.

### 5. i18n - add field to `lang_types.h`, define in `lang_active.cpp` (both LANG_PT/LANG_EN)

### 6. Web UI - add `<option>` in `page_recipients_asset.cpp` operator `<select>`

## Rules
- Never access `configManager.config["recipients"]` directly - use accessors
- No heap allocation in matching loop - stack only
- State maps keyed by slot index (1-50), not recipient name
- Must be safe from RS232/LPT receive task (Core 0)