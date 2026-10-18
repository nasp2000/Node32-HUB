#pragma once

#include <Arduino.h>
#include "../assets/page_api_reference_asset.h"
#include "page_render_common.h"
#include "../../grbl_sender.h"

// Streams the API reference page using the standard page shell.
// Injects a <script> block with compile-time feature flags before the page body
// so the JS can hide sections that are not compiled in.
inline size_t streamApiReferencePage(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x94\x8C Refer\xC3\xAAncia de API");

    // Emit feature flags as a JS object so the page JS can hide unused sections
    out.print(F("<script>var _F={"));
    out.print(F("modbus:"));      out.print((int)ENABLE_MODBUS);
    out.print(F(",proximity:"));   out.print((int)ENABLE_PROXIMITY);
    out.print(F(",sdcard:"));      out.print((int)ENABLE_SDCARD);
    out.print(F(",mq135:"));       out.print((int)ENABLE_MQ135);
    out.print(F(",ota:"));         out.print((int)ENABLE_OTA);
    out.print(F(",sniffer:"));     out.print((int)ENABLE_SNIFFER);
    out.print(F(",db25:"));        out.print((int)ENABLE_DB25);
#ifdef ENABLE_DEBUG_API
    out.print(F(",debug_api:"));   out.print((int)ENABLE_DEBUG_API);
#else
    out.print(F(",debug_api:0"));
#endif
    out.print(F(",ethernet:"));    out.print((int)ENABLE_ETHERNET);
    out.print(F(",rs232:"));       out.print((int)ENABLE_RS232);
    out.print(F(",telnet:"));      out.print((int)ENABLE_TELNET);
    out.print(F(",sensors:"));     out.print((int)(ENABLE_AHT20_BMP280 || ENABLE_MQ135));
    out.print(F(",do_out:"));      out.print((int)(BOARD_HAS_TCA9554_DO));
    out.print(F(",grbl:"));        out.print((int)ENABLE_GRBL);
    out.print(F(",camera:"));      out.print((int)ENABLE_CAMERA);
    out.print(F(",uda1334a:"));    out.print((int)ENABLE_UDA1334A);
    out.print(F("};</script>"));
    n += 290; // approximate size of the script block

    out.print(FPSTR(API_REFERENCE_PAGE));
    n += strlen_P(API_REFERENCE_PAGE);
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
