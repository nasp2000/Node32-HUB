#include "radio_schedule.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include <vector>
#include "commands.h"
#include "ConfigManager.h"
#include "littlefs_manager.h"
#include "psram_helpers.h"
#include "radio_audio_runtime.h"
#include "radio_schedule_core.h"

// ============================================================
// Weekly radio schedule (alarm / sleep-style windows)
//
// Ownership rules:
//  - Only the audio this schedule STARTED is owned by it. A manual play/stop
//    (web UI / deferred actions) releases ownership so the schedule never
//    hijacks or kills a manual session.
//  - A slot only fires once per window occurrence (tracked by the occurrence's
//    anchor day + start minute). Releasing ownership mid-window does NOT re-fire.
//  - Overnight windows (end <= start) span two days: START on the day in the
//    mask, TAIL on the following day.
//  - Persisted in /config/radio_schedule.json (atomic .tmp write) so a reboot
//    during an active window keeps ownership and the runtime's own resume
//    mechanism restores playback.
// ============================================================

namespace {

struct RadioSlot {
    String id;
    uint32_t days = 0; // bit0=Sunday .. bit6=Saturday
    uint8_t sh = 0;
    uint8_t sm = 0;
    uint8_t eh = 0;
    uint8_t em = 0;
    String type = "url"; // url | folder | playlist
    String src;
    String name;
    uint8_t vol = 20;
    bool enabled = true;
};

const char* kSchedulePath = "/config/radio_schedule.json";
const size_t kMaxSlots = 24;
const size_t kMaxSrcLen = 220;
const size_t kMaxNameLen = 48;
const uint32_t kCheckIntervalMs = 5000; // loop cadence

std::vector<RadioSlot> s_slots;
int s_loaded = 0; // 0=not loaded, 1=ok, -1=failed
uint32_t s_lastCheckMs = 0;
// The web handler (async_tcp, Core 0) flags a pending save; the loop task
// (Core 1) performs the actual LittleFS write so it never races ConfigManager.
volatile bool s_savePending = false;

// Ownership
String s_ownerId;
String s_ownerMode;
String s_ownerSource;
uint8_t s_ownerVol = 0;

// Occurrence the schedule last fired for (anchor day + start minute).
// Stays set while the same window occurrence is active so we don't re-fire
// after the user takes manual control.
time_t s_firedAnchorDay = -1;
uint16_t s_firedAnchorMin = 0;
uint16_t s_lastAttemptMin = 0; // minute of last start attempt (retry throttle)

bool radioScheduleSave();
bool radioScheduleLoad();

uint8_t slotWindowState(const RadioSlot& s, uint8_t wday, uint16_t minute) {
    return schedWindowState(s.days, s.sh, s.sm, s.eh, s.em, wday, minute);
}

bool slotAnchorFor(const RadioSlot& s, uint8_t wday, uint16_t minute,
                   uint8_t phase, time_t nowDay, time_t& outAnchorDay, uint16_t& outAnchorMin) {
    if (phase == SCHED_WINDOW_TAIL) {
        outAnchorDay = nowDay - 1;
    } else {
        outAnchorDay = nowDay;
    }
    outAnchorMin = (uint16_t)s.sh * 60u + s.sm;
    return true;
}

// ---- Playback -------------------------------------------------------------

void schedStartPlayback(const RadioSlot& s, bool* okOut, String& statusOut) {
    String msg;
    bool ok = false;
    if (s.type == F("folder")) {
        ok = radioAudioPlayFolder(s.src, msg);
    } else if (s.type == F("playlist")) {
        ok = radioAudioLoadPlaylist(s.src.c_str(), msg);
        if (ok) msg = F("Scheduled playlist loaded");
    } else {
        ok = radioAudioPlayUrl(s.src, msg);
    }
    if (okOut) *okOut = ok;
    statusOut = msg;
}

void schedStartSlot(int idx, uint8_t wday, uint16_t minute, time_t nowDay, uint8_t phase) {
    RadioSlot& s = s_slots[idx];
    time_t anchorDay;
    uint16_t anchorMin;
    slotAnchorFor(s, wday, minute, phase, nowDay, anchorDay, anchorMin);

    s_ownerId = s.id;
    s_ownerMode = s.type;
    s_ownerSource = s.src;
    s_ownerVol = s.vol;
    s_firedAnchorDay = anchorDay;
    s_firedAnchorMin = anchorMin;
    s_lastAttemptMin = minute;

    radioAudioSetVolume(s.vol);

    bool ok;
    String status;
    schedStartPlayback(s, &ok, status);

    // Persist ownership (LittleFS from the loop task is safe here).
    radioScheduleSave();

    String label = s.name.length() ? s.name : s.src;
    if (ok) {
        sendVirtualMessage(String(F("Scheduled: ")) + label + String(F(" now playing")), INFO);
    } else {
        sendVirtualMessage(String(F("Scheduled start failed: ")) +
                               (status.length() ? status : String(F("unknown reason"))),
                           WARN, false, true);
    }
}

void schedStopOwner(const String& reason) {
    if (!s_ownerId.length()) return;
    String id = s_ownerId;
    s_ownerId = "";
    s_ownerMode = "";
    s_ownerSource = "";
    s_ownerVol = 0;
    s_firedAnchorDay = -1;
    s_firedAnchorMin = 0;
    radioAudioStop();
    radioScheduleSave();
    sendVirtualMessage(String(F("Scheduled stop (")) + reason + String(F(")")) + (id.length() ? String(F(" - ")) + id : String()), INFO);
}

// ---- Persistence ----------------------------------------------------------

bool radioScheduleSave() {
    if (!littlefsBegin()) return false;
    SpiRamJsonDocument doc(16384);
    JsonArray arr = doc.createNestedArray("slots");
    for (size_t i = 0; i < s_slots.size(); i++) {
        JsonObject o = arr.createNestedObject();
        o["id"] = s_slots[i].id;
        o["days"] = (uint32_t)s_slots[i].days;
        o["sh"] = s_slots[i].sh;
        o["sm"] = s_slots[i].sm;
        o["eh"] = s_slots[i].eh;
        o["em"] = s_slots[i].em;
        o["type"] = s_slots[i].type;
        o["src"] = s_slots[i].src;
        o["name"] = s_slots[i].name;
        o["vol"] = s_slots[i].vol;
        o["enabled"] = s_slots[i].enabled;
    }
    if (s_ownerId.length()) {
        JsonObject owner = doc.createNestedObject("owner");
        owner["id"] = s_ownerId;
        owner["mode"] = s_ownerMode;
        owner["source"] = s_ownerSource;
        owner["vol"] = s_ownerVol;
    }

    String tmpPath = String(kSchedulePath) + ".tmp";
    File f = LittleFS.open(tmpPath, "w");
    if (!f) return false;
    size_t bytes = serializeJson(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes == 0) {
        LittleFS.remove(tmpPath);
        return false;
    }
    LittleFS.remove(kSchedulePath);
    if (!LittleFS.rename(tmpPath, kSchedulePath)) {
        LittleFS.remove(tmpPath);
        return false;
    }
    return true;
}

bool radioScheduleLoad() {
    if (!littlefsBegin()) return false;
    File f = LittleFS.open(kSchedulePath, "r");
    if (!f) {
        s_loaded = 1; // no file yet — fine
        return true;
    }
    SpiRamJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err != DeserializationError::Ok) return false;

    s_slots.clear();
    JsonArray arr = doc["slots"].as<JsonArray>();
    size_t idx = 0;
    for (JsonObject o : arr) {
        if (idx >= kMaxSlots) break;
        RadioSlot s;
        s.id = o["id"] | "";
        s.days = o["days"] | 0u;
        s.sh = o["sh"] | 0u;
        s.sm = o["sm"] | 0u;
        s.eh = o["eh"] | 0u;
        s.em = o["em"] | 0u;
        s.type = o["type"] | "url";
        s.src = o["src"] | "";
        s.name = o["name"] | "";
        s.vol = o["vol"] | 20u;
        s.enabled = o["enabled"] | true;
        if (!s.id.length()) s.id = String(F("sc_")) + String(idx);
        if (s.days != 0 && s.src.length()) {
            s_slots.push_back(s);
        }
        idx++;
    }

    s_ownerId = doc["owner"]["id"] | "";
    if (s_ownerId.length()) {
        s_ownerMode = doc["owner"]["mode"] | "";
        s_ownerSource = doc["owner"]["source"] | "";
        s_ownerVol = (uint8_t)((doc["owner"]["vol"] | 0u) & 0xffu);
        // Never restore a stale fired marker — after reboot the loop refires the
        // window (runtime resume is the primary restorer of playback).
        s_firedAnchorDay = -1;
        s_firedAnchorMin = 0;
    }
    return true;
}

} // namespace

// ---- Public API -----------------------------------------------------------

void radioScheduleBegin() {
    if (s_loaded == 0) {
        s_loaded = radioScheduleLoad() ? 1 : -1;
    }
    if (s_loaded < 0) return;
    if (!s_ownerId.length()) return;

    time_t now = time(nullptr);
    if (now <= 1000) return; // clock not synced yet — decide later in the loop

    // Re-evaluate the owner slot's window after reboot:
    //  - in window: keep ownership, apply slot volume (runtime resumes audio).
    //  - out of window: the window ended while powered off -> cancel the runtime
    //    auto-resume and stop.
    bool ownerActive = false;
    for (size_t i = 0; i < s_slots.size(); i++) {
        if (s_slots[i].id == s_ownerId) {
            struct tm ti;
            localtime_r(&now, &ti);
            if (slotWindowState(s_slots[i], (uint8_t)ti.tm_wday,
                                (uint16_t)(ti.tm_hour * 60 + ti.tm_min)) != SCHED_WINDOW_NONE) {
                ownerActive = true;
            }
            break;
        }
    }
    if (ownerActive) {
        radioAudioSetVolume(s_ownerVol);
    } else {
        // End-of-window stop after an off period. radioAudioCancelResume() stops
        // the runtime from auto-restoring the last source at +60s.
        radioAudioCancelResume();
        radioAudioStop();
        s_ownerId = "";
        s_ownerMode = "";
        s_ownerSource = "";
        s_ownerVol = 0;
        radioScheduleSave();
        sendVirtualMessage(F("Scheduled window ended while powered off - radio stays off"), INFO);
    }
}

void radioScheduleLoop() {
    if (s_loaded == 0) {
        s_loaded = radioScheduleLoad() ? 1 : -1;
    }
    if (s_loaded < 0) return;

    // Deferred save from the web handler (never LittleFS in async_tcp).
    if (s_savePending) {
        s_savePending = false;
        if (!radioScheduleSave()) s_savePending = true; // retry next tick
    }

    time_t now = time(nullptr);
    if (now <= 1000) return; // clock not synced yet — wait (matches sleep timer)

    uint32_t ms = millis();
    if ((int32_t)(ms - s_lastCheckMs) < (int32_t)kCheckIntervalMs) return;
    s_lastCheckMs = ms;

    struct tm ti;
    localtime_r(&now, &ti);
    uint8_t wday = (uint8_t)ti.tm_wday;
    uint16_t minute = (uint16_t)(ti.tm_hour * 60 + ti.tm_min);
    time_t nowDay = now / 86400;

    // Find the active enabled slot (first in list order wins on overlap).
    int activeIdx = -1;
    uint8_t activePhase = SCHED_WINDOW_NONE;
    for (size_t i = 0; i < s_slots.size(); i++) {
        if (!s_slots[i].enabled) continue;
        uint8_t st = slotWindowState(s_slots[i], wday, minute);
        if (st != SCHED_WINDOW_NONE) {
            activeIdx = (int)i;
            activePhase = st;
            break;
        }
    }

    if (activeIdx < 0) {
        if (s_ownerId.length()) {
            schedStopOwner(F("window ended"));
        }
        return;
    }

    RadioSlot& s = s_slots[activeIdx];

    if (s_ownerId == s.id) {
        // Already owned: keep the slot's volume, and gently retry a failed start
        // (every 2 minutes) so transient network failures heal themselves.
        if (radioAudioIsActive()) {
            if (s_ownerVol != s.vol) {
                s_ownerVol = s.vol;
                radioAudioSetVolume(s.vol);
            }
        } else if ((int16_t)(minute - s_lastAttemptMin) >= 2 ||
                   (int16_t)(minute - s_lastAttemptMin) < 0) {
            s_lastAttemptMin = minute;
            bool ok;
            String status;
            schedStartPlayback(s, &ok, status);
            if (!ok) {
                sendVirtualMessage(String(F("Scheduled retry failed: ")) +
                                       (status.length() ? status : String(F("unknown reason"))),
                                   WARN, false, true);
            }
        }
        return;
    }

    if (s_ownerId.length()) {
        // A different slot owns playback (overlap) or the user's session is
        // guarded — never hijack.
        return;
    }

    // No schedule session active: fire unless this window occurrence already
    // fired (the user may have taken manual control after that).
    time_t anchorDay;
    uint16_t anchorMin;
    slotAnchorFor(s, wday, minute, activePhase, nowDay, anchorDay, anchorMin);
    if (s_firedAnchorDay == anchorDay && s_firedAnchorMin == anchorMin) {
        return; // already fired for this occurrence
    }

    schedStartSlot(activeIdx, wday, minute, nowDay, activePhase);
}

void radioScheduleClearOwner() {
    if (s_loaded < 0) return;
    if (!s_ownerId.length()) return;
    s_ownerId = "";
    s_ownerMode = "";
    s_ownerSource = "";
    s_ownerVol = 0;
    // Keep s_firedAnchorDay/Min so the schedule does not re-fire inside the
    // same window occurrence (user took manual control).
    radioScheduleSave();
}

String radioScheduleJson() {
    SpiRamJsonDocument doc(16384);
    doc["success"] = true;
    JsonArray arr = doc.createNestedArray("slots");
    for (size_t i = 0; i < s_slots.size(); i++) {
        JsonObject o = arr.createNestedObject();
        o["id"] = s_slots[i].id;
        o["days"] = (uint32_t)s_slots[i].days;
        o["sh"] = s_slots[i].sh;
        o["sm"] = s_slots[i].sm;
        o["eh"] = s_slots[i].eh;
        o["em"] = s_slots[i].em;
        o["type"] = s_slots[i].type;
        o["src"] = s_slots[i].src;
        o["name"] = s_slots[i].name;
        o["vol"] = s_slots[i].vol;
        o["enabled"] = s_slots[i].enabled;
    }
    JsonObject owner = doc.createNestedObject("owner");
    if (s_ownerId.length()) {
        owner["id"] = s_ownerId;
        owner["mode"] = s_ownerMode;
        owner["source"] = s_ownerSource;
        owner["vol"] = s_ownerVol;
    }

    time_t now = time(nullptr);
    JsonObject nowObj = doc.createNestedObject("now");
    if (now > 1000) {
        struct tm ti;
        localtime_r(&now, &ti);
        nowObj["wday"] = ti.tm_wday;
        char hm[6];
        snprintf(hm, sizeof(hm), "%02d:%02d", ti.tm_hour, ti.tm_min);
        nowObj["hm"] = hm;
        nowObj["ts"] = (long)now;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

bool radioScheduleSet(const String& slotsJson, String& messageOut) {
    SpiRamJsonDocument doc(16384);
    if (deserializeJson(doc, slotsJson) != DeserializationError::Ok ||
        !doc["slots"].is<JsonArray>()) {
        messageOut = F("Invalid JSON");
        return false;
    }

    std::vector<RadioSlot> next;
    JsonArray arr = doc["slots"].as<JsonArray>();
    size_t idx = 0;
    for (JsonObject o : arr) {
        if (idx >= kMaxSlots) break;
        RadioSlot s;
        s.id = o["id"] | "";
        if (!s.id.length()) s.id = String(F("sc_")) + String(idx);
        if (s.id.length() > 16) s.id = s.id.substring(0, 16);

        uint32_t days = o["days"] | 0u;
        if (days == 0 || days > 127) {
            messageOut = F("Pick at least one day");
            return false;
        }
        uint8_t sh = o["sh"] | 0u;
        uint8_t sm = o["sm"] | 0u;
        uint8_t eh = o["eh"] | 0u;
        uint8_t em = o["em"] | 0u;
        if (sh > 23 || eh > 23 || sm > 59 || em > 59) {
            messageOut = F("Invalid time");
            return false;
        }
        String type = o["type"] | "url";
        if (type != "url" && type != "folder" && type != "playlist") {
            messageOut = F("Invalid source type");
            return false;
        }
        String src = o["src"] | "";
        src.trim();
        if (!src.length()) {
            messageOut = F("Missing source");
            return false;
        }
        if (src.length() > kMaxSrcLen) src = src.substring(0, kMaxSrcLen);
        String name = o["name"] | "";
        if (name.length() > kMaxNameLen) name = name.substring(0, kMaxNameLen);
        int vol = o["vol"] | 20;
        if (vol < 0) vol = 0;
        if (vol > 100) vol = 100;
        bool enabled = o["enabled"] | true;

        s.days = days;
        s.sh = sh;
        s.sm = sm;
        s.eh = eh;
        s.em = em;
        s.type = type;
        s.src = src;
        s.name = name;
        s.vol = (uint8_t)vol;
        s.enabled = enabled;
        next.push_back(s);
        idx++;
    }

    // Drop ownership if the owner slot no longer exists in the new set.
    bool ownerKept = false;
    if (s_ownerId.length()) {
        for (size_t i = 0; i < next.size(); i++) {
            if (next[i].id == s_ownerId) {
                ownerKept = true;
                break;
            }
        }
        if (!ownerKept) {
            s_ownerId = "";
            s_ownerMode = "";
            s_ownerSource = "";
            s_ownerVol = 0;
            s_firedAnchorDay = -1;
            s_firedAnchorMin = 0;
        }
    }

    s_slots.swap(next);
    // Defer the LittleFS write to the loop task (radioScheduleLoop).
    s_savePending = true;
    messageOut = F("Schedule saved");
    return true;
}

#else // !PACK_RADIO

// Stubs so the unified audio handlers link on non-PACK_RADIO builds.
void radioScheduleBegin() {}
void radioScheduleLoop() {}
void radioScheduleClearOwner() {}
String radioScheduleJson() { return String(F("{\"success\":false,\"message\":\"PACK_RADIO only\"}")); }
bool radioScheduleSet(const String&, String& messageOut) { messageOut = F("PACK_RADIO only"); return false; }

#endif // PACK_RADIO

#endif // ENABLE_UDA1334A==1
