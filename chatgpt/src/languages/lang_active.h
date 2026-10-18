#pragma once
#include <Arduino.h>
#include "lang_pt.h"
#include "lang_en.h"
#include "lang_pt_ui.h"
#include "lang_en_ui.h"

// Pointer to the active language pack. Initialized to English at startup.
// Call setupLanguage() after loading config to apply the stored preference.
extern const LangStrings* activeLang;

// Read a PROGMEM string from the active language pack into an Arduino String.
inline String L(const char* p) { return String(FPSTR(p)); }

// PROGMEM pointer to the active UI translation JSON, served at /api/lang.
extern const char* activeLangUiJson;

// Active UI language code served alongside the JSON payload.
extern const char* activeLangCode;

// Set activeLang based on the system_language value in ConfigManager.
// Must be called after configManager loads its settings (syncGlobalVariables).
void setupLanguage();
