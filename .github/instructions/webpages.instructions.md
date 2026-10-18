---
applyTo: "chatgpt/src/webpages/**"
---

# Web Pages Instructions

## Rules (all mandatory)

- **Shell**: Assets never contain `<!DOCTYPE>`, `<html>`, `<head>`, `<body>`, `<link href='/core.css'>`, or `<script src='/auto.js'>` — injected by `streamPageShellStart()`/`streamAutoJsFooter()`.
- **Save buttons**: Success → `customAlert(d.message || window._t('alertSaved','✅ Configuration saved!'))` + `showSavedCard()`. Error → `customAlert('❌ Error: ' + message)`. Never `alert()`, `showMsg()`, or `d.ok` (use `d.success`).
- **Confirm dialogs**: Always `customConfirm()` from `/auto.js`. Never native `confirm()`.
- **WiFi ↔ Ethernet parity**: Handler logic is unified in `handlers_unified/` (shared). Route registration remains separate: add the endpoint in both `routes/web_routes_*.cpp` (WiFi) and `ethernet_bootstrap_routes.h` + `ethernet_bootstrap_handlers.h` (Ethernet).
- **No GPIO in UI**: Never show pin numbers, wiring text, or hardware connections in page assets.
- **`/api/configs` frozen**: New pages create dedicated endpoints (<4KB). Never add to `/api/configs`.
- **Unified config**: Load `GET /api/config?section=<...>`, Save `POST /api/config` with `section=`.
- **Page file persistence**: One transactional LittleFS file per page. Never split fields across files.
- **Font stack**: `font-family` must start with `system-ui` for cross-platform: `system-ui,-apple-system,'Segoe UI',Roboto,'Noto Sans','Ubuntu','Cantarell','Helvetica Neue',sans-serif`.
- **Home button**: Every page has `<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome'>&#127968; Home</a></div>` as first child of `.wrap` (EN fallback; PT `btnHome` comes from the packs).
- **Section containers**: `border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px`.
- **i18n**: `data-i18n='key'` for text, `data-i18n-html='key'` for innerHTML.
- **No arrow functions**: Use `function(x){}` for ESP32 browser compat.

## 4-layer page split

| Layer | Location | Purpose |
|-------|----------|---------|
| Asset | `assets/page_<name>_asset.cpp/.h` | PROGMEM HTML/CSS/JS blob |
| Renderer | `renderers/<name>_page_renderer.cpp/.h` | `streamPageToPrint(Print& out)` |
| Handler | `handlers/<name>_handler.cpp/.h` | HTTP/API, JSON responses |
| Route | `routes/web_routes_<area>.cpp/.h` | URL registration on server |

`webpage_impl.h` = thin coordinator only. Never add inline HTML or handlers there.

## Asset pattern
```cpp
const char PAGE_NAME[] PROGMEM = R"rawliteral(...)rawliteral";
```
No DOCTYPE/html/head/body. Use `R"rawliteral(...)rawliteral"` delimiter.

## Renderer pattern
```cpp
size_t streamNamePage(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "Title");
    out.print(FPSTR(PAGE_NAME)); n += strlen_P(PAGE_NAME);
    out.print(F("<script src='/config-manager.js'></script>")); n += 37;
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
```

## Handler pattern (unified: one implementation serves WiFi + Ethernet)
```cpp
void handleXxxGet(HttpRequest& req, HttpResponse& res) {
    // registered once via registerApiRoute("GET", "/api/xxx", handleXxxGet)
    // - WiFi route + Ethernet dispatch are generated from the registry
    res.sendJson(String(F("{\"success\":true,\"message\":\"")) + L(activeLang->apiXxxDone) + F("\"}"));
}
```
First line: `context.ensureBasicAuth(request)` (via the registry wrapper). Response: `{"success":true|false,"message":"..."}` (message from `L(activeLang->...)`, never hardcoded).

## Route pattern
Every HTML route: `ensureBasicAuth` → `allowHtmlClient` → `beginResponseStream` → render → send → log. Feature flags use `#if ENABLE_X==1` with `(void)context;` in `#else`.

## JavaScript save pattern
```javascript
fetch('/api/xxx', {method:'POST', body:fd})
  .then(function(r){return r.json()})
  .then(function(d){
    if(d.success){customAlert(d.message || 'Configuration saved!');showSavedCard();}
    else customAlert('Error: '+(d.message||'?'));
  }).catch(function(e){customAlert('❌ '+e);});
```

