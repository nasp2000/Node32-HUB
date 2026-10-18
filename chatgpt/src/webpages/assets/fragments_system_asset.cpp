#include "fragments_system_asset.h"

const char SYSTEM_SECTION[] PROGMEM = R"HTML(
<div style='margin-top:28px'>
<h2 style='margin:0 0 12px 0;font-size:18px;color:#60a5fa' data-i18n='navSistemaTitle'>⚙️ Sistema</h2>
<div style='display:grid;gap:5px'>
)HTML";

const char SYSTEM_SECTION_SETTINGS_BUTTON[] PROGMEM = R"HTML(
<a href='/printer' class='btn' data-i18n='navSystemSettings'>⚙️ System Settings</a>
)HTML";

const char SYSTEM_SECTION_OTA_BUTTON[] PROGMEM = R"HTML(
<a href='/ota' class='btn' data-i18n='navOtaBtn'>&#128260; Firmware Update (OTA)</a>
)HTML";

const char SYSTEM_SECTION_ACCESS_BUTTON[] PROGMEM = R"HTML(
<a href='/access' class='btn' data-i18n='navAccessBtn'>&#128274; Acessos</a>
)HTML";
const char SYSTEM_SECTION_API_REF_BUTTON[] PROGMEM = R"HTML(
<a href='/api-reference' class='btn' data-i18n='navApiRef'>🔌 API Reference</a>
)HTML";

const char SYSTEM_SECTION_CLOSE[] PROGMEM = R"HTML(
</div>
</div>
)HTML";