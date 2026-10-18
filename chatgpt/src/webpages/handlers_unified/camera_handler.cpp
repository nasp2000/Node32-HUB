#include "camera_handler.h"
#include "feature_flags.h"
// Daily storage stats (defined in sd_tasks_impl.h)
void storNoteSdWrite(uint64_t bytes);
#if ENABLE_CAMERA==1
#include <ArduinoJson.h>
#include <time.h>
#include "../../camera_manager.h"
#include "../../camera_config.h"
#include "../../camera_resolution.h"
#include "../../camera_record.h"
#include "../../camera_motion.h"
#include "../../camera_p4_h264.h"  // Fase 1: codec/bitrate clamps (pure header)
#include "../../camera_p4_h264_enc.h"  // Fase 1: HW availability (stub off-P4)
#include "../../camera_p4_vision.h"  // Fase 5: vision clamps (pure header)
#include "../../camera_p4_vision_rt.h"  // Fase 5: burst/counters/model (stub off-P4)
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
#include "../../camera_p4_backend.h"  // resList/fpsMax adaptive caps
#endif
#include "../../psram_helpers.h"
#include "../../sdcard.h"
#include "feature_flags.h"

// Helpers
static String camTimestamp() {
    time_t now = time(nullptr);
    struct tm ti; localtime_r(&now, &ti);
    char buf[32]; sprintf(buf, "%04d%02d%02d_%02d%02d%02d", ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
    return String(buf);
}

// ── Status (extended with recording + SD info) ─────────────────────
void handleCameraStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    // Fase 5: 1536 B PSRAM (was 1024) — the P4 vision block adds ~11 keys.
    SpiRamJsonDocument doc(1536);
    doc["ready"] = cameraIsReady();
    doc["initInProgress"] = cameraInitInProgress();
    doc["model"] = cameraGetDetectedModel();
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    // P4 has two capture sources (MIPI/CSI native vs USB-UVC): the page shows
    // the Source selector whenever this flag is present, even before any
    // camera has started (model "Not detected"). Other boards omit it, so the
    // selector stays hidden there where the option does not apply.
    doc["p4src"] = true;
    // Adaptive capabilities: the page rebuilds Resolution/FPS from these
    // instead of offering one camera's typical values. MIPI OV5647 runs a
    // single compiled-in sensor mode; USB publishes the negotiated geometry
    // (empty until the first successful start this boot).
    // Attached-camera presence for the Source selector: refreshed here (while
    // idle only, throttled inside) so the page lists MIPI/USB cameras BEFORE
    // any Start — the model field alone only exists after a successful begin.
    cameraP4DetectSources();
    doc["detMipi"] = cameraP4MipiPresent();
    doc["detUsb"] = cameraP4UsbPresent();
    if (cameraP4UsbPresent()) {
        char usbId[12];
        cameraFormatUsbId(cameraP4UsbVid(), cameraP4UsbPid(), usbId, sizeof(usbId));
        doc["usbId"] = usbId;
    } else {
        doc["usbId"] = "";
    }
    if (cameraP4IsUsb()) {
        uint16_t aw = 0, ah = 0;
        cameraP4ActualDims(aw, ah);
        if (aw > 0 && ah > 0) {
            doc["resList"] = String(aw) + "x" + String(ah);
        } else {
            doc["resList"] = "";
        }
    } else {
        String list = cameraP4CsiResList();
        doc["resList"] = list.length() ? list : F("800x800");
    }
    uint16_t aw = 0, ah = 0;
    cameraP4ActualDims(aw, ah);
    doc["width"] = aw;
    doc["height"] = ah;
    doc["fpsMax"] = cameraP4FpsMax();
    // Fase 4 (Tarefa 5): per-path ceiling — the page caps the fps slider at
    // fpsMaxH264 when the H.264 codec is selected, fpsMax (MJPEG) otherwise.
    doc["fpsMaxH264"] = cameraP4FpsMaxH264();
    // Fase 1 (Tarefa 5): Codec block visibility (HW encoder present).
    doc["h264Avail"] = p4H264HwAvailable() ? true : false;
    // Last H.264 open failure step (Tarefa 5 follow-up): lets a device tell
    // "res rejected" from "hw_new/alloc/open failed" without a serial console.
    doc["h264Diag"] = p4H264BeginDiag();
    // Capture-pipeline stall flag (CAM-62): 1 while no frame arrived for
    // P4_CAP_STALL_MS. Lets a snapshot 503 be diagnosed remotely instead of
    // guessing whether the sensor, the encoder or the HTTP path failed.
    doc["capStall"] = cameraP4CaptureStalledNow() ? 1 : 0;
#endif
    doc["maxFrameSize"] = cameraDetectedMaxFrameSize();
    doc["streamFps"] = cameraStreamFps();
    doc["streamFrames"] = cameraStreamFramesSentTotal();
#if ENABLE_SDCARD==1
    doc["photoPend"] = cameraPhotoPending();
    {
        uint8_t const* lp = nullptr;
        size_t ll = 0;
        doc["photoRam"] = cameraPhotoLastJson(&lp, &ll) ? true : false;
    }
#endif
#if ENABLE_SDCARD==1
    // PSRAM shock-absorber health: bytes currently ringed + lifetime drops.
    // A growing `psramBuf` with `psramDrop` > 0 means the card cannot keep up
    // (or is dead) — capture keeps its own pace regardless.
    {
        uint32_t ringUsed = 0, ringDrop = 0;
        cameraRecordRingStats(&ringUsed, &ringDrop);
        doc["psramBuf"] = ringUsed;
        doc["psramDrop"] = ringDrop;
    }
#endif
    extern int cameraStreamClients();
    doc["streamClients"] = cameraStreamClients();
    doc["freeHeap"] = (uint32_t)ESP.getFreeHeap();
    doc["minHeap"] = (uint32_t)ESP.getMinFreeHeap();
    doc["maxAlloc"] = (uint32_t)ESP.getMaxAllocHeap();
#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4 != 1
    doc["boardTemp"] = temperatureRead();
#endif
    doc["recording"] = cameraRecordActive();
    if (cameraRecordActive()) {
        doc["recElapsed"] = cameraRecordElapsedSecs();
        doc["recFrames"] = cameraRecordFrameCount();
        doc["recPath"] = cameraRecordPath();
        doc["recCodec"] = cameraRecordIsH264() ? 1 : 0;  // Fase 1: .264 vs AVI
        // Fase 2: H.264 container (0=raw `.264`, 1=H.264-in-AVI).
        doc["recContainer"] = cameraRecordContainer();
    }
#if ENABLE_SDCARD==1
    if (sdcardShouldUseSD()) {
        uint64_t total = sdcardTotalBytes();
        uint64_t used = sdcardUsedBytes();
        if (total > 0) {
            doc["sdTotal"] = (uint32_t)(total >> 20);
            doc["sdUsed"] = (uint32_t)(used >> 20);
            doc["sdFree"] = (uint32_t)((total - used) >> 20);
        }
    }
#endif
    doc["streamCache"] = cameraStreamIsActive();
    doc["motion"] = cameraMotionActive();
    doc["motionScore"] = cameraMotionScore();
    doc["motionEvents"] = cameraMotionEventCount();
    doc["timelapseActive"] = cameraTimelapseActive();
    if (cameraTimelapseActive()) {
        doc["timelapseNextInSecs"] = cameraTimelapseNextInSecs();
        doc["timelapseLast"] = cameraTimelapseLastPath();
    }
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    // Fase 5 (Tarefa 5): vision foundation block (P4-only; flat keys keep the
    // doc small — nested `motion{}` precedent not repeated here).
    {
        CameraConfig vcfg = cameraGetConfig();
        doc["visMode"] = vcfg.vision_mode;
        doc["visThresh"] = vcfg.vision_thresh;
        doc["visModel"] = p4VisionModelPresent() ? true : false;
        if (p4VisionModelPresent()) doc["visModelSize"] = p4VisionModelSize();
        doc["visOk"] = p4VisionOk();
        doc["visNok"] = p4VisionNok();
        doc["visLast"] = p4VisionLast();
        doc["visFpsX10"] = p4VisionInferFpsX10();
        doc["visPpa"] = p4VisionPpaReady() ? true : false;
        doc["visBurstLeft"] = p4VisionBurstLeft();
    }
#endif
    // NOTE: a zero-heap slot here (stack buffer referenced past handler
    // return) is a use-after-free: beginResponse(ptr) builds AsyncProgmem-
    // Response which never copies. So serialize into a normal String — the
    // Async layer copies it into its own response object (safe).
    String body;
    body.reserve((size_t)doc.memoryUsage() * 2 + 64);  // avoid realloc churn on the 5 s poll
    serializeJson(doc, body);
    res.sendJson(body);
}

// ── Start camera ─────────────────────────────────────────────────
void handleCameraStartPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (cameraIsReady()) { cameraStart(); res.sendJson(F("{\"success\":true,\"message\":\"Camera active\"}")); return; }
    if (cameraInitInProgress()) { res.sendJson(F("{\"success\":true,\"message\":\"Camera startup in progress\"}")); return; }
    if (!cameraBeginAsync(0)) { res.sendJson(F("{\"success\":false,\"message\":\"Failed to schedule camera startup\"}")); return; }
    res.sendJson(F("{\"success\":true,\"message\":\"Camera startup requested\"}"));
}

// ── Stop camera ──────────────────────────────────────────────────
void handleCameraStopPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    cameraStop();
    extern void cameraStreamResetClients();
    cameraStreamResetClients();
    res.sendJson(F("{\"success\":true,\"message\":\"Camera stopped\"}"));
}

// ── Snapshot JPEG from stream cache (PSRAM, zero heap) ────────────
void handleCameraSnapshotGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!cameraIsReady()) { res.send(503, F("text/plain"), F("Camera not ready")); return; }
    // A direct capture inside a motion grayscale window would return a 96x96
    // grayscale thumbnail instead of JPEG. Wait briefly for sensor restore
    // (no-op when streaming: motion sampling is paused there).
    for (uint8_t w = 0; w < 20 && cameraMotionSamplingActive(); w++) delay(20);

    // Single pump: serve from the loop pre-capture when live, one direct
    // capture when idle, refuse inside motion grayscale windows.
    // Never auto-starts the stream (that leaked s_streamActive forever).
    uint8_t* img = nullptr;
    size_t len = 0;
    if (!cameraGrabFileFrame(&img, &len, 600)) {
        if (cameraMotionSamplingActive()) { res.send(503, F("text/plain"), F("Sampling, retry")); return; }
        res.send(503, F("text/plain"), F("Capture failed")); return;
    }
    res.code = 200; res.contentType = F("image/jpeg"); res.isBinary = true;
    res.binaryData = img; res.binaryLen = len;
    res.freeFn = [](const uint8_t* p) { free(const_cast<uint8_t*>(p)); };
}

// ── Get full camera config as JSON ────────────────────────────────
void handleCameraConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    CameraConfig cfg = cameraGetConfig();
    // Fase 5: 1536 B PSRAM (was 1024) — vision keys + motion object headroom.
    SpiRamJsonDocument doc(1536);
    doc["framesize"]   = (int)cfg.framesize;
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
    doc["lampAvail"]      = cameraLampAvailable();
    doc["source"]         = cfg.source;
    // Fase 1 (Tarefa 5, CAM_P4 only): codec select + bitrate; h264Avail tells
    // the Rec tab whether the Codec block applies (P4 MIPI + HW encoder).
    // Fase 2: container select (raw `.264` vs H.264-in-AVI).
    doc["rec_codec"]      = cfg.rec_codec;
    doc["h264_bitrate"]   = cfg.h264_bitrate;
    doc["h264_container"] = cfg.h264_container;
    // Fase 5 (Tarefa 5, CAM_P4 only): vision foundation.
    doc["vision_mode"]    = cfg.vision_mode;
    doc["vision_thresh"]  = cfg.vision_thresh;
    doc["vision_burst_n"] = cfg.vision_burst_n;
    doc["vision_burst_label"] = cfg.vision_burst_label;
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    doc["h264Avail"] = p4H264HwAvailable() ? true : false;
#else
    doc["h264Avail"] = false;
#endif
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    doc["p4_res_w"] = cfg.p4_res_w;
    doc["p4_res_h"] = cfg.p4_res_h;
    if (cfg.source == CAM_P4_SRC_MIPI) {
        uint16_t enumW = 0, enumH = 0, rw = 0, rh = 0;
        cameraSensorFrameSizeDims((uint8_t)cfg.framesize, enumW, enumH);
        // Match the backend's safe fallback instead of presenting an enum
        // size (e.g. 1280x720) that is not an OV5647 sensor mode.
        cameraResolveP4OutputDims(cfg.p4_res_w, cfg.p4_res_h, enumW, enumH, &rw, &rh);
        doc["p4_resolution"] = String(rw) + "x" + String(rh);
    }
#endif
    // Feature-availability flag (mirrors the lampAvail pattern): the page
    // hides the "Email alert" motion action when SMTP is compiled out.
    doc["motionEmailAvail"] = (ENABLE_SMTP==1);
    JsonObject m = doc.createNestedObject("motion");
    m["enabled"]          = cfg.motion.enabled;
    m["sensitivity"]      = cfg.motion.sensitivity;
    m["confirm_frames"]   = cfg.motion.confirm_frames;
    m["stop_frames"]      = cfg.motion.stop_frames;
    m["post_record_secs"] = cfg.motion.post_record_secs;
    m["action"]           = cfg.motion.action;
    m["cooldown_secs"]    = cfg.motion.cooldown_secs;
    m["lamp_auto"]        = cfg.motion.lamp_auto;
    m["total_events"]     = cfg.motion.total_events;
    m["roi_x"] = cfg.motion.roi_x; m["roi_y"] = cfg.motion.roi_y;
    m["roi_w"] = cfg.motion.roi_w; m["roi_h"] = cfg.motion.roi_h;
    // NOTE: a zero-heap slot here (stack buffer referenced past handler
    // return) is a use-after-free: beginResponse(ptr) builds AsyncProgmem-
    // Response which never copies. So serialize into a normal String — the
    // Async layer copies it into its own response object (safe).
    String body; serializeJson(doc, body);
    res.sendJson(body);
}

// ── Update camera config from form-encoded or JSON body ───────────
void handleCameraConfigPost(HttpRequest& req, HttpResponse& res) {
    const bool isJson = req.contentType.indexOf("json") >= 0 || req.body.startsWith("{");
    StaticJsonDocument<768> jdoc;
    if (isJson) {
        if (deserializeJson(jdoc, req.body) != DeserializationError::Ok) {
            res.sendJson(F("{\"success\":false,\"message\":\"Invalid JSON body\"}"));
            return;
        }
    }
    auto jsonScalar = [&](JsonVariant v) -> String {
        if (v.isNull()) return String();
        if (v.is<bool>()) return v.as<bool>() ? String("1") : String("0");
        if (v.is<signed char>()) return String((int)v.as<signed char>());
        if (v.is<unsigned char>()) return String((int)v.as<unsigned char>());
        if (v.is<int>()) return String(v.as<int>());
        if (v.is<unsigned int>()) return String((long)v.as<unsigned int>());
        if (v.is<long>()) return String((long)v.as<long>());
        if (v.is<unsigned long>()) return String((long)v.as<unsigned long>());
        if (v.is<float>()) return String(v.as<float>());
        if (v.is<double>()) return String((float)v.as<double>());
        const char* s = v.as<const char*>();
        return s ? String(s) : String();
    };
    auto jsonVal = [&](const char* k) -> String {
        String fk(k);
        JsonVariant v;
        if (fk.startsWith("motion_")) {
            JsonObject mo = jdoc["motion"].as<JsonObject>();
            if (mo.isNull()) return String();
            v = mo[fk.c_str() + 7];
        } else {
            v = jdoc[k];
        }
        return jsonScalar(v);
    };
    auto val = [&](const char* k) -> String {
        if (isJson) return jsonVal(k);
        String p = String(k) + "=";
        int s = 0, e = req.body.indexOf('&', s);
        while (e >= 0 || s < (int)req.body.length()) {
            String pair = (e >= 0) ? req.body.substring(s, e) : req.body.substring(s);
            if (pair.startsWith(p)) {
                String v = pair.substring(p.length());
                v.replace('+', ' ');  // form-encoded space
                return v;
            }
            if (e < 0) break;
            s = e + 1; e = req.body.indexOf('&', s);
        }
        return String();
    };
    auto truthy = [&](const char* k) -> bool { String v = val(k); v.toLowerCase(); return v == "1" || v == "true" || v == "on"; };
    auto intval = [&](const char* k, int def) -> int { String v = val(k); return v.length() ? v.toInt() : def; };
    // Direct-API hardening: the page constrains ranges, but raw POSTs can send
    // anything (e.g. quality=200 → uint8 wrap → sensor garbage). Clamp centrally.
    auto clampI = [&](int v, int lo, int hi) -> int { return v < lo ? lo : (v > hi ? hi : v); };

    CameraConfig cfg = cameraGetConfig();
    bool changed = false;

    if (val("framesize").length())  { int fs = intval("framesize", cfg.framesize); if (fs >= 0 && fs <= (int)FRAMESIZE_5MP) { cfg.framesize = (framesize_t)fs; changed = true; } }
    if (val("quality").length())    { cfg.quality = (uint8_t)clampI(intval("quality", cfg.quality), 0, 63); changed = true; }
    if (val("fps").length())        { cfg.fps = (uint8_t)clampI(intval("fps", cfg.fps), 1, 30); changed = true; }
    if (val("brightness").length()) { cfg.brightness = (int8_t)clampI(intval("brightness", cfg.brightness), -2, 2); changed = true; }
    if (val("contrast").length())   { cfg.contrast = (int8_t)clampI(intval("contrast", cfg.contrast), -2, 2); changed = true; }
    if (val("saturation").length()) { cfg.saturation = (int8_t)clampI(intval("saturation", cfg.saturation), -2, 2); changed = true; }
    if (val("dashcam_mins").length())   { cfg.dashcam_mins = (uint8_t)clampI(intval("dashcam_mins", cfg.dashcam_mins), 0, 120); changed = true; }
    if (val("timelapse_secs").length()) { cfg.timelapse_secs = (uint16_t)clampI(intval("timelapse_secs", cfg.timelapse_secs), 0, 3600); changed = true; }
    if (val("auto_mode").length())      { cfg.auto_mode = (uint8_t)clampI(intval("auto_mode", cfg.auto_mode), 0, 4); changed = true; }
    if (val("lamp").length())           { cfg.lamp = (uint8_t)clampI(intval("lamp", cfg.lamp), 0, 15); changed = true; }
    if (val("source").length())         { cfg.source = (uint8_t)clampI(intval("source", cfg.source), 0, 1); changed = true; }
    // Fase 1 (Tarefa 5): codec select (MJPEG default; H.264 opt-in, P4-only)
    // + bitrate 1000–8000 kbps. Off-P4 any H.264 ask folds to MJPEG.
    if (val("rec_codec").length()) {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
        cfg.rec_codec = cameraH264CodecClamp(intval("rec_codec", cfg.rec_codec));
#else
        cfg.rec_codec = CAM_REC_CODEC_MJPEG;
#endif
        changed = true;
    }
    if (val("h264_bitrate").length()) {
        cfg.h264_bitrate = (uint16_t)cameraH264BitrateClampKbps(intval("h264_bitrate", cfg.h264_bitrate));
        changed = true;
    }
    // Fase 2: container (raw `.264` default; AVI opt-in, P4-only).
    if (val("h264_container").length()) {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
        cfg.h264_container = cameraH264ContainerClamp(intval("h264_container", cfg.h264_container));
#else
        cfg.h264_container = CAM_H264_CONT_RAW;
#endif
        changed = true;
    }
    // Fase 5 (Tarefa 5): vision foundation (mode P4-only, rest clamped all boards).
    if (val("vision_mode").length()) {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
        cfg.vision_mode = cameraVisionModeClamp(intval("vision_mode", cfg.vision_mode));
#else
        cfg.vision_mode = CAM_VISION_OFF;
#endif
        changed = true;
    }
    if (val("vision_thresh").length()) {
        cfg.vision_thresh = cameraVisionThreshClamp(intval("vision_thresh", cfg.vision_thresh));
        changed = true;
    }
    if (val("vision_burst_n").length()) {
        cfg.vision_burst_n = cameraVisionBurstClamp(intval("vision_burst_n", cfg.vision_burst_n));
        changed = true;
    }
    if (val("vision_burst_label").length()) {
        String bl = val("vision_burst_label");
        if (bl.length() > CAM_VISION_LABEL_MAX) bl = bl.substring(0, CAM_VISION_LABEL_MAX);
        snprintf(cfg.vision_burst_label, sizeof(cfg.vision_burst_label), "%s", bl.c_str());
        changed = true;
    }
    if (val("resolution").length()) {
        // P4 MIPI "WxH" selector: validate against the sensor mode table
        // before persisting (ACK enforcement happens at apply/boot time).
        String rv = val("resolution");
        uint16_t rw = 0, rh = 0;
        bool ok = cameraParseResolution(rv.c_str(), &rw, &rh) == 1;
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
        if (ok && cameraP4ValidCsiMode(rw, rh)) {
            cfg.p4_res_w = rw; cfg.p4_res_h = rh; changed = true;
        }
#else
        (void)ok;
#endif
    }
    if (val("hmirror").length())    { cfg.hmirror = truthy("hmirror"); changed = true; }
    if (val("vflip").length())      { cfg.vflip = truthy("vflip"); changed = true; }
    if (val("awb").length())        { cfg.awb = truthy("awb"); changed = true; }
    if (val("aec").length())        { cfg.aec = truthy("aec"); changed = true; }
    if (val("agc").length())        { cfg.agc = truthy("agc"); changed = true; }
    if (val("lenc").length())       { cfg.lenc = truthy("lenc"); changed = true; }
    if (val("gainceiling").length()) { cfg.gainceiling = (uint8_t)clampI(intval("gainceiling", cfg.gainceiling), 0, 6); changed = true; }
    if (val("special_effect").length()) { cfg.special_effect = (int8_t)clampI(intval("special_effect", cfg.special_effect), 0, 6); changed = true; }
    if (val("wb_mode").length())        { cfg.wb_mode = (uint8_t)clampI(intval("wb_mode", cfg.wb_mode), 0, 4); changed = true; }
    if (val("aec_value").length())      { cfg.aec_value = (uint16_t)clampI(intval("aec_value", cfg.aec_value), 0, 1200); changed = true; }
    if (val("aec2").length())           { cfg.aec2 = truthy("aec2"); changed = true; }
    if (val("agc_gain").length())       { cfg.agc_gain = (uint8_t)clampI(intval("agc_gain", cfg.agc_gain), 0, 30); changed = true; }
    if (val("ae_level").length())       { cfg.ae_level = (int8_t)clampI(intval("ae_level", cfg.ae_level), -2, 2); changed = true; }

    // Motion sub-fields
    auto& mc = cfg.motion;
    if (val("motion_enabled").length())  { mc.enabled = truthy("motion_enabled"); changed = true; }
    if (val("motion_sensitivity").length()) { mc.sensitivity = (uint8_t)clampI(intval("motion_sensitivity", mc.sensitivity), 1, 100); changed = true; }
    if (val("motion_confirm_frames").length()) { mc.confirm_frames = (uint8_t)clampI(intval("motion_confirm_frames", mc.confirm_frames), 1, 20); changed = true; }
    if (val("motion_stop_frames").length())    { mc.stop_frames = (uint8_t)clampI(intval("motion_stop_frames", mc.stop_frames), 2, 30); changed = true; }
    if (val("motion_post_record_secs").length()) { mc.post_record_secs = (uint8_t)clampI(intval("motion_post_record_secs", mc.post_record_secs), 0, 30); changed = true; }
#if ENABLE_SMTP==1
    if (val("motion_action").length())          { mc.action = (uint8_t)clampI(intval("motion_action", mc.action), 0, 2); changed = true; }
#else
    // No email in this build: "Email alert" (2) is not offered by the UI.
    // Clamp a legacy stored 2 down to Snap photo (1) so old configs stay valid.
    if (val("motion_action").length())          { mc.action = (uint8_t)clampI(intval("motion_action", mc.action), 0, 1); changed = true; }
#endif
    if (val("motion_cooldown_secs").length())   { mc.cooldown_secs = (uint16_t)clampI(intval("motion_cooldown_secs", mc.cooldown_secs), 0, 600); changed = true; }
    if (val("motion_lamp_auto").length())       { mc.lamp_auto = truthy("motion_lamp_auto"); changed = true; }
    if (val("motion_roi_x").length()) { mc.roi_x = (uint8_t)clampI(intval("motion_roi_x", mc.roi_x), 0, 39); changed = true; }
    if (val("motion_roi_y").length()) { mc.roi_y = (uint8_t)clampI(intval("motion_roi_y", mc.roi_y), 0, 29); changed = true; }
    if (val("motion_roi_w").length()) { mc.roi_w = (uint8_t)clampI(intval("motion_roi_w", mc.roi_w), 0, 40); changed = true; }
    if (val("motion_roi_h").length()) { mc.roi_h = (uint8_t)clampI(intval("motion_roi_h", mc.roi_h), 0, 30); changed = true; }

    // Explicit engine-off wins over auto_mode: cameraArmAutoMode() would
    // otherwise resurrect the engine on EVERY apply (e.g. unchecking motion
    // with Boot mode=Motion silently re-enables it). Demoting auto_mode keeps
    // UI and boot behavior consistent: what you see is what boots.
    // Precedence: an explicitly (re)selected non-zero auto_mode in the SAME
    // request wins over an engine-off (mode choice is the stronger signal).
    bool autoTouched = val("auto_mode").length() > 0;
    if ((!autoTouched || cfg.auto_mode == 0)) {
        if (val("motion_enabled").length() && !mc.enabled && cfg.auto_mode == 3) cfg.auto_mode = 0;
        if (val("dashcam_mins").length() && cfg.dashcam_mins == 0 && cfg.auto_mode == 2) cfg.auto_mode = 0;
        if (val("timelapse_secs").length() && cfg.timelapse_secs == 0 && cfg.auto_mode == 4) cfg.auto_mode = 0;
    }

    if (!changed) { res.sendJson(F("{\"success\":true,\"message\":\"No changes\"}")); return; }

    bool ok = cameraApplyConfig(cfg);
    if (!ok) {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
        // Say why when it is the known case: 2592x1944 is offered as
        // stills-only and the sensor has no register profile for it yet, so
        // the mode switch is refused by design. Anything else keeps the
        // generic message.
        if (cfg.source == 0 && cfg.p4_res_w == 2592 && cfg.p4_res_h == 1944) {
            res.sendJson(F("{\"success\":false,\"message\":\"5MP stills need a sensor profile - not available yet\"}"));
            return;
        }
#endif
        res.sendJson(F("{\"success\":false,\"message\":\"Config apply failed\"}"));
        return;
    }

    // cameraApplyConfig() already persisted the (framesize/fps-clamped) config
    // and propagated motion config; no second save here (re-saving `cfg` would
    // overwrite the clamped framesize/fps with the raw requested values).

    res.sendJson(F("{\"success\":true}"));
}

// ── Last photo from RAM ────────────────────────────────────────────
// Serves the most recent capture straight from PSRAM: viewable even while
// its SD save is still queued, or when the card is dead. 404 when empty.
void handleCameraLastPhotoGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    uint8_t const* data = nullptr;
    size_t len = 0;
    if (!cameraPhotoLastJson(&data, &len) || !data || !len) {
        res.send(404, F("text/plain"), F("No photo in RAM"));
        return;
    }
    // Serve a copy: the RAM original may be replaced (freed) by the next
    // photo while a slow client is still downloading. The response frees it.
    uint8_t* copy = (uint8_t*)(psramFound() ? ps_malloc(len) : malloc(len));
    if (!copy) { res.send(503, F("text/plain"), F("Out of memory")); return; }
    memcpy(copy, data, len);
    res.code = 200; res.contentType = F("image/jpeg"); res.isBinary = true;
    res.binaryData = copy; res.binaryLen = len;
    res.freeFn = [](const uint8_t* p) { free(const_cast<uint8_t*>(p)); };
}

// ── Capture photo to SD ──────────────────────────────────────────
void handleCameraPhotoPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!cameraIsReady()) { res.sendJson(F("{\"success\":false,\"message\":\"Camera not ready\"}")); return; }
    // Same grayscale-window hazard as snapshot (see above): wait for restore.
    for (uint8_t w = 0; w < 20 && cameraMotionSamplingActive(); w++) delay(20);
    // Single pump (see snapshot): cache when live, direct when idle.
    uint8_t* img = nullptr;
    size_t imgLen = 0;
    if (!cameraGrabFileFrame(&img, &imgLen, 600)) {
        res.sendJson(cameraMotionSamplingActive()
            ? F("{\"success\":false,\"message\":\"Sampling, retry\"}")
            : F("{\"success\":false,\"message\":\"Capture failed\"}"));
        return;
    }

#if ENABLE_SDCARD==1
    String ts = camTimestamp();
    String folder = String(CAM_REC_ROOT) + "/PHOTOS";
    String path = folder + "/" + ts + ".jpg";
    // Fast path: the grab already returned an owned copy, so answer now and
    // let the tick saver do the SD write (the old code blocked the HTTP task
    // on the SD lock + write). Falls back to synchronous below when busy.
    cameraPhotoRemember(img, imgLen);
    if (cameraPhotoSubmit(img, imgLen, path)) {
        char buf[300]; snprintf(buf, sizeof(buf), "{\"success\":true,\"filename\":\"%s\"}", path.c_str());
        res.sendJson(String(buf));
        return;
    }
    // Slots busy: synchronous legacy write (slow but never loses the photo).
    // Serialise SD access with the loop-task scan and other SD consumers.
    extern bool acquireSDAccess(const char* owner, uint32_t timeoutMs);
    extern void releaseSDAccess();
    // Short SD lock: never stall the HTTP task for 8s when the SD is busy
    // with rotation/scans — fail fast so the web UI stays responsive.
    bool sdLocked = acquireSDAccess("camera-photo", 2000UL);
    if (!sdLocked) {
        free(img);
        res.sendJson(F("{\"success\":false,\"message\":\"SD busy\"}"));
        return;
    }
    if (!SD_MMC.exists(CAM_REC_ROOT))  SD_MMC.mkdir(CAM_REC_ROOT);
    if (!SD_MMC.exists(folder.c_str())) SD_MMC.mkdir(folder.c_str());
    File f = SD_MMC.open(path.c_str(), FILE_WRITE);
    if (!f) {
        releaseSDAccess();
        free(img);
        res.sendJson(F("{\"success\":false,\"message\":\"SD write failed\"}"));
        return;
    }
    f.write(img, imgLen);
    f.close();
    storNoteSdWrite(imgLen);
    releaseSDAccess();
    free(img);
    char buf[300]; snprintf(buf, sizeof(buf), "{\"success\":true,\"filename\":\"%s\"}", path.c_str());
    res.sendJson(String(buf));
#else
    free(img);
    res.sendJson(F("{\"success\":false,\"message\":\"SD card not available\"}"));
#endif
}

// ── Recording status ─────────────────────────────────────────────
void handleCameraRecordingStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    SpiRamJsonDocument doc(256);
    doc["active"] = cameraRecordActive();
    if (cameraRecordActive()) {
        doc["elapsed"] = cameraRecordElapsedSecs();
        doc["frames"]  = cameraRecordFrameCount();
        doc["path"]    = cameraRecordPath();
    }
    // NOTE: a zero-heap slot here (stack buffer referenced past handler
    // return) is a use-after-free: beginResponse(ptr) builds AsyncProgmem-
    // Response which never copies. So serialize into a normal String — the
    // Async layer copies it into its own response object (safe).
    String body; serializeJson(doc, body);
    res.sendJson(body);
}

// ── Start recording ──────────────────────────────────────────────
void handleCameraRecordingStartPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!cameraIsReady()) { res.sendJson(F("{\"success\":false,\"message\":\"Camera not ready\"}")); return; }
    if (cameraRecordActive()) { res.sendJson(F("{\"success\":false,\"message\":\"Already recording\"}")); return; }
    CameraConfig cfg = cameraGetConfig();
    // Clamp fps to the stable budget for this resolution so the AVI header
    // duration matches what the sensor can actually deliver.
    uint8_t recFps = cameraClampFps(cfg.framesize, cfg.fps);
    uint16_t w, h;
    cameraGetOutputDimensions(w, h);
    CameraRecordStartResult cause = CAM_REC_OK;
    // Fase 4 (Tarefa 5): H.264 runs its own ceiling (sensor rate, up to 30 —
    // POST already clamped cfg.fps to 1–30), not the MJPEG stable budget.
    // cameraClampFps() stays for the MJPEG/AVI path below.
    if (cfg.rec_codec == CAM_REC_CODEC_H264) {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
        if (cfg.source != CAM_P4_SRC_MIPI) {
            res.sendJson(F("{\"success\":false,\"message\":\"H.264 needs the P4 MIPI source\"}"));
            return;
        }
        uint8_t h264Fps = cameraH264FpsClamp(cfg.fps ? cfg.fps : 12);
        if (!cameraRecordStartH264(h264Fps, cfg.framesize, w, h, cfg.h264_bitrate,
                                   cfg.h264_container, "M", &cause)) {
            String body = F("{\"success\":false,\"message\":\"");
            body += cameraRecordStartCauseMessage(cause);
            body += F("\"}");
            res.sendJson(body);
            return;
        }
        res.sendJson(F("{\"success\":true,\"message\":\"H.264 recording started\"}"));
        return;
#else
        res.sendJson(F("{\"success\":false,\"message\":\"H.264 needs the P4 MIPI source\"}"));
        return;
#endif
    }
    if (!cameraRecordStartEx(recFps, cfg.framesize, w, h, "M", &cause)) {
        String body = F("{\"success\":false,\"message\":\"");
        body += cameraRecordStartCauseMessage(cause);
        body += F("\"}");
        res.sendJson(body);
        return;
    }
    res.sendJson(F("{\"success\":true,\"message\":\"Recording started\"}"));
}

// ── Stop recording ───────────────────────────────────────────────
void handleCameraRecordingStopPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!cameraRecordActive()) { res.sendJson(F("{\"success\":false,\"message\":\"Not recording\"}")); return; }
    uint32_t frames = cameraRecordFrameCount();
    uint32_t elapsed = cameraRecordElapsedSecs();
    bool ok = cameraRecordFinish();
    if (!ok) { res.sendJson(F("{\"success\":false,\"message\":\"Stop failed\"}")); return; }
    // Path AFTER finish: finish renames to ..._<DUR>s_<TAG>.avi.
    String path = cameraRecordPath();
    char buf[300]; snprintf(buf, sizeof(buf), "{\"success\":true,\"frames\":%u,\"elapsed\":%u,\"path\":\"%s\"}", frames, elapsed, path.c_str());
    res.sendJson(String(buf));
}

// ── Recording file list (newest first, capped for <4KB responses) ──
void handleCameraFilesGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_SDCARD==1
    extern bool acquireSDAccess(const char* owner, uint32_t timeoutMs);
    extern void releaseSDAccess();
    if (!sdcardShouldUseSD()) { res.sendJson(F("{\"files\":[]}")); return; }
    if (!acquireSDAccess("camera-files", 2000UL)) {
        res.sendJson(F("{\"success\":false,\"message\":\"SD busy\"}")); return;
    }
    // Collect day folders, walk each for video files (cap total).
    String out = F("{\"files\":[");
    bool first = true;
    int count = 0;
    const int kMaxFiles = 40;
    File root = SD_MMC.open(CAM_REC_ROOT);
    if (root && root.isDirectory()) {
        // Two passes: gather day dir names, then files. Day dirs sort
        // lexicographically = chronological (YYYYMMDD).
        File day = root.openNextFile();
        while (day && count < kMaxFiles) {
            String dayName = String(day.name());
            bool isDir = day.isDirectory();
            day.close();
            if (isDir) {
                String dayPath = String(CAM_REC_ROOT) + "/" + dayName;
                // Strip any leading dir prefix the FS layer may include.
                if (dayPath.startsWith("//")) dayPath = dayPath.substring(1);
                File df = SD_MMC.open(dayPath.c_str());
                if (df && df.isDirectory()) {
                    File f = df.openNextFile();
                    while (f && count < kMaxFiles) {
                        String fn = String(f.name());
                        size_t fsz = f.size();
                        bool isF = !f.isDirectory();
                        f.close();
                        // Both containers: MJPEG .avi and raw H.264 .264.
                        if (isF && (fn.endsWith(".avi") || fn.endsWith(".264"))) {
                            String full = dayPath + "/" + fn;
                            // f.name() may already carry the path; normalise.
                            if (fn.startsWith("/")) full = fn;
                            if (!first) out += ',';
                            first = false;
                            out += F("{\"n\":\"");
                            out += full;
                            out += F("\",\"s\":");
                            out += String((unsigned long)fsz);
                            out += '}';
                            count++;
                        }
                        if (count < kMaxFiles) f = df.openNextFile();
                    }
                    df.close();
                }
            }
            if (count < kMaxFiles) day = root.openNextFile();
        }
        root.close();
    }
    releaseSDAccess();
    out += F("]}");
    res.sendJson(out);
#else
    res.sendJson(F("{\"files\":[]}"));
#endif
}

// ── Delete one recording file ────────────────────────────────────
static bool camPathGuard(const String& p) {
    if (!p.startsWith(String(CAM_REC_ROOT) + "/")) return false;
    if (p.indexOf("..") >= 0) return false;
    if (p.length() > 128) return false;
    return true;
}

void handleCameraDeletePost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_SDCARD==1
    extern bool acquireSDAccess(const char* owner, uint32_t timeoutMs);
    extern void releaseSDAccess();
    String file;
    {
        String p = String("file=");
        int s = req.body.indexOf(p);
        if (s >= 0) {
            int e = req.body.indexOf('&', s);
            file = (e >= 0) ? req.body.substring(s + p.length(), e)
                            : req.body.substring(s + p.length());
        }
    }
    // Minimal URL-decoding for %2F (query strings arrive encoded via JS).
    file.replace("%2F", "/"); file.replace("%2f", "/");
    if (!camPathGuard(file)) { res.sendJson(F("{\"success\":false,\"message\":\"Bad path\"}")); return; }
    if (cameraRecordActive() && file == cameraRecordPath()) {
        res.sendJson(F("{\"success\":false,\"message\":\"Recording in progress\"}")); return;
    }
    if (!acquireSDAccess("camera-delete", 2000UL)) {
        res.sendJson(F("{\"success\":false,\"message\":\"SD busy\"}")); return;
    }
    bool ok = SD_MMC.exists(file.c_str()) && SD_MMC.remove(file.c_str());
    releaseSDAccess();
    res.sendJson(ok ? F("{\"success\":true}") : F("{\"success\":false,\"message\":\"Delete failed\"}"));
#else
    (void)req;
    res.sendJson(F("{\"success\":false,\"message\":\"SD card not available\"}"));
#endif
}

// ── Reset motion event counter ───────────────────────────────
void handleCameraMotionResetPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    cameraMotionResetCount();
    res.sendJson(F("{\"success\":true}"));
}

// ── Fase 5 (Tarefa 5): Vision dataset burst ──────────────────────────────
// Starts a one-shot collection (form: n=N&label=TAG, or JSON {n,label}).
// Needs the camera ready; the vision task captures at ~1/s into
// /CAM/VISION/<label>/. P4-only (stubs refuse elsewhere).
void handleCameraVisionBurstPost(HttpRequest& req, HttpResponse& res) {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    if (!cameraIsReady()) { res.sendJson(F("{\"success\":false,\"message\":\"Camera not ready\"}")); return; }
    // Reuse the config-POST body readers (form or JSON, same contract).
    const bool isJson = req.contentType.indexOf("json") >= 0 || req.body.startsWith("{");
    int n = 0;
    String label;
    if (isJson) {
        StaticJsonDocument<192> jdoc;
        if (deserializeJson(jdoc, req.body) != DeserializationError::Ok) {
            res.sendJson(F("{\"success\":false,\"message\":\"Invalid JSON body\"}"));
            return;
        }
        n = jdoc["n"] | 0;
        const char* l = jdoc["label"] | "";
        label = l ? String(l) : String();
    } else {
        String p = String("n=");
        int s = req.body.indexOf(p);
        if (s >= 0) {
            int e = req.body.indexOf('&', s);
            n = ((e >= 0) ? req.body.substring(s + p.length(), e)
                          : req.body.substring(s + p.length())).toInt();
        }
        p = String("label=");
        s = req.body.indexOf(p);
        if (s >= 0) {
            int e = req.body.indexOf('&', s);
            label = (e >= 0) ? req.body.substring(s + p.length(), e)
                             : req.body.substring(s + p.length());
            label.replace('+', ' ');
        }
    }
    n = cameraVisionBurstClamp(n);
    if (n == 0 || label.length() == 0) {
        res.sendJson(F("{\"success\":false,\"message\":\"Need n=1..50 and label\"}"));
        return;
    }
    char tmp[CAM_VISION_LABEL_MAX + 1];
    snprintf(tmp, sizeof(tmp), "%s", label.c_str());
    if (!p4VisionBurstStart((uint8_t)n, tmp)) {
        res.sendJson(F("{\"success\":false,\"message\":\"Burst busy or invalid\"}"));
        return;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"success\":true,\"n\":%d,\"label\":\"%s\"}", n, tmp);
    res.sendJson(String(buf));
#else
    (void)req;
    res.sendJson(F("{\"success\":false,\"message\":\"Vision needs the P4\"}"));
#endif
}

// ── Fase 5: reset vision OK/NOK counters ───────────────────────────────────
void handleCameraVisionResetPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    p4VisionResetCounters();
    res.sendJson(F("{\"success\":true}"));
}

// ── Motion diff visualisation (40x30 JPEG, red = changed pixels) ───
// Calibration aid for sensitivity/ROI. Works even with motion disabled
// (flat gray = no diff yet). Torn-frame races with the loop task are
// harmless for a visualisation.
void handleCameraMotionDiffGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!cameraIsReady()) { res.send(503, F("text/plain"), F("Camera not ready")); return; }
    const size_t kDiffBuf = 8192;
    uint8_t* img = (uint8_t*)ps_malloc(kDiffBuf);
    if (!img) { res.send(500, F("text/plain"), F("OOM")); return; }
    size_t n = cameraMotionBuildDiffJpeg(img, kDiffBuf);
    if (n == 0) { free(img); res.send(500, F("text/plain"), F("Diff failed")); return; }
    res.code = 200; res.contentType = F("image/jpeg"); res.isBinary = true;
    res.binaryData = img; res.binaryLen = n;
    res.freeFn = [](const uint8_t* p) { free(const_cast<uint8_t*>(p)); };
}

#endif
