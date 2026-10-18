#include "sd_diagnostics.h"
#include "feature_flags.h"

#if ENABLE_SDCARD==1

namespace {

void appendEscapedPreviewField(String& json, const char* key, const String& value) {
    json += '"';
    json += key;
    json += F("\":\"");
    json += value;
    json += '"';
}

}  // namespace

String getSdBuffersDiagnosticsJson() {
    SdBufferDiagnosticsSnapshot snapshot;
    captureSdBufferDiagnosticsSnapshot(snapshot);

    String json = F("{");
    json += String(F("\"rs232_active_size\":")) + String(snapshot.rs232ActiveSize);
    json += String(F(",\"rs232_active_capacity\":")) + String(snapshot.rs232ActiveCapacity);
    json += String(F(",\"lpt_active_size\":")) + String(snapshot.lptActiveSize);
    json += String(F(",\"lpt_active_capacity\":")) + String(snapshot.lptActiveCapacity);
    json += ',';
    appendEscapedPreviewField(json, "rs232_preview", snapshot.rs232Preview);
    json += ',';
    appendEscapedPreviewField(json, "lpt_preview", snapshot.lptPreview);
    json += '}';
    return json;
}
#endif // ENABLE_SDCARD