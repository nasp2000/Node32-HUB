#include "sensor_charts_page_renderer.h"

#include "../../feature_flags.h"
#include "../assets/page_sensor_charts_asset.h"

size_t streamSensorChartsPageToPrint(Print& out) {
#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_RS232_CHARTS==1
    out.print(FPSTR(SENSOR_CHARTS_HTML));
    return strlen_P(SENSOR_CHARTS_HTML);
#else
    (void)out;
    return 0;
#endif
}

size_t sendSensorChartsPageResponse(AsyncWebServerRequest* request) {
#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_RS232_CHARTS==1
    // Stream directly from PROGMEM using beginResponse_P — avoids allocating a
    // ~97KB contiguous heap buffer which AsyncResponseStream would need.
    const size_t len = strlen_P(SENSOR_CHARTS_HTML);
    AsyncWebServerResponse* response = request->beginResponse_P(
        200, F("text/html; charset=utf-8"),
        (const uint8_t*)SENSOR_CHARTS_HTML, len
    );
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
    return len;
#else
    request->send(404, "text/plain", "Not available");
    return 0;
#endif
}
