// camera_config.cpp — Load and save camera configuration from LittleFS.

#include "camera_config.h"

#include "feature_flags.h"
#if ENABLE_CAMERA==1

#include <LittleFS.h>
#include "arduinojson_fix.h"
#include <ArduinoJson.h>
#include "littlefs_manager.h"
#include "direct_log.h"
#include "psram_helpers.h"
#include "feature_flags.h"

bool cameraConfigLoad(CameraConfig& cfg) {
    cfg = cameraConfigDefaults();
    if (!littlefsBegin()) return false;

    File f = LittleFS.open(CAM_CFG_PATH, "r");
    if (!f) return false;

    // PSRAM doc sized for the full schema: 22 top-level keys + 14-key
    // "motion" object. ArduinoJson v6 needs ~16 B/pair + key strings
    // (~950 B total); the old 896 B overflowed silently once lamp/auto_mode/
    // cooldown/lamp_auto/total_events were added — save wrote truncated JSON,
    // load failed parse, every boot fell back to defaults (CAM-39).
    SpiRamJsonDocument doc(1536);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err != DeserializationError::Ok) return false;

    // Fallback values match cameraConfigDefaults() so a missing field keeps the default.
    cfg.framesize   = (framesize_t)(doc["framesize"]   | (uint8_t)cfg.framesize);
    cfg.quality     = doc["quality"]    | cfg.quality;
    cfg.fps         = doc["fps"]        | cfg.fps;
    cfg.brightness  = doc["brightness"] | cfg.brightness;
    cfg.contrast    = doc["contrast"]   | cfg.contrast;
    cfg.saturation  = doc["saturation"] | cfg.saturation;
    cfg.hmirror     = doc["hmirror"]    | cfg.hmirror;
    cfg.vflip       = doc["vflip"]      | cfg.vflip;
    cfg.gainceiling = doc["gainceiling"]| cfg.gainceiling;
    cfg.awb         = doc["awb"]        | cfg.awb;
    cfg.aec         = doc["aec"]        | cfg.aec;
    cfg.agc         = doc["agc"]        | cfg.agc;
    cfg.lenc        = doc["lenc"]       | cfg.lenc;
    cfg.special_effect = doc["special_effect"] | cfg.special_effect;
    cfg.wb_mode        = doc["wb_mode"]        | cfg.wb_mode;
    cfg.aec_value      = doc["aec_value"]      | cfg.aec_value;
    cfg.aec2           = doc["aec2"]           | cfg.aec2;
    cfg.agc_gain       = doc["agc_gain"]       | cfg.agc_gain;
    cfg.ae_level       = doc["ae_level"]       | cfg.ae_level;
    cfg.dashcam_mins   = doc["dashcam_mins"]   | cfg.dashcam_mins;
    cfg.timelapse_secs = doc["timelapse_secs"] | cfg.timelapse_secs;
    cfg.auto_mode      = doc["auto_mode"]      | cfg.auto_mode;
    cfg.lamp           = doc["lamp"]           | cfg.lamp;
    cfg.source         = doc["source"]         | cfg.source;
    cfg.p4_res_w       = doc["p4_res_w"]       | cfg.p4_res_w;
    cfg.p4_res_h       = doc["p4_res_h"]       | cfg.p4_res_h;
    cfg.rec_codec      = doc["rec_codec"]      | cfg.rec_codec;
    cfg.h264_bitrate   = doc["h264_bitrate"]   | cfg.h264_bitrate;
    cfg.h264_container = doc["h264_container"] | cfg.h264_container;
    cfg.vision_mode    = doc["vision_mode"]    | cfg.vision_mode;
    cfg.vision_thresh  = doc["vision_thresh"]  | cfg.vision_thresh;
    cfg.vision_burst_n = doc["vision_burst_n"] | cfg.vision_burst_n;
    {
        const char* bl = doc["vision_burst_label"] | "";
        strncpy(cfg.vision_burst_label, bl ? bl : "", sizeof(cfg.vision_burst_label) - 1);
        cfg.vision_burst_label[sizeof(cfg.vision_burst_label) - 1] = '\0';
    }
    // Motion config sub-fields.
    JsonObjectConst mo = doc["motion"];
    if (!mo.isNull()) {
        cfg.motion.enabled          = mo["enabled"]          | false;
        cfg.motion.sensitivity      = mo["sensitivity"]      | (uint8_t)20;
        cfg.motion.confirm_frames   = mo["confirm_frames"]   | (uint8_t)3;
        cfg.motion.stop_frames      = mo["stop_frames"]      | (uint8_t)10;
        cfg.motion.post_record_secs = mo["post_record_secs"] | (uint8_t)5;
        cfg.motion.action           = mo["action"]           | (uint8_t)MOTION_ACTION_RECORD_VIDEO;
        cfg.motion.roi_x            = mo["roi_x"]            | (uint8_t)0;
        cfg.motion.roi_y            = mo["roi_y"]            | (uint8_t)0;
        cfg.motion.roi_w            = mo["roi_w"]            | (uint8_t)0;
        cfg.motion.roi_h            = mo["roi_h"]            | (uint8_t)0;
        cfg.motion.cooldown_secs    = mo["cooldown_secs"]    | (uint16_t)5;
        cfg.motion.lamp_auto        = mo["lamp_auto"]        | false;
        cfg.motion.total_events     = mo["total_events"]     | (uint32_t)0;
    }
    return true;
}

bool cameraConfigSave(const CameraConfig& cfg) {
    if (!littlefsBegin()) return false;

    // Allocate JSON doc in PSRAM (see capacity note in Load above).
    SpiRamJsonDocument doc(1536);
    doc["framesize"]   = (uint8_t)cfg.framesize;
    doc["quality"]     = cfg.quality;
    doc["fps"]         = cfg.fps;
    doc["brightness"]  = cfg.brightness;
    doc["contrast"]    = cfg.contrast;
    doc["saturation"]  = cfg.saturation;
    doc["hmirror"]     = cfg.hmirror;
    doc["vflip"]       = cfg.vflip;
    doc["gainceiling"] = cfg.gainceiling;
    doc["awb"]         = cfg.awb;
    doc["aec"]         = cfg.aec;
    doc["agc"]         = cfg.agc;
    doc["lenc"]        = cfg.lenc;
    doc["special_effect"] = cfg.special_effect;
    doc["wb_mode"]        = cfg.wb_mode;
    doc["aec_value"]      = cfg.aec_value;
    doc["aec2"]           = cfg.aec2;
    doc["agc_gain"]       = cfg.agc_gain;
    doc["ae_level"]       = cfg.ae_level;
    doc["dashcam_mins"]   = cfg.dashcam_mins;
    doc["timelapse_secs"] = cfg.timelapse_secs;
    doc["auto_mode"]      = cfg.auto_mode;
    doc["lamp"]           = cfg.lamp;
    doc["source"]         = cfg.source;
    doc["p4_res_w"]       = cfg.p4_res_w;
    doc["p4_res_h"]       = cfg.p4_res_h;
    doc["rec_codec"]      = cfg.rec_codec;
    doc["h264_bitrate"]   = cfg.h264_bitrate;
    doc["h264_container"] = cfg.h264_container;
    doc["vision_mode"]    = cfg.vision_mode;
    doc["vision_thresh"]  = cfg.vision_thresh;
    doc["vision_burst_n"] = cfg.vision_burst_n;
    doc["vision_burst_label"] = cfg.vision_burst_label;
    // Nested motion object.
    JsonObject mo = doc.createNestedObject("motion");
    mo["enabled"]          = cfg.motion.enabled;
    mo["sensitivity"]      = cfg.motion.sensitivity;
    mo["confirm_frames"]   = cfg.motion.confirm_frames;
    mo["stop_frames"]      = cfg.motion.stop_frames;
    mo["post_record_secs"] = cfg.motion.post_record_secs;
    mo["action"]           = cfg.motion.action;
    mo["roi_x"]            = cfg.motion.roi_x;
    mo["roi_y"]            = cfg.motion.roi_y;
    mo["roi_w"]            = cfg.motion.roi_w;
    mo["roi_h"]            = cfg.motion.roi_h;
    mo["cooldown_secs"]    = cfg.motion.cooldown_secs;
    mo["lamp_auto"]        = cfg.motion.lamp_auto;
    mo["total_events"]     = cfg.motion.total_events;

    // Mandatory atomic write: write to .tmp first, then rename — never write directly to the final path.
    // LittleFS does not guarantee atomic writes; a power cut mid-write corrupts the file.
    // rename() is atomic per LittleFS spec.
    File f = LittleFS.open(CAM_CFG_TMP_PATH, "w");
    if (!f) return false;
    // Never persist a truncated doc: overflowed() means keys were silently
    // dropped (the CAM-39 boot-defaults bug). Fail loud, keep the old file.
    if (doc.overflowed()) {
        f.close();
        LittleFS.remove(CAM_CFG_TMP_PATH);
        directLogPrintln("[CAM] Config doc overflow — not saved");
        return false;
    }
    size_t bytes = serializeJson(doc, f);
    f.close();
    storNoteLfsWrite(bytes);

    if (bytes > 0) {
        LittleFS.remove(CAM_CFG_PATH);
        LittleFS.rename(CAM_CFG_TMP_PATH, CAM_CFG_PATH);
        return true;
    }
    LittleFS.remove(CAM_CFG_TMP_PATH);
    return false;
}

#endif // ENABLE_CAMERA==1
