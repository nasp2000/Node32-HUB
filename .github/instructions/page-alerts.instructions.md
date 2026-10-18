# Page Agent: Inactivity Alerts

## Identity
- **Agent**: `page-alerts`
- **Page**: Inactivity Alerts (`/alerts-config`)
- **Pack**: Waveshare (primary), also relevant for P4 if alerts enabled

## Responsibilities
- Inactivity alert configuration UI (timeout, max per day, recipients)
- Per-slot alert management (enable/disable, timeout minutes, max emails per day)
- Sensors reset timer toggle
- Alert test email functionality
- Frontend validation and backend persistence
- Alert email sending pipeline (async task)

## Files You Know
- **Page asset**: `chatgpt/src/webpages/assets/page_alerts_asset.h`
- **Alerts page renderer**: `chatgpt/src/webpages/renderers/alerts_page_renderer.h`
- **Config handler**: `chatgpt/src/webpages/handlers_unified/config_handler.h/.cpp`
- **Recipients handler**: `chatgpt/src/webpages/handlers_unified/recipients_handler.cpp` (inactivity alert APIs)
- **Routes**: `chatgpt/src/webpages/routes/web_routes_config.cpp` (lines 52-56)
- **Runtime email**: `chatgpt/src/runtime_email.cpp` (inactivityAlertTask, lines 85-1166)
- **Proximity manager**: `chatgpt/src/sensors/proximity_manager.cpp` (timer reset logic)
- **Arduino bridge**: `chatgpt/src/arduino_bridge.cpp` (inactivity flag management)
- **ConfigManager**: `chatgpt/src/ConfigManager.h/.cpp` (inactivity_alerts JSON section)
- **Test manifest**: `chatgpt/src/webpages/handlers_unified/test_manifest_handler.cpp` (lines 164-179)
- **Page doc**: `PAGES/inactivity_alerts.md`

## When to Activate
- Bug: alert not sent when expected, or sent too often
- Bug: timeout value not persisting after save
- Bug: per-slot configuration not working
- Feature: add new alert field or modify existing
- UX: change layout, inputs, or validation on alerts page
- Backend: change API endpoints or JSON schema

## Agent Rules
1. **Frontend + Backend together** — always change asset HTML/JS AND handler C++ in tandem
2. **API endpoints**: `GET/POST /api/inactivity-alert-config`, `POST /api/inactivity-alert-test`
3. **JSON schema**: `inactivity_alerts` → `slots[]` → `{enabled, timeout_minutes, max_per_day, emails}`
4. **Atomic config writes**: `.tmp` → validate bytes>0 → remove original → rename
5. **Use `customAlert`/`customConfirm`** — never native `alert()`/`confirm()`
6. **No arrow functions** in JS (ESP32 browser compat)
7. **`d.success`** not `d.ok` for AJAX responses
8. **Update `PAGES/inactivity_alerts.md`** after any code change
9. **Feature flag**: `ENABLE_PAGE_ALERTS==1` — must be ON for this page
10. **Email test**: uses `inactivityAlertTask` async — verify via Web Monitor logs

## Output Format
```
## Task: <description>
## Files Changed: <list>
## API Changes: <endpoints modified>
## Tests: PASS/FAIL (details)
## Docs Updated: yes/no
## Revert: <instructions>
```
