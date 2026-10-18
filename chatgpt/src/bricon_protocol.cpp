// bricon_protocol.cpp
// Bricon loft-module driver — SKELETON (wire driver pending hardware tap).
//
// Mirrors the Unikon engine structure (poll/calendar/pombal/arrivals/email)
// so the page, API and tests are real. The RS-232 command layer is a clean
// fill-in point (see BriconPending* markers): until the tap confirms the
// exact bytes, the engine NEVER transmits — it only listens and logs.

#include "bricon_protocol.h"

#include "feature_flags.h"
#if ENABLE_BRICON == 1

#include <LittleFS.h>
#include <ArduinoJson.h>
#include "littlefs_manager.h"
#include "direct_log.h"
#include "commands.h"  // sendVirtualMessage, LogLevel
#include "psram_helpers.h"
#include "email_message_builder.h"  // buildEmailSignature
#include "runtime_email.h"          // startUnikonEmailAsync (shared arrival-email slot)
#include "ConfigManager.h"
extern ConfigManager configManager;
#if ENABLE_GOOGLE_EMAIL == 1
#include "google_email.h"
#endif
#if ENABLE_UNIKON == 1
#include "unikon_protocol.h"  // mutual exclusion with the Rambox module
#endif
#if ENABLE_RS232 == 1
extern void applyRS232Config();
#endif

#define BRICON_CFG_PATH "/config/bricon.json"

extern HardwareSerial RS232;
extern volatile bool _rs232PortInitialized;
extern int rs232_baudrate;
extern int rs232_baudrate_index;
extern int rs232_data_bits;
extern String rs232_parity;
extern int rs232_stop_bits;
extern volatile bool _pendingRS232Apply;
#if ENABLE_UNIKON == 1
extern bool unikonIsPolling();
#endif

// ----- File-local state -----
static BriconConfig s_cfg = { false, 0, 9600, 8, "none", 1 };
static BriconStatus s_status = { false, true, 0, 0, 0, "none" };
static volatile bool s_configWritePending = false;
static bool s_pollingActive = false;
static uint32_t s_lastPollMs = 0;
static volatile bool s_pollPending = false;    // full poll (calendar + pombal + arrivals)
static volatile bool s_wPollPending = false;   // arrivals only
static volatile bool s_icPollPending = false;  // calendar only
static volatile bool s_xPollPending = false;   // pombal only
static uint32_t s_lastLogMs = 0;               // RX log throttle

// ----- Config persistence (atomic .tmp -> rename, same pattern as Unikon) -----
static void loadConfig() {
    s_cfg.enabled = false;
    s_cfg.pollIntervalSec = 0;
    s_cfg.baudrate = 9600;
    s_cfg.dataBits = 8;
    strlcpy(s_cfg.parity, "none", sizeof(s_cfg.parity));
    s_cfg.stopBits = 1;
    if (!littlefsBegin()) return;
    if (!LittleFS.exists(BRICON_CFG_PATH)) return;
    File f = LittleFS.open(BRICON_CFG_PATH, "r");
    if (!f) return;
    size_t fileSize = (size_t)f.size();
    if (fileSize == 0 || fileSize > 4096u) { f.close(); return; }
    SpiRamJsonDocument doc(2048u);
    if (doc.capacity() == 0) { f.close(); return; }
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();
    s_cfg.enabled         = doc["enabled"]  | false;
    s_cfg.pollIntervalSec = doc["interval"] | (uint16_t)0;
    s_cfg.baudrate        = doc["baud"]     | 9600;
    s_cfg.dataBits        = doc["data"]     | 8;
    const char* par       = doc["parity"]   | "none";
    strlcpy(s_cfg.parity, par, sizeof(s_cfg.parity));
    s_cfg.stopBits        = doc["stop"]     | 1;
}

static bool saveConfig() {
    if (!littlefsBegin()) return false;
    if (!LittleFS.exists("/config")) {
        if (!LittleFS.mkdir("/config")) return false;
    }
    SpiRamJsonDocument doc(2048u);
    if (doc.capacity() == 0) return false;
    doc["enabled"]  = s_cfg.enabled;
    doc["interval"] = s_cfg.pollIntervalSec;
    doc["baud"]     = s_cfg.baudrate;
    doc["parity"]   = s_cfg.parity;
    doc["data"]     = s_cfg.dataBits;
    doc["stop"]     = s_cfg.stopBits;
    String tmpPath = String(BRICON_CFG_PATH) + ".tmp";
    File f = LittleFS.open(tmpPath.c_str(), "w");
    if (!f) return false;
    size_t bytes = serializeJson(doc, f);
    f.close();
    if (bytes == 0) { LittleFS.remove(tmpPath.c_str()); return false; }
    LittleFS.remove(BRICON_CFG_PATH);
    if (!LittleFS.rename(tmpPath.c_str(), BRICON_CFG_PATH)) {
        LittleFS.remove(tmpPath.c_str());
        return false;
    }
    return true;
}

// ----- Module selection (mutual exclusion: rambox XOR bricon) -----
const char* briconActiveModuleName() {
#if ENABLE_UNIKON == 1
    UnikonConfig u = unikonGetConfig();
    if (s_cfg.enabled && !u.enabled) { strlcpy(s_status.activeModule, "bricon", sizeof(s_status.activeModule)); }
    else if (u.enabled && !s_cfg.enabled) { strlcpy(s_status.activeModule, "rambox", sizeof(s_status.activeModule)); }
    else if (s_cfg.enabled && u.enabled) {
        // Must never happen (setters enforce it); bricon wins loudly.
        strlcpy(s_status.activeModule, "bricon", sizeof(s_status.activeModule));
    } else { strlcpy(s_status.activeModule, "none", sizeof(s_status.activeModule)); }
#else
    strlcpy(s_status.activeModule, s_cfg.enabled ? "bricon" : "none", sizeof(s_status.activeModule));
#endif
    return s_status.activeModule;
}

bool briconIsActive() {
    if (!s_cfg.enabled) return false;
#if ENABLE_UNIKON == 1
    if (unikonGetConfig().enabled) return false;  // exclusion: rambox owns the UART
#endif
    return true;
}

void briconApplySerialProfile() {
#if ENABLE_RS232 == 1
    extern const int RS232_BAUDRATES[];
    extern const int RS232_BAUDRATES_COUNT;
    rs232_baudrate = s_cfg.baudrate;
    rs232_baudrate_index = 3;  // default 9600 slot
    for (int i = 0; i < RS232_BAUDRATES_COUNT; i++) {
        if (RS232_BAUDRATES[i] == s_cfg.baudrate) { rs232_baudrate_index = i; break; }
    }
    if (s_cfg.dataBits >= 5 && s_cfg.dataBits <= 8) rs232_data_bits = s_cfg.dataBits;
    {
        String p = String(s_cfg.parity);
        p.toLowerCase();
        if (p == "even" || p == "odd" || p == "none") rs232_parity = p;
    }
    if (s_cfg.stopBits == 1 || s_cfg.stopBits == 2) rs232_stop_bits = s_cfg.stopBits;
    _pendingRS232Apply = true;  // deferred to loop (avoids UART driver race)
    sendVirtualMessage(String(F("[BRICON] RS232 profile requested: ")) + String(s_cfg.baudrate)
                       + String(F(" baud")), WARN, false, true);
#endif
}

static void enforceExclusionOnEnable() {
#if ENABLE_UNIKON == 1
    UnikonConfig u = unikonGetConfig();
    if (u.enabled) {
        u.enabled = false;
        unikonSetConfig(u);
        sendVirtualMessage(F("[BRICON] Unikon disabled (exclusive module rule)"), WARN, false, true);
    }
#endif
}

void briconSetEnabled(bool on) {
    s_cfg.enabled = on;
    if (!saveConfig()) s_configWritePending = true;
    if (on) {
        enforceExclusionOnEnable();
        briconApplySerialProfile();
        s_pollingActive = s_cfg.pollIntervalSec > 0;
        s_status.connected = false;
        s_status.bytesRx = 0;
        s_status.framesSeen = 0;
        sendVirtualMessage(F("[BRICON] enabled — driver pending hardware tap (listen-only)"), WARN, false, true);
    } else {
        s_pollingActive = false;
        sendVirtualMessage(F("[BRICON] disabled"), WARN, false, true);
    }
}

BriconConfig briconGetConfig() { return s_cfg; }

BriconStatus briconGetStatus() {
    briconActiveModuleName();
    return s_status;
}

void briconSetConfig(const BriconConfig& cfg) {
    bool wasOn = s_cfg.enabled;
    s_cfg = cfg;
    if (s_cfg.baudrate <= 0) s_cfg.baudrate = 9600;
    if (s_cfg.dataBits < 5 || s_cfg.dataBits > 8) s_cfg.dataBits = 8;
    if (s_cfg.stopBits != 1 && s_cfg.stopBits != 2) s_cfg.stopBits = 1;
    if (!saveConfig()) s_configWritePending = true;
    if (s_cfg.enabled && !wasOn) {
        enforceExclusionOnEnable();
        briconApplySerialProfile();
    }
    s_pollingActive = s_cfg.enabled && s_cfg.pollIntervalSec > 0;
}

// ----- Bricon data storage (PSRAM, mirrors Unikon; caps from bricon_protocol.h) -----
static BriconArrival* s_arrivals = nullptr;
static BriconArrival* s_registrations = nullptr;
static uint16_t s_arrivalCount = 0;
static uint16_t s_registrationCount = 0;
static BriconContest* s_contests = nullptr;
static uint16_t s_contestCount = 0;

// Pending-email window: arrivals below s_pendingEmailStart were already dispatched.
static uint16_t s_pendingEmailStart = 0;
static unsigned long s_lastEmailAttemptMs = 0;
#define BRICON_EMAIL_RETRY_MS 60000UL

static inline bool briconStorageReady() {
    return s_arrivals != nullptr && s_registrations != nullptr && s_contests != nullptr;
}

static void briconAllocateStorage() {
    bool hasPsram = psramFound();
    if (!s_arrivals) {
        s_arrivals = (BriconArrival*)(hasPsram
            ? ps_calloc(BRICON_MAX_ARRIVALS, sizeof(BriconArrival))
            : calloc(BRICON_MAX_ARRIVALS, sizeof(BriconArrival)));
    }
    if (!s_registrations) {
        s_registrations = (BriconArrival*)(hasPsram
            ? ps_calloc(BRICON_MAX_ARRIVALS, sizeof(BriconArrival))
            : calloc(BRICON_MAX_ARRIVALS, sizeof(BriconArrival)));
    }
    if (!s_contests) {
        s_contests = (BriconContest*)(hasPsram
            ? ps_calloc(BRICON_MAX_CONTESTS, sizeof(BriconContest))
            : calloc(BRICON_MAX_CONTESTS, sizeof(BriconContest)));
    }
}

static int16_t findArrivalIndexByRing(const char* ring) {
    if (!briconStorageReady() || !ring) return -1;
    for (uint16_t i = 0; i < s_arrivalCount; i++) {
        if (strncmp(s_arrivals[i].ring, ring, sizeof(s_arrivals[i].ring)) == 0) return (int16_t)i;
    }
    return -1;
}

static bool hasValidTime(const BriconArrival& a) {
    return a.arrivalTime[0] != '\0' && memcmp(a.arrivalTime, "000000", 6) != 0;
}

// Single ingestion funnel: poll parser AND spontaneous-frame parser both land
// here. New rings are appended; known rings get time/date refreshed (race
// records keep precedence over training, mirroring Unikon).
bool briconIngestArrival(const BriconArrival& src) {
    if (!briconStorageReady()) return false;
    if (!hasValidTime(src)) return false;
    int16_t idx = findArrivalIndexByRing(src.ring);
    if (idx >= 0) {
        bool hadRaceTime = hasValidTime(s_arrivals[idx]) && !s_arrivals[idx].isTraining;
        if (hadRaceTime && src.isTraining) return true;  // stored, race kept
        s_arrivals[idx] = src;
        return true;
    }
    if (s_arrivalCount >= BRICON_MAX_ARRIVALS) return false;
    s_arrivals[s_arrivalCount++] = src;
    return true;
}

uint16_t briconGetArrivals(BriconArrival* buf, uint16_t maxCount) {
    if (!buf || !briconStorageReady()) return 0;
    uint16_t n = s_arrivalCount < maxCount ? s_arrivalCount : maxCount;
    memcpy(buf, s_arrivals, n * sizeof(BriconArrival));
    return n;
}

uint16_t briconGetRegistrations(BriconArrival* buf, uint16_t maxCount) {
    if (!buf || !briconStorageReady()) return 0;
    uint16_t n = s_registrationCount < maxCount ? s_registrationCount : maxCount;
    memcpy(buf, s_registrations, n * sizeof(BriconArrival));
    return n;
}

uint16_t briconGetContests(BriconContest* buf, uint16_t maxCount) {
    if (!buf || !briconStorageReady()) return 0;
    uint16_t n = s_contestCount < maxCount ? s_contestCount : maxCount;
    memcpy(buf, s_contests, n * sizeof(BriconContest));
    return n;
}

uint16_t briconGetArrivalCount() { return s_arrivalCount; }
uint16_t briconGetRegistrationCount() { return s_registrationCount; }

// ----- Email (mirrors Unikon: pending window, race/training sections, totals) -----
static void appendBriconArrivalSection(String& body, const __FlashStringHelper* title,
                                       bool trainingOnly, uint16_t fromIdx, uint16_t toIdx) {
    const uint8_t ringColWidth = 13;   // "BE-10-1100002" style rings
    const uint8_t chipColWidth = 11;
    const uint8_t timeColWidth = 8;
    const uint8_t dateColWidth = 8;    // DD/MM/YY
    const uint8_t raceColWidth = 9;
    const uint8_t catColWidth = 4;     // OLD/YEA/YOU
    const uint8_t speedColWidth = 6;   // m/min

    uint16_t maxNo = toIdx > 0 ? toIdx : 1;
    uint8_t noDigits = 1;
    while (maxNo >= 10) { maxNo /= 10; noDigits++; }
    uint8_t noColWidth = (uint8_t)(noDigits + 1);
    if (noColWidth < 2) noColWidth = 2;

    String sep;
    sep.reserve(110);
    sep += '+';
    for (uint8_t i = 0; i < (uint8_t)(noColWidth + 2); i++) sep += '-';
    sep += '+';
    for (uint8_t i = 0; i < (uint8_t)(ringColWidth + 2); i++) sep += '-';
    sep += '+';
    for (uint8_t i = 0; i < (uint8_t)(chipColWidth + 2); i++) sep += '-';
    sep += '+';
    for (uint8_t i = 0; i < (uint8_t)(timeColWidth + 2); i++) sep += '-';
    sep += '+';
    for (uint8_t i = 0; i < (uint8_t)(dateColWidth + 2); i++) sep += '-';
    sep += '+';
    for (uint8_t i = 0; i < (uint8_t)(raceColWidth + 2); i++) sep += '-';
    sep += '+';
    for (uint8_t i = 0; i < (uint8_t)(catColWidth + 2); i++) sep += '-';
    sep += '+';
    for (uint8_t i = 0; i < (uint8_t)(speedColWidth + 2); i++) sep += '-';
    sep += "+\n";

    char hdr[128];
    snprintf(hdr, sizeof(hdr), "| %-*s | %-13s | %-11s | %-8s | %-8s | %-9s | %-4s | %-6s |\n",
             (int)noColWidth, "No", "Anilha", "Chip", "Hora", "Data", "Prova", "Cat", "Vel");

    String section;
    section.reserve(200 + (uint16_t)(toIdx - fromIdx) * 90u);
    section += sep;
    section += String(hdr);
    section += sep;

    uint16_t rowCount = 0;
    for (uint16_t i = fromIdx; i < toIdx; i++) {
        if ((bool)s_arrivals[i].isTraining != trainingOnly) continue;
        if (!hasValidTime(s_arrivals[i])) continue;
        rowCount++;

        char timeStr[10] = "        ";
        {
            const char* t = s_arrivals[i].arrivalTime;
            snprintf(timeStr, sizeof(timeStr), "%c%c:%c%c:%c%c",
                     t[0], t[1], t[2], t[3], t[4], t[5]);
        }
        char dateStr[10] = "        ";
        {
            const char* d = s_arrivals[i].arrivalDate;
            size_t dl = strlen(d);
            if (dl >= 4) {
                if (dl >= 6) snprintf(dateStr, sizeof(dateStr), "%c%c/%c%c/%c%c",
                                      d[0], d[1], d[2], d[3], d[4], d[5]);
                else snprintf(dateStr, sizeof(dateStr), "%c%c/%c%c", d[0], d[1], d[2], d[3]);
            }
        }
        char ringStr[14];
        snprintf(ringStr, sizeof(ringStr), "%-13.13s", s_arrivals[i].ring);
        char chipStr[12];
        if (s_arrivals[i].hasChip) snprintf(chipStr, sizeof(chipStr), "%-11.11s", s_arrivals[i].chip);
        else { memset(chipStr, ' ', 11); chipStr[11] = '\0'; }
        char raceStr[10] = "         ";
        {
            size_t rlen = strlen(s_arrivals[i].race);
            if (rlen > 9) rlen = 9;
            memcpy(raceStr, s_arrivals[i].race, rlen);
        }
        char idxStr[10];
        snprintf(idxStr, sizeof(idxStr), "%u.", (unsigned)(i + 1));

        const char* catStr = (s_arrivals[i].category == 1) ? "OLD"
                           : (s_arrivals[i].category == 2) ? "YEA"
                           : (s_arrivals[i].category == 3) ? "YOU" : "";
        char speedStr[8] = "      ";
        if (s_arrivals[i].speed > 0) snprintf(speedStr, sizeof(speedStr), "%6u", (unsigned)s_arrivals[i].speed);

        char row[176];
        snprintf(row, sizeof(row), "| %-*s | %-13s | %-11s | %-8s | %-8s | %-9s | %-4s | %-6s |\n",
                 (int)noColWidth, idxStr, ringStr, chipStr, timeStr, dateStr, raceStr, catStr, speedStr);
        section += String(row);
    }

    if (rowCount > 0) {
        section += sep;
        body = String(title) + String(F("\n")) + section + body;
    }
}

static void briconEmailTick() {
#if ENABLE_SMTP == 1 || ENABLE_GOOGLE_EMAIL == 1
    if (!briconIsActive()) return;
    if (!briconStorageReady()) return;
    uint16_t pending = (s_arrivalCount > s_pendingEmailStart) ? (s_arrivalCount - s_pendingEmailStart) : 0;
    if (pending == 0) return;
    unsigned long now = millis();
    if (s_lastEmailAttemptMs != 0 && (now - s_lastEmailAttemptMs) < BRICON_EMAIL_RETRY_MS) return;

    uint16_t pendingRace = 0, pendingTraining = 0;
    for (uint16_t i = s_pendingEmailStart; i < s_arrivalCount; i++) {
        if (!hasValidTime(s_arrivals[i])) continue;
        if (s_arrivals[i].isTraining) pendingTraining++;
        else pendingRace++;
    }
    if (pendingRace == 0 && pendingTraining == 0) {
        s_pendingEmailStart = s_arrivalCount;
        s_lastEmailAttemptMs = now;
        return;
    }

    String subject = String(F("Bricon Rambox \u2014 ")) + String(pendingRace + pendingTraining)
                   + String(F(" chegadas"));
    String body = String(F("Registos captados pelo modulo Bricon:\n\n"));

    String secRaceNew, secTrainNew, secRaceOld;
    appendBriconArrivalSection(secRaceNew, F("Novos Registos - Pombos"), false, s_pendingEmailStart, s_arrivalCount);
    if (secRaceNew.length() > 0) body += secRaceNew + String(F("\n"));
    appendBriconArrivalSection(secTrainNew, F("Treino"), true, s_pendingEmailStart, s_arrivalCount);
    if (secTrainNew.length() > 0) body += secTrainNew + String(F("\n"));

    if (s_pendingEmailStart > 0) {
        body += String(F("\n--- Hist\u00f3rico anterior ---\n\n"));
        appendBriconArrivalSection(secRaceOld, F("Provas anteriores"), false, 0, s_pendingEmailStart);
        if (secRaceOld.length() > 0) body += secRaceOld + String(F("\n"));
    }

    uint16_t totalRace = 0, totalTraining = 0;
    for (uint16_t i = 0; i < s_arrivalCount; i++) {
        if (!hasValidTime(s_arrivals[i])) continue;
        if (s_arrivals[i].isTraining) totalTraining++;
        else totalRace++;
    }
    body += String(F("\nTotais: "));
    body += String(totalRace) + String(F(" pombo(s)"));
    if (totalTraining > 0) {
        body += String(F(" + ")) + String(totalTraining) + String(F(" treino"));
    }
    body += buildEmailSignature();
    s_lastEmailAttemptMs = now;
#if ENABLE_SMTP == 1
    if (startUnikonEmailAsync(subject, body)) {
        s_pendingEmailStart = s_arrivalCount;
        sendVirtualMessage(F("[BRICON] Email queued"), WARN, false, true);
    } else {
        sendVirtualMessage(F("[BRICON] Email deferred (transport busy, retry in 60s)"), WARN, false, true);
    }
#elif ENABLE_GOOGLE_EMAIL == 1
    {
        String dest = configManager.getEmailDestination();
        if (dest.length() > 0 && googleEmailIsAvailable()) {
            extern bool emailArchiveSaveBody(const String&, const String&);
            emailArchiveSaveBody(F("BRICON"), body);
            googleEmailSendSimple(dest, subject, body);
            s_pendingEmailStart = s_arrivalCount;
        }
    }
#endif
#endif
}

// ----- Poll/calendar/pombal requests (structure ready, wire driver TBD) -----
void briconRequestPoll() { s_pollPending = true; }
void briconRequestWOnlyPoll() { s_wPollPending = true; }
void briconRequestIcOnlyPoll() { s_icPollPending = true; }
void briconRequestXOnlyPoll() { s_xPollPending = true; }
void briconStartPolling() { s_pollingActive = true; s_lastPollMs = 0; }
void briconStopPolling() { s_pollingActive = false; }

// BriconPendingDriver: single choke point. Until the tap confirms the exact
// command bytes (GetPigeon/SendFlight/…), NOTHING is transmitted — requests
// are acknowledged and logged so the page/API behave sanely.
static bool briconPendingDriver(const __FlashStringHelper* what) {
    sendVirtualMessage(String(F("[BRICON] ")) + String(what)
                       + String(F(": driver pending hardware tap — listen-only, nothing sent")),
                       WARN, false, true);
    return false;
}

bool briconPollNow() {
    if (!briconIsActive()) return false;
    return briconPendingDriver(F("poll"));
}

// ----- Lifecycle -----
void briconBegin() {
    loadConfig();
    briconAllocateStorage();
    if (s_configWritePending) { /* LittleFS was down; keep memory values */ }
    if (s_cfg.enabled) {
        enforceExclusionOnEnable();
        briconApplySerialProfile();
    }
    s_pollingActive = s_cfg.enabled && s_cfg.pollIntervalSec > 0;
    briconActiveModuleName();
}

void briconUpdate() {
    // Retry a deferred config save (LittleFS was down at change time).
    // Cheap: at most one attempt per 30 s.
    static uint32_t s_lastSaveRetryMs = 0;
    if (s_configWritePending && (s_lastSaveRetryMs == 0 || millis() - s_lastSaveRetryMs >= 30000UL)) {
        s_lastSaveRetryMs = millis();
        if (saveConfig()) s_configWritePending = false;
    }
    if (!briconIsActive()) return;
#if ENABLE_UNIKON == 1
    if (unikonIsPolling()) return;  // never touch the UART during a Unikon poll
#endif
    if (!_rs232PortInitialized) return;

    // Auto-poll timer (structure only — fires into the pending driver for now).
    if (s_pollingActive && s_cfg.pollIntervalSec > 0) {
        if (s_lastPollMs == 0 || millis() - s_lastPollMs >= (uint32_t)s_cfg.pollIntervalSec * 1000UL) {
            s_lastPollMs = millis();
            s_wPollPending = true;
        }
    }

    // Consume pending requests (all funnel through the pending driver until wired).
    if (s_pollPending)   { s_pollPending = false;   briconPollNow(); return; }
    if (s_wPollPending)  { s_wPollPending = false;  briconPollNow(); return; }
    if (s_icPollPending) { s_icPollPending = false; briconPollNow(); return; }
    if (s_xPollPending)  { s_xPollPending = false;  briconPollNow(); return; }

    // Passive RX sniffer: drain + hexdump for the monitor so the first Little
    // bytes on the wire are visible immediately (day-one diagnosis).
    if (!RS232.available()) return;
    uint8_t buf[48];
    uint8_t n = 0;
    while (RS232.available() && n < sizeof(buf)) buf[n++] = (uint8_t)RS232.read();
    if (n == 0) return;
    s_status.connected = true;
    s_status.lastRxMs = millis();
    s_status.bytesRx += n;
    for (uint8_t i = 0; i < n; i++) {
        if (buf[i] == 0x02) s_status.framesSeen++;
    }
    // Throttle: at most one log line per 500 ms to protect the monitor.
    if (millis() - s_lastLogMs < 500) return;
    s_lastLogMs = millis();
    char tmp[4];
    String line;
    line.reserve(n * 3 + 32);
    line += F("[BRICON] RX ");
    line += n;
    line += F("B: ");
    for (uint8_t i = 0; i < n; i++) {
        snprintf(tmp, sizeof(tmp), "%02X ", buf[i]);
        line += tmp;
    }
    sendVirtualMessage(line, WARN, false, true);

    // Spontaneous-frame parse hook: when the Little pushes bytes on its own
    // (training arrivals included), they land here and flow into the same
    // ingest + email funnel as polled arrivals. No parser wired yet (tap TBD).
    // if (briconTryParseSpontaneous(buf, n)) { /* ingested */ }

    briconEmailTick();
}

#endif  // ENABLE_BRICON == 1
