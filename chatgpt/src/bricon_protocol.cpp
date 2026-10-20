// bricon_protocol.cpp
// Bricon loft-module driver.
//
// Mirrors the Unikon engine structure (poll/calendar/pombal/arrivals/email).
// The RS-232 layer below is implemented from the recovered protocol
// (PROTOCOL_RS232.md): framing + CK, ActivateMaster/BA scan, then the
// FlightsReadOut sequence GetFancier -> GetPigeon chain -> GetTimer, plus the
// GetFlightNames and GetBandTable readouts. Validated on the host against the
// captured byte layouts; still to be exercised against a real clock.

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
#include "rambox_email.h"        // shared Rambox email header
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
static BriconConfig s_cfg = { false, 0, 9600, 8, "none", 2, "PT", 0, "0000", "0000" };  // 9600 8N2, PT
static BriconStatus s_status = { false, true, 0, 0, 0, 0, 0, 0, "none" };
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
    s_cfg.stopBits = 2;                       // Bricon = 8N2
    strlcpy(s_cfg.country, "PT", sizeof(s_cfg.country));
    s_cfg.baAddress = 0;
    strlcpy(s_cfg.clubId, "0000", sizeof(s_cfg.clubId));
    strlcpy(s_cfg.flightId, "0000", sizeof(s_cfg.flightId));
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
    s_cfg.stopBits        = doc["stop"]     | 2;
    const char* ctry      = doc["country"]  | "PT";
    strlcpy(s_cfg.country, ctry, sizeof(s_cfg.country));
    s_cfg.baAddress       = doc["ba"]       | 0;
    const char* cl        = doc["club"]     | "0000";
    const char* fl        = doc["flight"]   | "0000";
    strlcpy(s_cfg.clubId, cl, sizeof(s_cfg.clubId));
    strlcpy(s_cfg.flightId, fl, sizeof(s_cfg.flightId));
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
    doc["country"]  = s_cfg.country;
    doc["ba"]       = s_cfg.baAddress;
    doc["club"]     = s_cfg.clubId;
    doc["flight"]   = s_cfg.flightId;
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

bool briconIsEnabled() { return s_cfg.enabled; }

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
        {
            // REA-derived profile: 9600 8N2 and the layout the country selects.
            uint8_t fed = 22;
            const char* shortFed[] = { "PT", "ES", "NL", "UA", "PL", "FR", "KSA" };
            for (size_t i = 0; i < sizeof(shortFed) / sizeof(shortFed[0]); i++)
                if (strcasecmp(s_cfg.country, shortFed[i]) == 0) { fed = 14; break; }
            char prof[160];
            snprintf(prof, sizeof(prof),
                     "[BRICON] profile: %d 8%s%d country=%s (FedBand %u, arrival %u B, fancier %s) BA=%s",
                     s_cfg.baudrate, s_cfg.parity[0] ? s_cfg.parity : "none", s_cfg.stopBits,
                     s_cfg.country, (unsigned)fed, (unsigned)(39 + fed),
                     (!strcasecmp(s_cfg.country, "PT") || !strcasecmp(s_cfg.country, "ES"))
                         ? "non-UK 104B" : "per IsUKProtocol",
                     s_cfg.baAddress ? "explicit" : "auto-scan");
            sendVirtualMessage(String(prof), WARN, false, true);
        }
        sendVirtualMessage(F("[BRICON] enabled — polling the clock"), WARN, false, true);
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
    if (s_cfg.stopBits != 1 && s_cfg.stopBits != 2) s_cfg.stopBits = 2;
    if (s_cfg.country[0] == '\0') strlcpy(s_cfg.country, "PT", sizeof(s_cfg.country));
    if (s_cfg.baAddress != 0 && s_cfg.baAddress != 0x10 &&
        (s_cfg.baAddress < 0x80 || s_cfg.baAddress > 0x85)) s_cfg.baAddress = 0;
    if (strlen(s_cfg.clubId) != 4)   strlcpy(s_cfg.clubId, "0000", sizeof(s_cfg.clubId));
    if (strlen(s_cfg.flightId) != 4) strlcpy(s_cfg.flightId, "0000", sizeof(s_cfg.flightId));
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
// Append one padded cell: " value<pad> |" (width = column width, no overflow).
static void bkAppendCell(String& out, const char* s, uint8_t width) {
    size_t n = s ? strlen(s) : 0;
    if (n > width) n = width;
    out += ' ';
    for (size_t i = 0; i < n; i++) out += s[i];
    for (size_t i = n; i < width; i++) out += ' ';
    out += ' ';
    out += '|';
}

// Loft list in the arrival email (same section the Unikon email already carries,
// so both clocks send "the email with the pigeons").
static void appendBriconRegistrationsSection(String& body) {
    if (s_registrationCount == 0) return;
    body += String(rmbT("Pombal da \u00e9poca (registados) \u2014 ", "Season loft (registered) \u2014 "))
          + String((unsigned)s_registrationCount)
          + String((s_registrationCount == 1) ? rmbT(" pombo\n", " pigeon\n")
                                              : rmbT(" pombos\n", " pigeons\n"));

    uint8_t wNo = 3, wRing = 6;
    for (uint16_t i = 0; i < s_registrationCount; i++) {
        char b[10];
        int n = snprintf(b, sizeof(b), "%u.", (unsigned)(i + 1));
        if (n > (int)wNo) wNo = (uint8_t)n;
        n = (int)strlen(s_registrations[i].ring);
        if (n > (int)wRing) wRing = (uint8_t)n;
    }
    if (wNo > 6) wNo = 6;
    if (wRing > 15) wRing = 15;

    String sep;
    sep += '+';
    for (uint8_t k = 0; k < (uint8_t)(wNo + 2); k++) sep += '-';
    sep += '+';
    for (uint8_t k = 0; k < (uint8_t)(wRing + 2); k++) sep += '-';
    sep += '+';
    sep += '\n';

    body += sep;
    body += '|';
    bkAppendCell(body, rmbT("No", "No"), wNo);
    bkAppendCell(body, rmbT("Anilha", "Ring"), wRing);
    body += '\n';
    body += sep;
    for (uint16_t i = 0; i < s_registrationCount; i++) {
        char b[10];
        snprintf(b, sizeof(b), "%u.", (unsigned)(i + 1));
        body += '|';
        bkAppendCell(body, b, wNo);
        bkAppendCell(body, s_registrations[i].ring, wRing);
        body += '\n';
    }
    body += sep;
    body += '\n';
}

// Email table for the arrival sections. The column widths are COMPUTED from the
// widest text in each column (header vs values), and the separator, the header and
// every row use the same widths — so the table is always aligned, whoever reads it.
static void appendBriconArrivalSection(String& body, const char* title,
                                       bool trainingOnly, uint16_t fromIdx, uint16_t toIdx) {
    const char* HDR[8] = { "No", rmbT("Anilha", "Ring"), "Chip", rmbT("Hora", "Time"),
                           rmbT("Data", "Date"), rmbT("Prova", "Race"), "Pos", "Eval" };
    uint8_t w[8];
    for (uint8_t c = 0; c < 8; c++) w[c] = (uint8_t)strlen(HDR[c]);

    // ---- pass 1: measure (header + every value that will be printed) ----
    for (uint16_t i = fromIdx; i < toIdx; i++) {
        if ((bool)s_arrivals[i].isTraining != trainingOnly) continue;
        if (!hasValidTime(s_arrivals[i])) continue;
        char b[10];
        int n = snprintf(b, sizeof(b), "%u.", (unsigned)(i + 1));
        if (n > (int)w[0]) w[0] = (uint8_t)n;
        n = (int)strlen(s_arrivals[i].ring);
        if (n > (int)w[1]) w[1] = (uint8_t)n;
        n = s_arrivals[i].hasChip ? (int)strlen(s_arrivals[i].chip) : 0;
        if (n > (int)w[2]) w[2] = (uint8_t)n;
        if (w[3] < 8) w[3] = 8;                         // HH:MM:SS
        if (w[4] < 8) w[4] = 8;                         // DD/MM/YY
        n = (int)strlen(s_arrivals[i].race);
        if (n > (int)w[5]) w[5] = (uint8_t)n;
        n = (s_arrivals[i].posArriveClub > 0)
            ? snprintf(b, sizeof(b), "%u", (unsigned)s_arrivals[i].posArriveClub) : 0;
        if (n > (int)w[6]) w[6] = (uint8_t)n;
    }
    if (w[1] > 15) w[1] = 15;                           // sane caps
    if (w[2] > 11) w[2] = 11;
    if (w[5] > 12) w[5] = 12;
    if (w[6] < 3)  w[6] = 3;

    // ---- separator ----
    String sep;
    sep.reserve(96);
    sep += '+';
    for (uint8_t c = 0; c < 8; c++) {
        for (uint8_t k = 0; k < (uint8_t)(w[c] + 2); k++) sep += '-';
        sep += '+';
    }
    sep += '\n';

    // ---- header ----
    String hdr;
    hdr.reserve(96);
    hdr += '|';
    for (uint8_t c = 0; c < 8; c++) bkAppendCell(hdr, HDR[c], w[c]);
    hdr += '\n';

    String section;
    section.reserve(220 + (uint16_t)(toIdx - fromIdx) * 96u);
    section += sep;
    section += hdr;
    section += sep;

    uint16_t rowCount = 0;
    for (uint16_t i = fromIdx; i < toIdx; i++) {
        if ((bool)s_arrivals[i].isTraining != trainingOnly) continue;
        if (!hasValidTime(s_arrivals[i])) continue;
        rowCount++;

        char timeStr[10] = "";
        const char* t6 = s_arrivals[i].arrivalTime;
        snprintf(timeStr, sizeof(timeStr), "%c%c:%c%c:%c%c", t6[0], t6[1], t6[2], t6[3], t6[4], t6[5]);

        char dateStr[10] = "";
        const char* d = s_arrivals[i].arrivalDate;
        size_t dl = strlen(d);
        if (dl >= 6)      snprintf(dateStr, sizeof(dateStr), "%c%c/%c%c/%c%c", d[0], d[1], d[2], d[3], d[4], d[5]);
        else if (dl >= 4) snprintf(dateStr, sizeof(dateStr), "%c%c/%c%c", d[0], d[1], d[2], d[3]);

        char noStr[10];
        snprintf(noStr, sizeof(noStr), "%u.", (unsigned)(i + 1));
        char posStr[10] = "";
        if (s_arrivals[i].posArriveClub > 0)
            snprintf(posStr, sizeof(posStr), "%u", (unsigned)s_arrivals[i].posArriveClub);
        char evalStr[3] = "";
        if (s_arrivals[i].evaluation) { evalStr[0] = s_arrivals[i].evaluation; evalStr[1] = '\0'; }

        section += '|';
        bkAppendCell(section, noStr, w[0]);
        bkAppendCell(section, s_arrivals[i].ring, w[1]);
        bkAppendCell(section, s_arrivals[i].hasChip ? s_arrivals[i].chip : "", w[2]);
        bkAppendCell(section, timeStr, w[3]);
        bkAppendCell(section, dateStr, w[4]);
        bkAppendCell(section, s_arrivals[i].race, w[5]);
        bkAppendCell(section, posStr, w[6]);
        bkAppendCell(section, evalStr, w[7]);
        section += '\n';
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

    // Shared Rambox layout: the newest contest gives the "Prova" line, so the email
    // looks the same as the Unikon one (Clube / Concorrente / Prova / Solta).
    const BriconContest* rc = (s_contestCount > 0) ? &s_contests[s_contestCount - 1] : nullptr;
    String raceNm = rc ? String(rc->name) : String(F(""));
    String clubStr = String(s_cfg.clubId);
    if (s_cfg.flightId[0]) clubStr += String(F(" / voo ")) + String(s_cfg.flightId);
    String subject = ramboxEmailSubject("Bricon", raceNm.c_str(), pendingRace + pendingTraining);
    String body;
    ramboxEmailHeader(body, "Bricon", clubStr.c_str(), "",
                      s_status.fancierName, raceNm.c_str(), rc ? rc->date : "", "");
    appendBriconRegistrationsSection(body);

    String secRaceNew, secTrainNew, secRaceOld;
    appendBriconArrivalSection(secRaceNew, ramboxEmailSecNew(), false, s_pendingEmailStart, s_arrivalCount);
    if (secRaceNew.length() > 0) body += secRaceNew + String(F("\n"));
    appendBriconArrivalSection(secTrainNew, ramboxEmailSecTrain(), true, s_pendingEmailStart, s_arrivalCount);
    if (secTrainNew.length() > 0) body += secTrainNew + String(F("\n"));

    if (s_pendingEmailStart > 0) {
        body += String(F("\n")) + String(ramboxEmailSecHistory()) + String(F("\n\n"));
        appendBriconArrivalSection(secRaceOld, ramboxEmailSecOld(), false, 0, s_pendingEmailStart);
        if (secRaceOld.length() > 0) body += secRaceOld + String(F("\n"));
    }

    uint16_t totalRace = 0, totalTraining = 0;
    for (uint16_t i = 0; i < s_arrivalCount; i++) {
        if (!hasValidTime(s_arrivals[i])) continue;
        if (s_arrivals[i].isTraining) totalTraining++;
        else totalRace++;
    }
    ramboxEmailTotals(body, totalRace, totalTraining);
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

// ===========================================================================
// Wire layer — framing recovered from BriconLib.Communication::SendCommand
//   request   6C addr cmd n payload(N) CK    CK = (6C+addr+cmd+n+sum(payload))&FF
//   response  33 addr cmd n status payload(N-1) CK
//                                            CK = (33+addr+cmd+n+status+sum)&FF
//   n == 0 => no status byte. addr/cmd must be echoed.
// The app writes byte by byte with Thread.Sleep(1) => 1 ms pacing per byte.
// ===========================================================================
#define BK_START_REQ   0x6C
#define BK_START_RSP   0x33
#define BK_ADDR_MASTER 0x10
#define BK_TIMEOUT_MS  800
#define BK_FRAME_MAX   160

static void bkFlushRx() {
    uint32_t t = millis();
    while (RS232.available() && millis() - t < 50) RS232.read();
}

static uint8_t bkChecksumReq(uint8_t addr, uint8_t cmd, uint8_t n, const uint8_t* payload) {
    uint16_t sum = BK_START_REQ + addr + cmd + n;
    for (uint8_t i = 0; i < n; i++) sum += payload[i];
    return (uint8_t)(sum & 0xFF);
}

// Send one request: 6C addr cmd n payload CK (byte by byte, 1 ms pacing).
static void bkSendRequest(uint8_t addr, uint8_t cmd, const uint8_t* payload, uint8_t n) {
    uint8_t frame[BK_FRAME_MAX];
    uint8_t p = 0;
    frame[p++] = BK_START_REQ;
    frame[p++] = addr;
    frame[p++] = cmd;
    frame[p++] = n;
    for (uint8_t i = 0; i < n; i++) frame[p++] = payload[i];
    frame[p++] = bkChecksumReq(addr, cmd, n, payload);
    for (uint8_t i = 0; i < p; i++) {
        RS232.write(frame[i]);
        delay(1);                      // the app paces every byte with Sleep(1)
    }
    RS232.flush();
    s_status.framesSeen++;                    // one request out
}

// Read one reply: 33 addr cmd n status payload(n-1) CK. Returns true when a
// well-formed reply that echoes addr/cmd arrived; status/payload are filled.
static bool bkReadReply(uint8_t addr, uint8_t cmd,
                        uint8_t* status, uint8_t* payload, uint8_t* payloadLen,
                        uint32_t timeoutMs) {
    uint8_t buf[BK_FRAME_MAX];
    uint8_t len = 0;
    uint32_t t0 = millis();
    bool inFrame = false;
    while (millis() - t0 < timeoutMs) {
        if (!RS232.available()) { yield(); continue; }
        uint8_t b = (uint8_t)RS232.read();
        s_status.bytesRx++;
        if (!inFrame) {
            if (b != BK_START_RSP) continue;      // resync on the response start byte
            inFrame = true;
            len = 0;
        }
        if (len < BK_FRAME_MAX) buf[len++] = b;
        else { inFrame = false; len = 0; continue; }
        if (len < 4) continue;                    // need addr/cmd/n
        uint8_t n = buf[3];
        if (len < (uint8_t)(5 + n)) continue;     // 4 header + n payload + CK
        // complete frame
        inFrame = false;
        if (buf[1] != addr || buf[2] != cmd) {
            char m[96];
            snprintf(m, sizeof(m), "[BRICON] reply echo mismatch (want %02X/%02X, got %02X/%02X)",
                     addr, cmd, buf[1], buf[2]);
            sendVirtualMessage(String(m), WARN, false, true);
            return false;
        }
        uint8_t st = 0, plen = 0;
        uint16_t sum = BK_START_RSP + buf[1] + buf[2] + n;
        if (n) { st = buf[4]; sum += st; }
        for (uint8_t i = 0; i < (uint8_t)(n ? n - 1 : 0); i++) { payload[i] = buf[5 + i]; sum += buf[5 + i]; }
        plen = (uint8_t)(n ? n - 1 : 0);
        uint8_t ck = buf[4 + n];
        if (ck != (uint8_t)(sum & 0xFF)) {
            char m[96];
            snprintf(m, sizeof(m), "[BRICON] reply bad CK (got %02X want %02X) — retrying",
                     ck, (uint8_t)(sum & 0xFF));
            sendVirtualMessage(String(m), WARN, false, true);
            return false;
        }
        *status = st;
        *payloadLen = plen;
        s_status.framesSeen++;                // one reply in
        s_status.baAddress = addr;
        return true;
    }
    sendVirtualMessage(F("[BRICON] reply timeout"), WARN, false, true);
    return false;
}

// One command round-trip. retries mirrors the app's tolerance for a lost reply.
static bool bkCommand(uint8_t addr, uint8_t cmd, const uint8_t* payload, uint8_t n,
                      uint8_t* status, uint8_t* rspPayload, uint8_t* rspLen) {
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        bkFlushRx();
        bkSendRequest(addr, cmd, payload, n);
        if (bkReadReply(addr, cmd, status, rspPayload, rspLen, BK_TIMEOUT_MS)) return true;
        delay(20);
    }
    return false;
}

// ---- device activation / discovery (Communication::RefreshComPort order) ----
static uint8_t s_baAddr = 0;

static bool briconActivate(uint8_t addr) {
    uint8_t status = 0, len = 0, pl[4];
    if (addr == BK_ADDR_MASTER) {
        if (!bkCommand(BK_ADDR_MASTER, 0x61, nullptr, 0, &status, pl, &len)) return false;
    } else {
        uint8_t p[1] = { 0x00 };
        if (!bkCommand(addr, 0x50, p, 1, &status, pl, &len)) return false;
    }
    if (status != 0x00) {
        char m[80];
        snprintf(m, sizeof(m), "[BRICON] activate 0x%02X refused (status 0x%02X)", addr, status);
        sendVirtualMessage(String(m), WARN, false, true);
        return false;
    }
    s_baAddr = addr;
    s_status.connected = true;
    char m[80];
    snprintf(m, sizeof(m), "[BRICON] device activated at address 0x%02X", addr);
    sendVirtualMessage(String(m), WARN, false, true);
    return true;
}

// Try the configured address first, then the recovered scan order.
static bool briconFindDevice() {
    if (s_cfg.baAddress != 0) return briconActivate((uint8_t)s_cfg.baAddress);
    const uint8_t order[] = { BK_ADDR_MASTER, 0x82, 0x81, 0x83, 0x84, 0x85, 0x80, 0x00 };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (briconActivate(order[i])) return true;
    }
    sendVirtualMessage(F("[BRICON] no clock answered the activation scan"), WARN, false, true);
    return false;
}

// ---- record parsers -------------------------------------------------------
static void bkAscii(const uint8_t* p, uint8_t n, char* out, size_t outSize) {
    size_t k = 0;
    for (uint8_t i = 0; i < n && k + 1 < outSize; i++) {
        char c = (char)p[i];
        if (c == '\0') break;                 // ByteArrayToString: from the first 0x00 it is spaces
        out[k++] = (c >= 0x20 && c < 0x7F) ? c : ' ';
    }
    out[k] = '\0';
    while (k > 0 && out[k - 1] == ' ') out[--k] = '\0';   // trim
}

static uint8_t bkFedBandForCountry() {
    const char* shortFed[] = { "PT", "ES", "NL", "UA", "PL", "FR", "KSA" };
    for (size_t i = 0; i < sizeof(shortFed) / sizeof(shortFed[0]); i++)
        if (strcasecmp(s_cfg.country, shortFed[i]) == 0) return 14;
    return 22;
}

// GetPigeonCommand::HandleResponse (rva 0x54270). Layout:
//   reserved(2) ElBand(8) FedBand(14|22) ClubID(4) FlightID(4) Position1..4(2 BE)
//   Date(6 DDMMYY) Time(6 HHMMSS) Evaluation(1) [+1..2 ignored] [+1 TeamNbr RO/MD]
static bool briconParsePigeon(const uint8_t* p, uint8_t len, BriconArrival* out) {
    if (len < 39) return false;
    uint8_t fed = bkFedBandForCountry();
    if (len < (uint8_t)(39 + fed)) {
        // the reply length itself is a discriminator in the app; fall back to 14
        fed = 14;
        if (len < (uint8_t)(39 + fed)) return false;
    }
    memset(out, 0, sizeof(*out));
    uint8_t off = 2;
    bkAscii(p + off, 8, out->chip, sizeof(out->chip));  off += 8;
    out->hasChip = (out->chip[0] != '\0');
    bkAscii(p + off, fed, out->ring, sizeof(out->ring)); off += fed;
    char club[5] = {0}, flight[5] = {0};
    bkAscii(p + off, 4, club, sizeof(club)); off += 4;
    bkAscii(p + off, 4, flight, sizeof(flight)); off += 4;
    uint16_t pos[4];
    for (int i = 0; i < 4; i++) { pos[i] = (uint16_t)((p[off] << 8) | p[off + 1]); off += 2; }
    bkAscii(p + off, 6, out->arrivalDate, sizeof(out->arrivalDate)); off += 6;
    bkAscii(p + off, 6, out->arrivalTime, sizeof(out->arrivalTime)); off += 6;
    out->evaluation = (off < len) ? (char)p[off] : '\0';
    out->posArriveClub = pos[0];
    out->isTraining = false;          // derived from the enclosing flight, not from Evaluation
    if (club[0] && !(club[0] == '0' && club[1] == '0' && club[2] == '0' && club[3] == '0'))
        strlcpy(out->race, flight, sizeof(out->race));
    return true;
}

// ---- readouts -------------------------------------------------------------
// FlightsReadOut::frm_OnPostCommands posts GetFancier -> GetPigeon -> GetTimer.
static void briconReadFancier() {
    uint8_t status = 0, len = 0, pl[128];
    uint8_t req[1] = { 0x00 };
    if (!bkCommand(s_baAddr, 0x51, req, 1, &status, pl, &len)) return;
    if (status != 0x00 || len < 20) return;
    bool uk = false;
    const char* ukc[] = { "IN","KV","UK","PL","HU","RO","MD","JP","AU","IR","SA","SK","TS","CZ","NZ","MX","US","ES","PT","FI" };
    for (size_t i = 0; i < sizeof(ukc) / sizeof(ukc[0]); i++)
        if (strcasecmp(s_cfg.country, ukc[i]) == 0) { uk = true; break; }
    if (!strcasecmp(s_cfg.country, "PT") || !strcasecmp(s_cfg.country, "ES")) uk = false;
    uint8_t licLen = uk ? 10 : 8;
    if (len < licLen) return;
    char lic[16] = {0}, name[32] = {0};
    bkAscii(pl, licLen, lic, sizeof(lic));
    uint8_t off = licLen;
    bkAscii(pl + off, uk ? 24 : 25, name, sizeof(name));
    // Keep the fancier (the email header shows "Concorrente" like the Unikon one).
    strlcpy(s_status.fancierLic, lic, sizeof(s_status.fancierLic));
    strlcpy(s_status.fancierName, name, sizeof(s_status.fancierName));
    char m[128];
    snprintf(m, sizeof(m), "[BRICON] fancier: %s '%s' (%s layout)", lic, name, uk ? "UK" : "non-UK");
    sendVirtualMessage(String(m), WARN, false, true);
}

// GetPigeon chain: index+1 until status 0x05 (NoRecordThatCompliesTheRequest).
static uint16_t briconReadArrivals() {
    uint16_t idx = 0, got = 0;
    for (uint16_t guard = 0; guard < BRICON_MAX_ARRIVALS + 16; guard++) {
        uint8_t req[10];
        req[0] = (uint8_t)(idx >> 8);
        req[1] = (uint8_t)(idx & 0xFF);
        // club + flight codes (4 chars each); "0000" = the wildcard the app tolerates
        memcpy(req + 2, s_cfg.clubId, 4);
        memcpy(req + 6, s_cfg.flightId, 4);
        uint8_t status = 0, len = 0, pl[128];
        if (!bkCommand(s_baAddr, 0x58, req, 10, &status, pl, &len)) break;
        if (status == 0x05) break;                 // end of chain
        if (status != 0x00) {
            char m[80];
            snprintf(m, sizeof(m), "[BRICON] GetPigeon idx=%u status 0x%02X — stopping", idx, status);
            sendVirtualMessage(String(m), WARN, false, true);
            break;
        }
        BriconArrival a;
        if (briconParsePigeon(pl, len, &a)) {
            if (briconIngestArrival(a)) got++;
            if (got <= 3 || (got % 25) == 0) {     // throttled log
                char m[128];
                snprintf(m, sizeof(m), "[BRICON] arrival %u: %s %s %s %s pos=%u eval=%c",
                         (unsigned)got, a.chip, a.ring, a.arrivalDate, a.arrivalTime,
                         (unsigned)a.posArriveClub, a.evaluation ? a.evaluation : '?');
                sendVirtualMessage(String(m), WARN, false, true);
            }
        }
        idx++;
    }
    return got;
}

static void briconReadTimer() {
    uint8_t status = 0, len = 0, pl[80];
    uint8_t req[5] = { 0x00, '0', '0', '0', '0' };
    if (!bkCommand(s_baAddr, 0x5D, req, 5, &status, pl, &len)) return;
    if (status != 0x00 || len < 50) return;
    char d[8] = {0}, t[8] = {0}, rd[8] = {0}, rt[8] = {0};
    bkAscii(pl + 8, 6, d, sizeof(d));
    bkAscii(pl + 14, 6, t, sizeof(t));
    bkAscii(pl + 30, 6, rd, sizeof(rd));
    bkAscii(pl + 36, 6, rt, sizeof(rt));
    char m[112];
    snprintf(m, sizeof(m), "[BRICON] timer: basketing %s %s, readout %s %s", d, t, rd, rt);
    sendVirtualMessage(String(m), WARN, false, true);
}

// GetFlightNames chain (FA 0C idx) — the contest calendar.
static uint16_t briconReadCalendar() {
    uint16_t idx = 0, got = 0;
    for (uint16_t guard = 0; guard < BRICON_MAX_CONTESTS + 8; guard++) {
        uint8_t req[2] = { 0x0C, (uint8_t)idx };
        uint8_t status = 0, len = 0, pl[128];
        if (!bkCommand(s_baAddr, 0xFA, req, 2, &status, pl, &len)) break;
        if (status == 0x05 || status == 0x0B) break;      // end of the list
        if (status != 0x00) break;
        if (!briconStorageReady() || s_contestCount >= BRICON_MAX_CONTESTS) break;
        BriconContest c;
        memset(&c, 0, sizeof(c));
        bkAscii(pl, 3, c.code, sizeof(c.code));
        bkAscii(pl + 3, 6, c.date, sizeof(c.date));
        bkAscii(pl + 9, 9, c.name, sizeof(c.name));
        // A clock that repeats the same record must not fill the calendar with clones.
        bool dup = false;
        for (uint16_t k = 0; k < s_contestCount; k++) {
            if (strncmp(s_contests[k].code, c.code, sizeof(c.code)) == 0 &&
                strncmp(s_contests[k].date, c.date, sizeof(c.date)) == 0) { dup = true; break; }
        }
        if (!dup) { s_contests[s_contestCount++] = c; got++; }
        idx++;
    }
    char m[80];
    snprintf(m, sizeof(m), "[BRICON] calendar: %u contest(s)", (unsigned)got);
    sendVirtualMessage(String(m), WARN, false, true);
    return got;
}

// GetBandTable (52|C2) — the fancier's pigeon list (the "pombal").
static uint16_t briconReadPombal() {
    uint8_t req[3] = { 0x00, 0x00, 0x0A };
    uint8_t status = 0, len = 0, pl[128];
    if (!bkCommand(s_baAddr, 0x52, req, 3, &status, pl, &len)) return 0;
    if (status != 0x00) {
        char m[80];
        snprintf(m, sizeof(m), "[BRICON] GetBandTable status 0x%02X", status);
        sendVirtualMessage(String(m), WARN, false, true);
        return 0;
    }
    uint16_t got = 0;
    // Records are 12 bytes: 8-char ring + 4-char club, until the payload runs out.
    for (uint8_t off = 0; off + 12 <= len && s_registrationCount < BRICON_MAX_ARRIVALS; off += 12) {
        BriconArrival r;
        memset(&r, 0, sizeof(r));
        bkAscii(pl + off, 8, r.ring, sizeof(r.ring));
        s_registrations[s_registrationCount++] = r;
        got++;
    }
    char m[96];
    snprintf(m, sizeof(m), "[BRICON] pombal: %u pigeon(s) read (total %u)",
             (unsigned)got, (unsigned)s_registrationCount);
    sendVirtualMessage(String(m), WARN, false, true);
    return got;
}

// A full poll = identify/activate + the FlightsReadOut sequence + calendar.
static bool briconPoll(bool arrivals, bool calendar, bool pombal) {
    if (!briconIsActive()) return false;
#if ENABLE_UNIKON == 1
    if (unikonIsPolling()) return false;          // never fight over the UART
#endif
    if (!_rs232PortInitialized) {
        sendVirtualMessage(F("[BRICON] poll skipped: RS232 not ready"), WARN, false, true);
        return false;
    }
    s_status.connected = false;
    if (!briconFindDevice()) return false;
    if (arrivals) {
        briconReadFancier();
        uint16_t n = briconReadArrivals();
        briconReadTimer();
        s_status.lastNewCount = n;
        char m[80];
        snprintf(m, sizeof(m), "[BRICON] poll done — %u arrival(s)", (unsigned)n);
        sendVirtualMessage(String(m), WARN, false, true);
    }
    if (calendar) briconReadCalendar();
    if (pombal)   briconReadPombal();
    s_status.lastPollMs = millis();
    return true;
}

bool briconPollNow() { return briconPoll(true, true, true); }

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
    // Requests map to the captured readouts: W-only = arrivals (GetPigeon chain),
    // IC-only = calendar (GetFlightNames chain), X-only = pombal (GetBandTable).
    if (s_pollPending)   { s_pollPending = false;   briconPoll(true, true, true);   return; }
    if (s_wPollPending)  { s_wPollPending = false;  briconPoll(true, false, false); return; }
    if (s_icPollPending) { s_icPollPending = false; briconPoll(false, true, false); return; }
    if (s_xPollPending)  { s_xPollPending = false;  briconPoll(false, false, true); return; }

    // Arrival email on EVERY loop pass. A synchronous poll ends with no further RX, and
    // the tick used to live only in the RX logger -- so after a poll the arrivals were
    // ingested but never emailed. Cheap when there is nothing pending (early return).
    briconEmailTick();

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
        if (buf[i] == BK_START_RSP) s_status.framesSeen++;   // stray reply start byte
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
