// unikon_protocol.cpp
// Unikon RAMBOX RS-232 protocol driver — implementation.
//
// RAMBOX protocol families — verified against real RS-232 sniffer captures:
//
//   Step 1 — Handshake:
//     PC  -> v-frame (02 81 76 F7 80 03)
//     RMB <- 0x15 NAK (not ready — always on first attempt)
//     PC  -> v-frame (retry)
//     RMB <- ident frame (02 81 76 [serial] ... 03) — *no 0x06 ACK before this*
//     PC  -> 0x06 ACK
//
//   Step 2 — Status:
//     PC  -> X-frame (02 81 58 D9 80 03)
//     RMB <- 0x06 ACK (twice in practice — second 0x06 is discarded by readFrame)
//     Variant (4.txt): RMB sends another ident frame + immediately starts S-frames
//       without ACKing X-frame. Code handles this via s_prefetchedByte.
//
//   Step 3 — Data stream (S-frames first, then ZP-frames, then EOT):
//     RMB <- S1/S2/S3 frames (club/solta info) -> PC ACKs each
//     RMB <- ZP frames (one per pigeon: ring + chip) -> PC ACKs each
//     RMB <- 0x04 EOT
//
//   Separate W session (captures 9.txt / 12.txt):
//     PC  -> v-frame
//     RMB <- ident frame
//     PC  -> ACK
//     PC  -> W-frame (02 81 57 D8 80 03)
//     RMB <- frame '3' (race info) + P-frames (confirmed arrivals with HHMMSS/DDMM)
//
//   Separate IC0 session (captures 1.txt / 6.txt):
//     PC  -> v-frame
//     RMB <- ident frame
//     PC  -> ACK
//     PC  -> IC0-frame (02 81 49 43 30 3D 81 03)
//     RMB <- I-frames (contest schedule) -> PC ACKs each -> EOT
//
// ZP frame layout (30 bytes total):
//   STX ADDR 'Z' 'P' RING[11] ' ' CHIP[11] CK1 CK2 ETX
//   CHIP '00000000000' = no transponder.
//
// NOTE: ZP only contains registered pigeons (ring+chip), not confirmed arrivals.
// Confirmed arrivals come from W-phase P-frames, which carry HHMMSS/DDMM.

#include "unikon_protocol.h"
#if ENABLE_BRICON == 1
#include "bricon_protocol.h"   // mutual exclusion: only one Rambox module at a time
#endif

#include "feature_flags.h"
#if ENABLE_UNIKON == 1

#include <LittleFS.h>
#include <ArduinoJson.h>
#include "littlefs_manager.h"
#include "direct_log.h"
#include "email_message_builder.h"
#include "rambox_email.h"        // shared Rambox email header
#include "ConfigManager.h"
#include "psram_helpers.h"
#include "commands.h"  // LogLevel + sendVirtualMessage for debug output
extern ConfigManager configManager;
#if ENABLE_SMTP == 1
#include "runtime_email.h"
#endif
#if ENABLE_GOOGLE_EMAIL == 1
#include "google_email.h"
#endif

#define UNIKON_CFG_PATH      "/config/unikon.json"
#define UNIKON_TIMEOUT_MS       700     // wait for first response byte — matched to rambox_read.ps1 (RxB 700ms)
#define UNIKON_POST_NAK_MS      0       // no post-NAK ident window — matched to rambox_read.ps1 (retries immediately after NAK)
#define UNIKON_NAK_RETRY_DELAY  80      // pause after NAK before retry — matched to rambox_read.ps1 (Start-Sleep 80ms)
#define UNIKON_MAX_RETRIES      5       // NAK retry count — matched to rambox_read.ps1 (for loop 1..5)
#define UNIKON_NO_DEVICE_BACKOFF_MS  60000UL  // back-off when no RAMBOX responds, to minimize RS232 lock time
#define UNIKON_MAX_SFRAME_CONTENT 64    // S1/S2/S3 content bytes cached for training replay (ADDR+payload, no STX/ETX)
// Maximum interval between successive Unikon email attempts.
// Guarantees that pending arrivals are retried within this window even if the
// email task was busy on the previous poll.
#define UNIKON_EMAIL_RETRY_MS        60000UL

// ----- File-local state -----

static UnikonConfig s_cfg = { false, 180, false, true };
static UnikonStatus s_status;
// Allocated in PSRAM at runtime by unikonAllocatePsram() — frees ~23.6 KB of SRAM.
static UnikonArrival* s_arrivals = nullptr;
static UnikonArrival* s_registrations = nullptr;

static uint16_t s_arrivalCount = 0;
static uint16_t s_registrationCount = 0;
static UnikonContest* s_contests = nullptr;
static uint16_t s_contestCount = 0;

static inline size_t unikonArrivalStorageBytes() {
    return (size_t)UNIKON_MAX_ARRIVALS * sizeof(UnikonArrival);
}

static inline size_t unikonContestStorageBytes() {
    return (size_t)UNIKON_MAX_CONTESTS * sizeof(UnikonContest);
}

static inline bool unikonStorageReady() {
    return s_arrivals != nullptr && s_registrations != nullptr && s_contests != nullptr;
}

static inline void clearArrivalsStorage() {
    if (s_arrivals) memset(s_arrivals, 0, unikonArrivalStorageBytes());
}

static inline void clearRegistrationsStorage() {
    if (s_registrations) memset(s_registrations, 0, unikonArrivalStorageBytes());
}

static inline void clearContestsStorage() {
    if (s_contests) memset(s_contests, 0, unikonContestStorageBytes());
}

static bool s_pollingActive    = false;
static bool s_passiveListening = false;  // true while tryPassiveListen() holds the UART
static bool s_pollInProgress = false;  // true while a poll cycle is actively running
static volatile bool s_configWritePending = false;  // config changed but LittleFS was unavailable
static unsigned long s_lastPollEndMs = 0;  // millis() when last poll finished
static const unsigned long UNIKON_POST_POLL_BLACKOUT_MS = 500;  // ignore RS232 bytes for this long after poll
static uint32_t s_lastPollMs = 0;
static volatile bool s_pollPending = false;
static volatile bool s_wPollPending = false;  // W-only poll: arrivals refresh without X-phase
static bool s_forceWOnlyPoll = false;  // next poll cycle skips X-phase and refreshes arrivals only
static volatile bool s_icPollPending = false;  // IC-only poll: calendar refresh without X/W phases
static bool s_forceIcOnlyPoll = false;  // next poll cycle skips X/W and refreshes calendar only
static volatile bool s_xPollPending = false;  // X-only poll: pombal refresh without W/IC phases
static bool s_forceXOnlyPoll = false;  // next poll cycle skips W/IC and refreshes pombal only
static volatile bool s_sendTrainingPending = false;  // write training session to RAMBOX
static volatile bool s_clearTrainingPending = false; // clear training contest from RAMBOX
static uint8_t s_prefetchedByte = 0;  // byte pre-read during X-frame step; consumed by readFrame
static uint32_t s_noDeviceBackoffUntilMs = 0;  // if >0, skip polls until this timestamp (no RAMBOX found)

// Last S1/S2/S3 frame contents seen on the wire (ADDR+payload, no STX/ETX/checks).
// Replayed verbatim when programming a training session so the fictitious race
// carries the same club/solta/fancier context (see captures 26/27: IC0+I+S+ZP).
static uint8_t s_cachedS1[UNIKON_MAX_SFRAME_CONTENT];
static uint8_t s_cachedS1Len = 0;
static uint8_t s_cachedS2[UNIKON_MAX_SFRAME_CONTENT];
static uint8_t s_cachedS2Len = 0;
static uint8_t s_cachedS3[UNIKON_MAX_SFRAME_CONTENT];
static uint8_t s_cachedS3Len = 0;

// Contest name of the last programmed training session (e.g. "T26071430").
// W-phase tags P-frames as training while the current frame-'3' race matches it.
static bool s_inTrainingRace = false;

// Contest code carried by the I-frame. The original software's builder is
//     "I0%02u%02u%02u%-8s%01u"  ->  'I' '0' RR[2] DD[2] MM[2] NAME[8] N[1]
// The '0' immediately after 'I' is a LITERAL (both liberation-plan samples carry it
// while the race number differs), so the code must be '0' + a 2-digit race number.
// "990" wrote '9' where that literal belongs and shifted the fields by one -- prime
// suspect for the failed first training test. Race number 90 keeps the old number.
#define UNIKON_TRAINING_CODE "090"
// Trailing digit of the name field (real builder: %-8s%01u). Programmed races in the
// reference data carry '1'; a placeholder entry carried '0'. Not proven by the RE.
#define UNIKON_I_TAIL_RACE  '1'
#define UNIKON_I_TAIL_CLEAR '0'
// Pigeon record form used when WRITING the entry list:
//   1 = "ZP" + ring[11] + ' ' + chip[11]  (the form the RAMBOX itself emits on the X
//       phase -- 30-byte frame; measured on every sniffer ZP record)
//   0 = "P"  + ring[11] + ' ' + chip[11]  (the form the PC-side files carry -- 29-byte
//       frame; the RAMBOX adds the 'Z' tag when it emits)
// If the RAMBOX rejects the ZP write, flip this to 0.
#define UNIKON_ZP_WRITE_PREFIX_Z 1
// Entry-list selection sentinels (see setTrainingFilter()). They cannot collide with a
// ring or an S4 line, so an empty list keeps its legacy meaning ("whole pombal").
#define UNIKON_TRAIN_SEL_ALL  "*"
#define UNIKON_TRAIN_SEL_NONE "-"
// What the filter means: LIST = only the listed rings, ALL = whole pombal, NONE = no
// pigeons at all (program the race alone).
enum {
    UNIKON_TRAIN_MODE_LIST = 0,
    UNIKON_TRAIN_MODE_ALL  = 1,
    UNIKON_TRAIN_MODE_NONE = 2
};

// Pure match on the contest code carried by frame '3' at bytes [9..11]
// (see captures 9/17: "001"/"003" right after the 7-char solta block).
// Robust even when the frame has no name/date ("sem dados de concurso").
static bool isTrainingContestCode(const char* raceCode, const char* trainingCode) {
    if (!raceCode || !trainingCode) return false;
    if (raceCode[0] == '\0' || trainingCode[0] == '\0') return false;
    while (*raceCode == ' ') raceCode++;   // tolerate a space-padded frame '3' field
    if (strcmp(raceCode, trainingCode) == 0) return true;
    // Tolerate the RAMBOX reporting the bare race number without the leading literal
    // '0' (trainingCode is '0' + RR[2], so trainingCode + 1 is the bare RR). This keeps
    // the training tag working whichever form the device echoes back in frame '3'.
    if (trainingCode[0] == '0' && strcmp(raceCode, trainingCode + 1) == 0) return true;
    return false;
}

// Training entry filter: which rings to program.
//   empty text  -> UNIKON_TRAIN_MODE_ALL  (legacy: whole pombal)
//   "*"         -> UNIKON_TRAIN_MODE_ALL  (explicit whole pombal)
//   "-"         -> UNIKON_TRAIN_MODE_NONE (race only, no pigeons)
//   otherwise   -> UNIKON_TRAIN_MODE_LIST (S4 lines / plain 11-digit rings)
static char s_trainingFilter[UNIKON_MAX_ARRIVALS][12];
// Anilhas efectivamente enviadas no treino (para o read-back do modo LIST).
static char s_sentRings[UNIKON_MAX_ARRIVALS][12];
static uint16_t s_trainingFilterCount = 0;
static uint8_t  s_trainingMode = UNIKON_TRAIN_MODE_ALL;

// Parse one entry-list line: full S4 record ("S4<10 contest>P<ring11>...")
// or a plain 11-digit ring. True when a ring was extracted (11 chars + null).
static bool parseTrainingEntryLine(const char* line, char* outRing) {
    if (!line || !outRing) return false;
    while (*line == ' ' || *line == '\t' || *line == '\r') line++;
    if (line[0] == '\0' || line[0] == '\n') return false;
    // Full S4 line: "S4" + 10 contest chars + 1 marker + 11-digit ring.
    if (line[0] == 'S' && line[1] == '4') {
        size_t len = strlen(line);
        if (len < 24) return false;
        for (int i = 0; i < 11; i++) {
            char c = line[13 + i];
            if (c < '0' || c > '9') return false;
        }
        memcpy(outRing, line + 13, 11);
        outRing[11] = '\0';
        return true;
    }
    // Plain 11-char ring. The module's registrations can be alphanumeric (a custom
    // ring read from the pombal), so accept any printable non-space char -- requiring
    // digits only made the filter silently select nothing for such rings.
    int i = 0;
    while (i < 11 && line[i] > ' ' && line[i] != '\r' && line[i] != '\n') {
        outRing[i] = line[i];
        i++;
    }
    if (i != 11) return false;
    outRing[11] = '\0';
    return true;
}

// Selection test for the entry list (see s_trainingMode).
static bool trainingFilterAllows(const char* ring) {
    if (s_trainingMode == UNIKON_TRAIN_MODE_ALL)  return true;
    if (s_trainingMode == UNIKON_TRAIN_MODE_NONE) return false;
    if (!ring) return false;
    for (uint16_t i = 0; i < s_trainingFilterCount; i++) {
        if (memcmp(s_trainingFilter[i], ring, 11) == 0) return true;
    }
    return false;
}

// Remember one S-frame payload for later training replay. Only stores the
// content bytes (ADDR first), capped at UNIKON_MAX_SFRAME_CONTENT.
static void cacheSFrame(const uint8_t* f, uint8_t fLen) {
    if (fLen < 6 || f[0] != UNIKON_STX || f[2] != 0x53) return;
    uint8_t contentLen = (uint8_t)(fLen - 4);  // strip STX + CK1 + CK2 + ETX
    if (contentLen == 0 || contentLen > UNIKON_MAX_SFRAME_CONTENT) return;
    if (f[3] == 0x31) {
        memcpy(s_cachedS1, f + 1, contentLen);
        s_cachedS1Len = contentLen;
    } else if (f[3] == 0x32) {
        memcpy(s_cachedS2, f + 1, contentLen);
        s_cachedS2Len = contentLen;
    } else if (f[3] == 0x33) {
        memcpy(s_cachedS3, f + 1, contentLen);
        s_cachedS3Len = contentLen;
    }
}

// Pending-email window: arrivals at s_arrivals[s_pendingEmailStart..s_arrivalCount-1]
// have not yet been included in a successfully dispatched email.
// Advanced to s_arrivalCount only after startUnikonEmailAsync() returns true.
static uint16_t      s_pendingEmailStart  = 0;
static unsigned long s_lastEmailAttemptMs = 0;

extern HardwareSerial RS232;           // shared RS-232 port (UART2, GPIO 16/17, 9600 baud)
extern volatile bool _rs232PortInitialized;  // UART driver installed flag; false = driver deleted, do not read/write UART
extern int rs232_data_bits;
extern String rs232_parity;
extern int rs232_stop_bits;
extern bool rs232_invert_pins;
extern int rs232_rx_pin;
extern int rs232_tx_pin;
#if ENABLE_RS232 == 1
extern void applyRS232Config();
extern volatile bool _pendingRS232Apply;
#endif

// Forward declaration: defined in screen.cpp / screen.h.
// Called from within the poll busy-wait loops so that the OLED rotation is not
// frozen during the blocking RS232 exchange (which can take up to ~10 s).
#if ENABLE_LCD_SCREEN == 1
extern void updateScreen();
#endif

// Called instead of yield() inside every RS232 busy-wait loop.
// Drives the OLED every ~50 ms so the screen keeps rotating during long polls.
static void unikonYield() {
    yield();
#if ENABLE_LCD_SCREEN == 1
    static uint32_t _lastScreenUpdate = 0;
    uint32_t now = millis();
    if (now - _lastScreenUpdate >= 50) {
        _lastScreenUpdate = now;
        updateScreen();
    }
#endif
}

// Build a hex-dump string from a byte buffer (e.g. "02 81 76 F7 80 03").
static String hexDump(const uint8_t* buf, uint8_t len) {
    String s;
    s.reserve(len * 3);
    char tmp[4];
    for (uint8_t i = 0; i < len; i++) {
        snprintf(tmp, sizeof(tmp), "%02X ", buf[i]);
        s += tmp;
    }
    if (s.length() > 0) s.remove(s.length() - 1); // trim trailing space
    return s;
}

// ----- Config persistence -----

static void loadConfig() {
    // Snapshots are reset before loading persisted contests/registrations.
    s_contestCount = 0;
    s_registrationCount = 0;
    clearContestsStorage();
    clearRegistrationsStorage();
    s_status.contestCount = 0;
    s_status.registeredCount = 0;

    if (!unikonStorageReady()) {
        Serial.println(F("[UNIKON] loadConfig: storage not ready"));
        return;
    }

    if (!littlefsBegin()) {
        Serial.println(F("[UNIKON] loadConfig: littlefsBegin() failed - using defaults"));
        return;
    }
    bool exists = LittleFS.exists(UNIKON_CFG_PATH);
    Serial.printf("[UNIKON] loadConfig: file exists=%d path=%s\n", (int)exists, UNIKON_CFG_PATH);
    if (!exists) return;
    File f = LittleFS.open(UNIKON_CFG_PATH, "r");
    if (!f) {
        Serial.println(F("[UNIKON] loadConfig: LittleFS.open failed"));
        return;
    }
    size_t fileSize = (size_t)f.size();
    if (fileSize == 0 || fileSize > 262144u) {
        Serial.printf("[UNIKON] loadConfig: invalid file size=%u\n", (unsigned)fileSize);
        f.close();
        return;
    }

    // Keep a generous pool to avoid NoMemory in ArduinoJson while parsing arrays.
    // fileSize+margin can be too small because each JSON member consumes slots.
    size_t docCap = (fileSize * 3u) + 4096u;
    if (docCap < 32768u) docCap = 32768u;
    if (docCap > 131072u) docCap = 131072u;

    size_t freeForDoc = psramFound() ? ESP.getFreePsram() : ESP.getFreeHeap();
    if (freeForDoc < (docCap + 8192u)) {
        Serial.printf("[UNIKON] loadConfig: low memory for JSON (need~%u free=%u)\n",
                      (unsigned)docCap, (unsigned)freeForDoc);
        f.close();
        return;
    }
    SpiRamJsonDocument doc(docCap);
    if (doc.capacity() == 0) {
        Serial.println(F("[UNIKON] loadConfig: JSON alloc failed"));
        f.close();
        return;
    }
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err != DeserializationError::Ok) {
        Serial.printf("[UNIKON] loadConfig: deserializeJson failed: %s\n", err.c_str());
        return;
    }
    s_cfg.enabled         = doc["enabled"]  | false;
    s_cfg.pollIntervalSec = doc["interval"] | (uint16_t)180;  // default: 3 minutes
    // Legacy/invalid short intervals are migrated to the default (3 minutes).
    if (s_cfg.pollIntervalSec > 0 && s_cfg.pollIntervalSec < 60)
        s_cfg.pollIntervalSec = 180;
    s_cfg.passiveListen   = doc["passive"]   | false;
    s_cfg.officialFormat  = doc["official_format"] | true;
    s_cfg.manualSerial    = doc["manual_serial"] | false;
    Serial.printf("[UNIKON] loadConfig: loaded enabled=%d interval=%u passive=%d official=%d\n",
                  (int)s_cfg.enabled, (unsigned)s_cfg.pollIntervalSec,
                  (int)s_cfg.passiveListen, (int)s_cfg.officialFormat);

    // Load contests array if present. Use const char* to avoid heap String allocations.
    if (doc.containsKey("contests")) {
        JsonArray contestsArray = doc["contests"].as<JsonArray>();
        s_contestCount = 0;
        clearContestsStorage();
        for (JsonObject obj : contestsArray) {
            if (s_contestCount >= UNIKON_MAX_CONTESTS) break;
            const char* code = obj["code"] | "";
            const char* date = obj["date"] | "";
            const char* name = obj["name"] | "";
            strlcpy(s_contests[s_contestCount].code, code, sizeof(s_contests[s_contestCount].code));
            strlcpy(s_contests[s_contestCount].date, date, sizeof(s_contests[s_contestCount].date));
            strlcpy(s_contests[s_contestCount].name, name, sizeof(s_contests[s_contestCount].name));
            s_contestCount++;
        }
        s_status.contestCount = s_contestCount;
        Serial.printf("[UNIKON] loadConfig: loaded %u contests\n", (unsigned)s_contestCount);
    }

    // Load registrations array if present. Use const char* to avoid heap String allocations.
    if (doc.containsKey("registrations")) {
        JsonArray regsArray = doc["registrations"].as<JsonArray>();
        s_registrationCount = 0;
        clearRegistrationsStorage();
        for (JsonObject obj : regsArray) {
            if (s_registrationCount >= UNIKON_MAX_ARRIVALS) break;
            const char* ring = obj["ring"] | "";
            const char* chip = obj["chip"] | "";
            strlcpy(s_registrations[s_registrationCount].ring, ring, sizeof(s_registrations[s_registrationCount].ring));
            strlcpy(s_registrations[s_registrationCount].chip, chip, sizeof(s_registrations[s_registrationCount].chip));
            s_registrationCount++;
        }
        s_status.registeredCount = s_registrationCount;
        Serial.printf("[UNIKON] loadConfig: loaded %u registrations\n", (unsigned)s_registrationCount);
    }
}

static bool saveConfig() {
    if (!littlefsBegin()) {
        directLogPrintln(F("[UNIKON] saveConfig: littlefsBegin() failed"));
        return false;
    }
    // Ensure /config directory exists (may not exist on a fresh flash)
    if (!LittleFS.exists("/config")) {
        directLogPrintln(F("[UNIKON] saveConfig: creating /config dir"));
        bool mkOk = LittleFS.mkdir("/config");
        if (!mkOk) {
            directLogPrintln(F("[UNIKON] saveConfig: mkdir(/config) failed"));
            return false;
        }
    }
    size_t docCap = 4096u + ((size_t)s_contestCount * 128u) + ((size_t)s_registrationCount * 128u);
    if (docCap < 32768u) docCap = 32768u;
    if (docCap > 131072u) docCap = 131072u;
    SpiRamJsonDocument doc(docCap);
    if (doc.capacity() == 0) {
        directLogPrintln(F("[UNIKON] saveConfig: JSON alloc failed"));
        return false;
    }
    doc["enabled"]  = s_cfg.enabled;
    doc["interval"] = s_cfg.pollIntervalSec;
    doc["passive"]  = s_cfg.passiveListen;
    doc["official_format"] = s_cfg.officialFormat;
    doc["manual_serial"]   = s_cfg.manualSerial;

    JsonArray contestsArray = doc.createNestedArray("contests");
    for (uint16_t i = 0; i < s_contestCount; i++) {
        JsonObject obj = contestsArray.createNestedObject();
        obj["code"] = s_contests[i].code;
        obj["date"] = s_contests[i].date;
        obj["name"] = s_contests[i].name;
    }

    JsonArray regsArray = doc.createNestedArray("registrations");
    for (uint16_t i = 0; i < s_registrationCount; i++) {
        JsonObject obj = regsArray.createNestedObject();
        obj["ring"] = s_registrations[i].ring;
        obj["chip"] = s_registrations[i].chip;
    }

    String tmpPath = String(UNIKON_CFG_PATH) + ".tmp";
    File f = LittleFS.open(tmpPath.c_str(), "w");
    if (!f) {
        directLogPrintln(F("[UNIKON] saveConfig: LittleFS.open(.tmp) failed"));
        return false;
    }
    size_t bytes = serializeJson(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes == 0) {
        directLogPrintln(F("[UNIKON] saveConfig: serializeJson wrote 0 bytes - aborting"));
        LittleFS.remove(tmpPath.c_str());
        return false;
    }
    LittleFS.remove(UNIKON_CFG_PATH);
    bool renamed = LittleFS.rename(tmpPath.c_str(), UNIKON_CFG_PATH);
    if (!renamed) {
        directLogPrintln(F("[UNIKON] saveConfig: rename .tmp -> final FAILED"));
        LittleFS.remove(tmpPath.c_str());
        return false;
    }
    Serial.printf("[UNIKON] saveConfig: saved enabled=%d interval=%u passive=%d official=%d\n",
                  (int)s_cfg.enabled, (unsigned)s_cfg.pollIntervalSec,
                  (int)s_cfg.passiveListen, (int)s_cfg.officialFormat);
    return true;
}

// ----- Serial helpers -----

// Drain any stale bytes before starting a poll.
static void flushRx() {
    uint32_t t = millis();
    while (RS232.available() && millis() - t < 100) RS232.read();
}

// ----- Frame builder -----

// Prebuilt frame content arrays (ADDR + payload only, no STX/ETX/checksums).
// Frame v (identify): 02 81 76 F7 80 03
static const uint8_t CONTENT_V[] = { UNIKON_ADDR, 0x76 };
// Frame X (status/pigeon list):   02 81 58 D9 80 03
static const uint8_t CONTENT_X[] = { UNIKON_ADDR, 0x58 };
// Frame W (race info / registrations): 02 81 57 D8 80 03
static const uint8_t CONTENT_W[] = { UNIKON_ADDR, 0x57 };
// Frame IC0 (contest schedule): 02 81 49 43 30 3D 81 03
static const uint8_t CONTENT_IC[] = { UNIKON_ADDR, 0x49, 0x43, 0x30 };
// Push (device-initiated arrivals). The vendor driver's uniActivatePush()/uniDeactivatePush()
// send exactly these two frames. The RAMBOX must be asked PER DEVICE, so the negotiated state
// is bound to the identification string (version + serial) and re-armed when another one answers.
static const uint8_t CONTENT_P1[] = { UNIKON_ADDR, 0x50, 0x31 };   // push ON  ("P1")
static const uint8_t CONTENT_P0[] = { UNIKON_ADDR, 0x50, 0x30 };   // push OFF ("P0")

enum { UNIKON_PUSH_OFF = 0, UNIKON_PUSH_ACTIVE = 1, UNIKON_PUSH_UNSUPPORTED = 2 };
static uint8_t  s_pushState      = UNIKON_PUSH_OFF;
static int8_t   s_pushPending    = -1;      // -1 idle, 0 = turn off, 1 = turn on
// The push is tied to the polling: as soon as a RAMBOX is identified, the hub asks it for the
// push once (per device) -- no user action. Harmless when unsupported (one NAK, logged).
static bool     s_pushAuto       = true;
static char     s_pushDevice[44] = {0};     // ident the push was negotiated with
static char     s_pushVersion[44] = {0};    // ident captured by the last probe
static uint16_t s_pushCode       = 0;       // last result: 0 = ACK, 0x3f1 = NAK, 0x3ef = timeout

// Called on every successful identification (any session). If the RAMBOX is not the one the
// push was negotiated with, the push state is dropped -- and re-armed when auto mode is on --
// so plugging the hub into another RAMBOX asks again instead of assuming.
static void pushCheckDevice(const char* ident) {
    if (!ident || ident[0] == '\0') return;
    if (s_pushDevice[0] != '\0' && strcmp(s_pushDevice, ident) == 0) return;   // same device
    bool changed = (s_pushDevice[0] != '\0');
    snprintf(s_pushDevice, sizeof(s_pushDevice), "%s", ident);
    if (changed) {
        s_pushState = UNIKON_PUSH_OFF;
        s_pushCode  = 0;
        sendVirtualMessage(F("[UNIKON] push: different RAMBOX detected — push state reset"), WARN, false, true);
    }
    // Nothing else here: the push is asked at the end of the poll that identified this device
    // (see unikonPollNow()), so a device that answers is probed exactly once.
}
static void pushProbe(bool want);        // defined after sendAndWaitAck()

// Send v-frame and wait for the RAMBOX ident frame response.
// Protocol variants seen in the field:
// - NAK, then retry, then ident frame (real RAMBOX units)
// - direct ident frame (no ACK byte first)
// - ACK, then ident frame (Simulator.ino compatibility path)
// Returns true if ident frame received and ACKed, false on timeout.
static bool sendVFrameAndReadIdent(char* moduleIdBuf, size_t bufLen) {
    uint8_t frame[64];
    uint8_t fLen = buildFrame(CONTENT_V, sizeof(CONTENT_V), frame);
    for (uint8_t attempt = 0; attempt < UNIKON_MAX_RETRIES; attempt++) {
        sendVirtualMessage(String(F("[UNIKON] TX [")) + String(attempt+1) + F("]: ") + hexDump(frame, fLen), WARN, false, true);
        RS232.write(frame, fLen);
        RS232.flush();  // ensure all bytes are physically transmitted before waiting for response
        uint32_t t = millis();
        bool gotNak = false;
        bool gotAckBeforeIdent = false;
        while (millis() - t < UNIKON_TIMEOUT_MS) {
            if (!RS232.available()) { unikonYield(); continue; }
            int b = RS232.read();
            char tmp[40];
            snprintf(tmp, sizeof(tmp), "[UNIKON] RX: %02X (%s)", (uint8_t)b,
                     b == UNIKON_NAK ? "NAK" : (b == UNIKON_STX ? "STX(ident)" : (b == UNIKON_ACK ? "ACK(pre-ident)" : "OTHER")));
            sendVirtualMessage(String(tmp), WARN, false, true);
            if (b == UNIKON_ACK) {
                // Simulator.ino sends ACK first, then the ident frame.
                // Treat ACK as a valid intermediate state and restart the timeout window.
                gotAckBeforeIdent = true;
                t = millis();
                continue;
            }
            if (b == UNIKON_NAK) {
                // RAMBOX not ready — but some variants send ident immediately after NAK.
                // Keep listening for UNIKON_POST_NAK_MS before giving up and retrying.
                gotNak = true;
                uint32_t nakT = millis();
                while (millis() - nakT < UNIKON_POST_NAK_MS) {
                    if (!RS232.available()) { unikonYield(); continue; }
                    b = RS232.read();
                    if (b == UNIKON_STX) break;  // ident follows NAK — fall through to STX handler
                    b = 0;  // discard other bytes (extra NAKs, garbage) in post-NAK window
                }
                if (b != UNIKON_STX) break;  // no ident in window — retry with full v-frame
                // b == UNIKON_STX: fall through to STX handler below
            }
            if (b == UNIKON_STX) {
                // RAMBOX sent its ident frame directly (no ACK byte first).
                // Read the rest of the frame and ACK it.
                uint8_t frameBuf[80];
                frameBuf[0] = UNIKON_STX;
                uint8_t idx = 1;
                uint32_t tf = millis();
                while (millis() - tf < UNIKON_TIMEOUT_MS) {
                    if (!RS232.available()) { unikonYield(); continue; }
                    int fb = RS232.read();
                    tf = millis();  // reset per-byte timeout — legacy devices may send bytes slowly
                    if (idx < sizeof(frameBuf)) frameBuf[idx++] = (uint8_t)fb;
                    if (fb == UNIKON_ETX) {
                        sendVirtualMessage(String(F("[UNIKON] RX ident frame: ")) + hexDump(frameBuf, idx), WARN, false, true);
                        // Extract module ID from payload (bytes 2..idx-4).
                        if (moduleIdBuf && bufLen > 1 && idx > 5) {
                            uint8_t payStart = 2;
                            uint8_t payEnd   = idx - 4;
                            uint8_t copyLen  = payEnd > payStart ?
                                               (uint8_t)min((size_t)(payEnd - payStart), bufLen - 1) : 0;
                            if (copyLen > 0) {
                                memcpy(moduleIdBuf, frameBuf + payStart, copyLen);
                                moduleIdBuf[copyLen] = '\0';
                                sendVirtualMessage(String(F("[UNIKON] moduleId: ")) + String(moduleIdBuf), WARN, false, true);
                                pushCheckDevice(moduleIdBuf);   // per-device push state
                            }
                        }
                        RS232.write(UNIKON_ACK);
                        sendVirtualMessage(F("[UNIKON] TX: ACK (ident ack)"), WARN, false, true);
                        return true;
                    }
                }
                sendVirtualMessage(F("[UNIKON] ident frame timeout (incomplete)"), WARN, false, true);
                return false;
            }
        }
        if (gotAckBeforeIdent) {
            sendVirtualMessage(F("[UNIKON] RX: timeout waiting for ident after ACK"), WARN, false, true);
        } else if (!gotNak) {
            sendVirtualMessage(F("[UNIKON] RX: timeout waiting for ident/NAK"), WARN, false, true);
        }
        delay(UNIKON_NAK_RETRY_DELAY);  // give RAMBOX time to transition to ready state before next query
    }
    return false;
}


// Returns true on ACK, false on timeout or repeated NAK.
// If EOT (0x04) is received before ACK, it is saved in s_prefetchedByte so that
// the caller's readFrame() immediately detects "no data" instead of timing out.
// This happens in the W-phase when the RAMBOX has no arrival records with time:
// it sends EOT (end-of-data) followed by ACK (W-frame acknowledged).
static bool sendAndWaitAck(const uint8_t* content, uint8_t contentLen) {
    uint8_t frame[64];
    uint8_t fLen = buildFrame(content, contentLen, frame);
    for (uint8_t attempt = 0; attempt < UNIKON_MAX_RETRIES; attempt++) {
        sendVirtualMessage(String(F("[UNIKON] TX [")) + String(attempt+1) + F("]: ") + hexDump(frame, fLen), WARN, false, true);
        RS232.write(frame, fLen);
        RS232.flush();  // ensure all bytes are physically transmitted before waiting for response
        uint32_t t = millis();
        while (millis() - t < UNIKON_TIMEOUT_MS) {
            if (RS232.available()) {
                int b = RS232.read();
                uint8_t byt = (uint8_t)b;
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "[UNIKON] RX: %02X (%s)",
                         byt,
                         b == UNIKON_ACK ? "ACK" : (b == UNIKON_NAK ? "NAK" : (b == UNIKON_EOT ? "EOT" : "OTHER")));
                sendVirtualMessage(String(tmp), WARN, false, true);
                if (b == UNIKON_ACK) return true;
                if (b == UNIKON_NAK) break;  // NAK: retry
                // EOT before ACK: RAMBOX signals no data frames will follow.
                // Save it so readFrame() can detect EOT immediately instead of timing out.
                if (b == UNIKON_EOT) { s_prefetchedByte = UNIKON_EOT; continue; }
                // STX before ACK: RAMBOX already started sending frames (same as X-phase variant).
                // Save STX so readFrame() can consume it on the next call.
                if (b == UNIKON_STX) { s_prefetchedByte = UNIKON_STX; return true; }
            }
            unikonYield();
        }
        sendVirtualMessage(F("[UNIKON] RX: timeout waiting for ACK/NAK"), WARN, false, true);
        delay(UNIKON_NAK_RETRY_DELAY);
    }
    return false;
}

// Ask the RAMBOX to push ("P1") or stop pushing ("P0") and record the outcome.
// Handshake first so the identification string (version + serial) is captured and the
// push state stays bound to that specific device.
// Result codes mirror the vendor driver: 0 = ACK, 0x3f1 = NAK, 0x3ef = no answer.
static void pushProbe(bool want) {
    char ident[44] = {0};
    if (!sendVFrameAndReadIdent(ident, sizeof(ident))) {
        s_pushState = UNIKON_PUSH_UNSUPPORTED;
        s_pushCode  = 0x3ef;   // timeout / no RAMBOX
        sendVirtualMessage(F("[UNIKON] push: handshake failed — no RAMBOX answered"), WARN, false, true);
        return;
    }
    snprintf(s_pushVersion, sizeof(s_pushVersion), "%s", ident);
    bool acked = sendAndWaitAck(want ? CONTENT_P1 : CONTENT_P0, want ? sizeof(CONTENT_P1) : sizeof(CONTENT_P0));
    s_pushCode = acked ? 0 : 0x3f1;   // sendAndWaitAck() only returns true on ACK
    if (want) {
        s_pushState = acked ? UNIKON_PUSH_ACTIVE : UNIKON_PUSH_UNSUPPORTED;
    } else {
        s_pushState = UNIKON_PUSH_OFF;   // we asked to stop: OFF regardless of the reply
    }
    char msg[120];
    snprintf(msg, sizeof(msg), "[UNIKON] push: %s %s (ident=%s)",
             want ? "P1" : "P0",
             acked ? "ACK" : (s_pushCode == 0x3f1 ? "NAK/rejected" : "no answer"),
             s_pushVersion[0] ? s_pushVersion : "?");
    sendVirtualMessage(String(msg), WARN, false, true);
    if (want && acked) {
        sendVirtualMessage(F("[UNIKON] push: active — pushed frames are read as they arrive (the auto-poll keeps running as the guarantee)"), WARN, false, true);
    } else if (want) {
        sendVirtualMessage(F("[UNIKON] push: not supported by this RAMBOX — polling keeps running as before"), WARN, false, true);
    }
}

// Read one framed response from the RAMBOX (blocks up to UNIKON_TIMEOUT_MS).
// Fills frameBuf[]. Sets *isEOT = true when 0x04 is received instead of a frame.
// Consumes s_prefetchedByte first if set (used when X-frame step already read an STX).
// Returns frame length, or 0 on timeout/error.
static uint8_t readFrame(uint8_t* frameBuf, uint8_t bufLen, bool* isEOT) {
    *isEOT = false;
    uint8_t idx = 0;
    bool inFrame = false;
    uint32_t tFrame = millis();

    // Consume a byte that was pre-read during the X-frame step.
    if (s_prefetchedByte != 0) {
        uint8_t pre = s_prefetchedByte;
        s_prefetchedByte = 0;
        if (pre == UNIKON_EOT) { *isEOT = true; return 0; }
        if (pre == UNIKON_STX) { inFrame = true; frameBuf[idx++] = UNIKON_STX; tFrame = millis(); }
        // Any other pre-fetched byte is discarded (not STX/EOT — shouldn't happen).
    }

    while (millis() - tFrame < UNIKON_TIMEOUT_MS) {
        if (!RS232.available()) { unikonYield(); continue; }
        int b = RS232.read();
        if (!inFrame) {
            if (b == UNIKON_EOT) { *isEOT = true; return 0; }
            if (b == UNIKON_STX) {
                inFrame = true; idx = 0;
                frameBuf[idx++] = (uint8_t)b;
                tFrame = millis();  // reset timeout when frame starts
            }
        } else {
            if (idx < bufLen) frameBuf[idx++] = (uint8_t)b;
            if (b == UNIKON_ETX) return idx;
            tFrame = millis();  // reset timeout for each new byte within frame
        }
    }
    return 0;  // timeout
}

// ----- Frame parsers -----

// Parse ZP (pigeon) frame.
// Verified byte layout (30 bytes total) — confirmed against sniffer capture 11.txt:
//   STX(0) ADDR(1) 'Z'(2) 'P'(3) RING[11](4..14) SP(15) CHIP[11](16..26) CK1(27) CK2(28) ETX(29)
// ZP-frames carry no arrival time (X-operation only returns ring+chip).
// CHIP is "00000000000" (11 zeros) when the pigeon has no transponder.
// Returns true on success.
static bool parseZpFrame(const uint8_t* f, uint8_t len, UnikonArrival* out) {
    if (len < 30) return false;
    if (f[2] != 0x5A || f[3] != 0x50) return false;   // not 'Z', 'P'
    if (f[15] != 0x20) return false;                   // space separator must be present
    memcpy(out->ring, f + 4,  11); out->ring[11] = '\0';
    memcpy(out->chip, f + 16, 11); out->chip[11] = '\0';
    out->hasChip = (out->chip[0] != '0');
    out->isTraining = false;
    out->race[0] = '\0';  // registrations carry no race; set on W-phase P-frames only
    out->arrivalTime[0] = '\0';  // no arrival time in ZP-frames
    out->arrivalDate[0] = '\0';
    return true;
}

// Trim leading and trailing spaces in place.
static void trimSpaces(char* s) {
    if (!s || !*s) return;
    char* start = s;
    while (*start == ' ') start++;
    size_t len = strlen(start);
    if (len == 0) { s[0] = '\0'; return; }
    char* end = start + len - 1;
    while (end > start && *end == ' ') { *end = '\0'; end--; }
    if (start != s) memmove(s, start, strlen(start) + 1);
}

// Parse S1 (club) frame — club name at frame[10..33] (24 bytes).
static void parseS1Frame(const uint8_t* f, uint8_t len, char* out, uint8_t outLen) {
    if (len < 37 || f[2] != 0x53 || f[3] != 0x31) return;
    uint8_t copyLen = (uint8_t)min((int)24, (int)(outLen - 1));
    memcpy(out, f + 10, copyLen);
    out[copyLen] = '\0';
    trimSpaces(out);
}

// Parse S2 (solta) frame — solta name at frame[14..24] (11 bytes).
static void parseS2Frame(const uint8_t* f, uint8_t len, char* out, uint8_t outLen) {
    if (len < 28 || f[2] != 0x53 || f[3] != 0x32) return;
    uint8_t copyLen = (uint8_t)min((int)11, (int)(outLen - 1));
    memcpy(out, f + 14, copyLen);
    out[copyLen] = '\0';
    trimSpaces(out);
}

// Parse S3 (fancier) frame — fancier name at frame[14..].
// clubCode at f[4..10] (7 bytes), fancierSuffix at f[11..13] (3 bytes).
// Name starts at f+14 and extends to (len - 14 - 2 - 1) bytes (before CK+ETX).
// Trailing spaces and zeros (padding) are stripped.
static void parseS3Frame(const uint8_t* f, uint8_t len, char* out, uint8_t outLen) {
    if (len < 35 || f[2] != 0x53 || f[3] != 0x33) return;
    int maxBytes = (int)len - 14 - 2 - 1;
    if (maxBytes < 0) return;
    uint8_t copyLen = (uint8_t)min(maxBytes, (int)(outLen - 1));
    memcpy(out, f + 14, copyLen);
    out[copyLen] = '\0';
    trimSpaces(out);
    // Strip trailing zeros (padding) — names don't end in consecutive zeros
    size_t slen = strlen(out);
    while (slen > 0 && out[slen - 1] == '0') out[--slen] = '\0';
    // Re-strip spaces uncovered by zero removal
    trimSpaces(out);
}

// Parse club code from S2/S3 prefix — 7 ASCII digits at frame[4..10].
static void parseSClubCode(const uint8_t* f, uint8_t len, char* out, uint8_t outLen) {
    if (len < 11 || f[2] != 0x53 || (f[3] != 0x32 && f[3] != 0x33)) return;
    if (outLen < 8) return;
    for (uint8_t i = 0; i < 7; i++) {
        uint8_t c = f[4 + i];
        if (c < '0' || c > '9') return;
        out[i] = (char)c;
    }
    out[7] = '\0';
}

// Parse frame '3' (race info) from W-frame operation.
// Frame layout (83 bytes total):
//   STX ADDR '3' [13-byte ID] [40-byte timestamps] "44" ' ' DATE[5] ' ' NAME[9] PIN[6] CK1 CK2 ETX
// DATE at offset 59 ("DD.MM"), NAME at offset 65 (9 chars right-padded with spaces).
// Verified against real capture 9.txt: "15.06" / "CANAME   " with CK=ee 8f.
// Parse the contest code from frame '3' bytes [9..11] ("001".."044", "090" for training).
// Present in BOTH frame-3 flavours (with and without race data), so it is the
// robust training discriminator. Absent/garbage leaves out empty.
// Verified against captures 9.txt ("001") and 17.txt ("003").
static void parseFrame3Code(const uint8_t* f, uint8_t len, char* out, uint8_t outLen) {
    if (!out || outLen < 2) return;
    out[0] = '\0';
    if (len < 12 || f[2] != 0x33) return;
    // Validate all three digits before committing: a partial code must never
    // leave a short value that could match by accident.
    char tmp[4] = { 0, 0, 0, 0 };
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t c = f[9 + i];
        if (c < '0' || c > '9') return;
        tmp[i] = (char)c;
    }
    strlcpy(out, tmp, outLen);
}

static void parseFrame3(const uint8_t* f, uint8_t len,
                        char* dateBuf, uint8_t dateBufLen,
                        char* nameBuf, uint8_t nameBufLen) {
    if (len < 83 || f[2] != 0x33) return;
    // Race date: 5 chars at offset 59 e.g. "15.06"
    uint8_t copyDate = (uint8_t)min((int)5, (int)(dateBufLen - 1));
    memcpy(dateBuf, f + 59, copyDate);
    dateBuf[copyDate] = '\0';
    // Race name: 9 chars at offset 65 (right-padded with spaces)
    uint8_t copyName = (uint8_t)min((int)9, (int)(nameBufLen - 1));
    memcpy(nameBuf, f + 65, copyName);
    nameBuf[copyName] = '\0';
    trimSpaces(nameBuf);
}

// Parse P-frame (race entry) from W-frame operation.
// Verified byte layout (39 bytes total) — confirmed against sniffer captures 9.txt and 12.txt:
//   STX(0) ADDR(1) 'P'(2) RING[11](3..13) SP(14) CHIP[11](15..25) HHMMSS[6](26..31) DDMM[4](32..35) CK1(36) CK2(37) ETX(38)
// HHMMSS = arrival time in ASCII digits (e.g. "095518" = 09:55:18). "000000" = not yet arrived.
// DDMM   = arrival date in ASCII digits (e.g. "2306"  = 23 June).  "0000"   = not yet arrived.
// Cross-verified against uni.exe output file U3761701.001.
// Returns true on success.
static bool parsePFrame(const uint8_t* f, uint8_t len, UnikonArrival* out) {
    if (len < 39 || f[2] != 0x50) return false;
    if (f[14] != 0x20) return false;                   // space separator must be present
    memcpy(out->ring, f + 3,  11); out->ring[11] = '\0';
    memcpy(out->chip, f + 15, 11); out->chip[11] = '\0';
    out->hasChip = (out->chip[0] != '0');
    out->isTraining = false;
    out->race[0] = '\0';  // filled by the caller from the current frame '3'
    // Arrival time and date from EXTRA bytes.
    memcpy(out->arrivalTime, f + 26, 6); out->arrivalTime[6] = '\0';
    memcpy(out->arrivalDate, f + 32, 4); out->arrivalDate[4] = '\0';
    return true;
}

// Parse I-frame (contest schedule) from IC0 session.
// Verified byte layout (22 bytes total) — confirmed against sniffer captures 1.txt and 6.txt:
//   STX(0) ADDR(1) 'I'(2) CODE[3](3..5) DDMM[4](6..9) NAME[9](10..18) CK1(19) CK2(20) ETX(21)
static bool parseIFrame(const uint8_t* f, uint8_t len, UnikonContest* out) {
    if (len < 22 || f[2] != 0x49) return false;
    memcpy(out->code, f + 3, 3); out->code[3] = '\0';
    memcpy(out->date, f + 6, 4); out->date[4] = '\0';
    memcpy(out->name, f + 10, 9); out->name[9] = '\0';
    trimSpaces(out->name);
    return true;
}

// ----- Known-set management -----

static int16_t findArrivalIndexByRing(const char* ring) {
    if (!unikonStorageReady()) return -1;
    for (uint16_t i = 0; i < s_arrivalCount; i++) {
        if (memcmp(s_arrivals[i].ring, ring, 12) == 0) return (int16_t)i;
    }
    return -1;
}

static bool isArrivalKnown(const char* ring) {
    return findArrivalIndexByRing(ring) >= 0;
}

static bool isRegistered(const char* ring) {
    if (!unikonStorageReady()) return false;
    for (uint16_t i = 0; i < s_registrationCount; i++) {
        if (memcmp(s_registrations[i].ring, ring, 12) == 0) return true;
    }
    return false;
}

static bool addArrival(const UnikonArrival& a) {
    if (!unikonStorageReady()) return false;
    if (s_arrivalCount >= UNIKON_MAX_ARRIVALS) return false;
    s_arrivals[s_arrivalCount++] = a;
    return true;
}

static bool addRegistration(const UnikonArrival& a) {
    if (!unikonStorageReady()) return false;
    if (s_registrationCount >= UNIKON_MAX_ARRIVALS) return false;
    s_registrations[s_registrationCount++] = a;
    return true;
}

static bool isContestKnown(const UnikonContest& c) {
    for (uint16_t i = 0; i < s_contestCount; i++) {
        if (memcmp(s_contests[i].code, c.code, sizeof(c.code)) == 0 &&
            memcmp(s_contests[i].date, c.date, sizeof(c.date)) == 0 &&
            memcmp(s_contests[i].name, c.name, sizeof(c.name)) == 0) {
            return true;
        }
    }
    return false;
}

static bool addContest(const UnikonContest& c) {
    if (s_contestCount >= UNIKON_MAX_CONTESTS) return false;
    s_contests[s_contestCount++] = c;
    return true;
}

// Find an existing arrival by ring number and update its time/date fields.
// Called from W-phase when a P-frame arrives for a ring already registered via ZP-frame.
// Returns true if the entry was found and updated.
static bool updateArrivalTime(const UnikonArrival& src) {
    int16_t idx = findArrivalIndexByRing(src.ring);
    if (idx >= 0) {
        memcpy(s_arrivals[idx].arrivalTime, src.arrivalTime, 7);
        memcpy(s_arrivals[idx].arrivalDate, src.arrivalDate, 5);
        s_arrivals[idx].isTraining = false;
        return true;
    }
    return false;
}

static bool updateTrainingEntry(const UnikonArrival& src) {
    int16_t idx = findArrivalIndexByRing(src.ring);
    if (idx < 0) return false;

    // Keep race arrivals as race; only mark training when there is no confirmed time.
    bool hasRaceTime = (s_arrivals[idx].arrivalTime[0] != '\0' &&
                        memcmp(s_arrivals[idx].arrivalTime, "000000", 6) != 0);
    if (!hasRaceTime) {
        memcpy(s_arrivals[idx].chip, src.chip, 12);
        s_arrivals[idx].hasChip = src.hasChip;
        s_arrivals[idx].isTraining = true;
        memcpy(s_arrivals[idx].arrivalTime, src.arrivalTime, 7);
        memcpy(s_arrivals[idx].arrivalDate, src.arrivalDate, 5);
        memcpy(s_arrivals[idx].race, src.race, sizeof(s_arrivals[idx].race));
        return true;
    }
    return false;
}

static bool hasValidTime(const UnikonArrival& a) {
    return a.arrivalTime[0] != '\0' && memcmp(a.arrivalTime, "000000", 6) != 0;
}

static void buildDisplayRing(const char* rawRing, char* out, size_t outSize) {
    if (s_cfg.officialFormat) {
        unikonFormatDisplayRing(rawRing, out, outSize);
    } else if (out && outSize > 0) {
        snprintf(out, outSize, "%s", rawRing ? rawRing : "");
    }
}

static void buildDisplayChip(const UnikonArrival& arrival, char* out, size_t outSize) {
    if (s_cfg.officialFormat) {
        unikonFormatDisplayChip(arrival.chip, arrival.hasChip, out, outSize);
    } else if (out && outSize > 0) {
        snprintf(out, outSize, "%s", arrival.hasChip ? arrival.chip : "");
    }
}

static void formatPombalChipForAscii(const UnikonArrival& reg, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!reg.hasChip) {
        snprintf(out, outSize, "-");
        return;
    }

    char chipDisplay[16];
    buildDisplayChip(reg, chipDisplay, sizeof(chipDisplay));
    if (chipDisplay[0] == '\0') {
        snprintf(out, outSize, "-");
        return;
    }
    sanitizeAsciiText(chipDisplay, out, outSize);
}

// One cell of the ASCII table: "| text... |" (width is computed from the widest
// text of the column, so every line has exactly the same length).
static void ukAppendCell(String& out, const char* s, uint8_t width) {
    size_t n = s ? strlen(s) : 0;
    if (n > width) n = width;                    // never let a value widen the row
    out += ' ';
    for (size_t i = 0; i < n; i++) out += s[i];
    for (size_t i = n; i < width; i++) out += ' ';
    out += ' ';
    out += '|';
}

static void appendArrivalSectionEmail(String& body, const char* title, bool trainingOnly,
                                      uint16_t fromIdx, uint16_t toIdx) {
    // Phone-friendly table: No / Ring / Chip / Time only. Date, Race, Pos and Eval
    // were dropped because the 8-column ASCII table wrapped and was unreadable on a
    // smartphone (the race is already named in the header lines of the email).
    const char* HDR[4] = { "No", rmbT("Anilha", "Ring"), "Chip", rmbT("Hora", "Time") };
    uint8_t w[4];
    for (uint8_t c = 0; c < 4; c++) w[c] = (uint8_t)strlen(HDR[c]);
    if (w[1] < 11) w[1] = 11;                    // 11 digits is the normal ring
    if (w[3] < 8)  w[3] = 8;                     // HH:MM:SS

    // ---- pass 1: measure header + every value that will be printed ----
    for (uint16_t i = fromIdx; i < toIdx; i++) {
        if ((bool)s_arrivals[i].isTraining != trainingOnly) continue;
        char b[24];
        int n = snprintf(b, sizeof(b), "%u.", (unsigned)(i + 1));
        if (n > (int)w[0]) w[0] = (uint8_t)n;

        char ringDisplay[16];
        buildDisplayRing(s_arrivals[i].ring, ringDisplay, sizeof(ringDisplay));
        n = (int)strlen(ringDisplay);
        if (n > (int)w[1]) w[1] = (uint8_t)n;

        n = 0;
        if (s_arrivals[i].hasChip) {
            char chipDisplay[16];
            buildDisplayChip(s_arrivals[i], chipDisplay, sizeof(chipDisplay));
            n = (int)strlen(chipDisplay);
        }
        if (n > (int)w[2]) w[2] = (uint8_t)n;
    }
    if (w[1] > 15) w[1] = 15;                    // sane caps
    if (w[2] > 11) w[2] = 11;

    // ---- separator: '+' (width+2) '-' ... '+' ----
    String sep;
    sep.reserve(40);
    sep += '+';
    for (uint8_t c = 0; c < 4; c++) {
        for (uint8_t k = 0; k < (uint8_t)(w[c] + 2); k++) sep += '-';
        sep += '+';
    }
    sep += '\n';

    String hdr;
    hdr.reserve(60);
    hdr += '|';
    for (uint8_t c = 0; c < 4; c++) ukAppendCell(hdr, HDR[c], w[c]);
    hdr += '\n';

    String section;
    section.reserve(120 + (uint16_t)(toIdx - fromIdx) * 52u);
    section += sep;
    section += hdr;
    section += sep;

    uint16_t rowCount = 0;
    for (uint16_t i = fromIdx; i < toIdx; i++) {
        if ((bool)s_arrivals[i].isTraining != trainingOnly) continue;
        rowCount++;

        // Time: stored as "HHMMSS", displayed as "HH:MM:SS" (8 chars).
        char timeStr[10] = "        ";
        if (hasValidTime(s_arrivals[i])) {
            const char* t = s_arrivals[i].arrivalTime;
            snprintf(timeStr, sizeof(timeStr), "%c%c:%c%c:%c%c",
                     t[0], t[1], t[2], t[3], t[4], t[5]);
        }

        char ringDisplay[16];
        buildDisplayRing(s_arrivals[i].ring, ringDisplay, sizeof(ringDisplay));

        char chipStr[16] = "";
        if (s_arrivals[i].hasChip) {
            buildDisplayChip(s_arrivals[i], chipStr, sizeof(chipStr));
        }

        char idxStr[10];
        snprintf(idxStr, sizeof(idxStr), "%u.", (unsigned)(i + 1));

        section += '|';
        ukAppendCell(section, idxStr, w[0]);
        ukAppendCell(section, ringDisplay, w[1]);
        ukAppendCell(section, chipStr, w[2]);
        ukAppendCell(section, timeStr, w[3]);
        section += '\n';
    }

    if (rowCount > 0) {
        section += sep;
        body = String(title) + String(F("\n")) + section + body;
    }
}

static void appendRegistrationsSectionEmail(String& body) {
    uint16_t regCount = s_registrationCount;
    if (regCount == 0) return;
    
    body += String(rmbT("Pombal da \u00e9poca (ZP registados) \u2014 ", "Season loft (ZP registered) \u2014 "))
          + String(regCount)
          + String((regCount == 1) ? rmbT(" pombo\n", " pigeon\n") : rmbT(" pombos\n", " pigeons\n"));
    for (uint16_t i = 0; i < regCount; i++) {
        char numBuf[6];
        char ringDisplay[16];
        buildDisplayRing(s_registrations[i].ring, ringDisplay, sizeof(ringDisplay));
        snprintf(numBuf, sizeof(numBuf), "%3u. ", (unsigned)(i + 1));
        body += String(numBuf);
        body += String(rmbT("Anilha: ", "Ring: ")) + String(ringDisplay);
        if (s_registrations[i].hasChip) {
            char chipDisplay[16];
            buildDisplayChip(s_registrations[i], chipDisplay, sizeof(chipDisplay));
            body += String(F("  Chip: ")) + String(chipDisplay);
        }
        body += String(F("\n"));
    }
    body += String(F("\n"));
}

// ----- W-phase: fetch race info (date + name) via W-frame -----
// Sends v-frame (handshake) + W-frame, reads frame '3' (extracts race date/name),
// then drains all P-frames until EOT or timeout.
// Called once per session when raceDate is still unknown.
// Uses its own v-frame handshake — independent session from the X-frame phase.
static bool runWPhase() {
    char tmpModuleId[44];
    // Drain any residual bytes left in RX from the X-phase before starting a
    // new independent v-frame session. Without this, stale bytes (e.g. the
    // tail of the last ZP-frame or UART FIFO artefacts) can confuse
    // sendVFrameAndReadIdent() and cause the W-phase to silently skip,
    // leaving all arrival times as empty.
    delay(50);
    flushRx();
    // Stale race identity from a previous poll must not leak into this one:
    // frame '3' below re-establishes it (possibly per block).
    s_status.raceName[0] = '\0';
    s_status.raceDate[0] = '\0';
    s_status.raceCode[0] = '\0';
    sendVirtualMessage(F("[UNIKON] W-phase: sending v-frame (identify)"), WARN, false, true);
    if (!sendVFrameAndReadIdent(tmpModuleId, sizeof(tmpModuleId))) {
        sendVirtualMessage(F("[UNIKON] W-phase: no ident — skipping race info"), WARN, false, true);
        return false;
    }

    // Send W-frame and wait for ACK from RAMBOX.
    sendVirtualMessage(F("[UNIKON] W-phase: sending W-frame (race info)"), WARN, false, true);
    if (!sendAndWaitAck(CONTENT_W, sizeof(CONTENT_W))) {
        sendVirtualMessage(F("[UNIKON] W-phase: no ACK to W-frame — skipping race info"), WARN, false, true);
        return false;
    }

    // Read frames: first '3' (race date/name), then P-frames (registrations), until EOT/timeout.
    uint8_t frameBuf[128];
    bool gotRaceInfo = false;
    s_inTrainingRace = false;  // re-decided at every frame '3' below
    uint8_t pFrameCount = 0;
    size_t regsBytes = (size_t)UNIKON_MAX_ARRIVALS * sizeof(UnikonArrival);
    UnikonArrival* wRegistrations = (UnikonArrival*)(psramFound() ? ps_malloc(regsBytes) : malloc(regsBytes));
    bool canStoreWRegistrations = (wRegistrations != NULL);
    if (!canStoreWRegistrations) {
        sendVirtualMessage(F("[UNIKON] W-phase: no memory for registration snapshot buffer"), WARN, false, true);
    }
    uint16_t wRegistrationCount = 0;
    bool gotWRegistrationSnapshot = false;
    while (true) {
        bool isEOT = false;
        uint8_t fLen = readFrame(frameBuf, sizeof(frameBuf), &isEOT);
        if (isEOT) {
            char dbg[48];
            snprintf(dbg, sizeof(dbg), "[UNIKON] W-phase: EOT — got frame3=%d P-frames=%u",
                     (int)gotRaceInfo, pFrameCount);
            sendVirtualMessage(String(dbg), WARN, false, true);
            break;
        }
        if (fLen == 0) {
            sendVirtualMessage(F("[UNIKON] W-phase: timeout waiting for frame"), WARN, false, true);
            break;
        }

        RS232.write(UNIKON_ACK);

        if (fLen >= 3 && frameBuf[2] == 0x33) {
            // Frame '3': race info with date and name — dump full frame for cross-reference.
            {
                uint8_t dumpLen = fLen < 24 ? fLen : 24;
                char f3raw[80];
                int pos = snprintf(f3raw, sizeof(f3raw), "[UNIKON] W frame3 raw len=%u bytes=", fLen);
                for (uint8_t i = 0; i < dumpLen && pos + 3 < (int)sizeof(f3raw); i++)
                    pos += snprintf(f3raw + pos, sizeof(f3raw) - pos, "%02X ", frameBuf[i]);
                sendVirtualMessage(String(f3raw), WARN, false, true);
            }
            parseFrame3(frameBuf, fLen,
                        s_status.raceDate, sizeof(s_status.raceDate),
                        s_status.raceName, sizeof(s_status.raceName));
            parseFrame3Code(frameBuf, fLen, s_status.raceCode, sizeof(s_status.raceCode));
            gotRaceInfo = true;
            // Training when the frame's contest code is the fixed training code
            // (robust: present in both frame-3 flavours, no persistence needed).
            s_inTrainingRace = isTrainingContestCode(s_status.raceCode, UNIKON_TRAINING_CODE);
            char msg[80];
            snprintf(msg, sizeof(msg), "[UNIKON] W-phase: race='%s' code='%s' date='%s' len=%u%s",
                     s_status.raceName, s_status.raceCode, s_status.raceDate, fLen,
                     s_inTrainingRace ? " (TRAINING)" : "");
            sendVirtualMessage(String(msg), WARN, false, true);
        } else if (fLen >= 39 && frameBuf[2] == 0x50) {
            // P-frame: race registration record.
            // EXTRA bytes 26-35 carry the arrival time (HHMMSS) and date (DDMM).
            // "000000" HHMMSS means the pigeon has not yet arrived — skip it.
            UnikonArrival arrival;
            if (parsePFrame(frameBuf, fLen, &arrival)) {
                pFrameCount++;
                // Log raw time/date bytes for diagnosis.
                char raw[32];
                snprintf(raw, sizeof(raw), "[UNIKON] P-frame ring=%.11s t=%.6s d=%.4s",
                         arrival.ring, arrival.arrivalTime, arrival.arrivalDate);
                sendVirtualMessage(String(raw), WARN, false, true);
                bool arrived = (arrival.arrivalTime[0] != '\0' && memcmp(arrival.arrivalTime, "000000", 6) != 0);
                if (arrived) {
                    if (s_inTrainingRace) arrival.isTraining = true;
                    // Race tag for the arrival: name when the frame carried one,
                    // else the contest code (short frame '3' has code only).
                    if (s_status.raceName[0] != '\0') {
                        strlcpy(arrival.race, s_status.raceName, sizeof(arrival.race));
                    } else {
                        strlcpy(arrival.race, s_status.raceCode, sizeof(arrival.race));
                    }
                    bool alreadyKnown = isArrivalKnown(arrival.ring);
                    if (alreadyKnown) {
                        // Arrival already cached from a previous W-phase.
                        // Training arrivals refresh via updateTrainingEntry (race time kept on conflict).
                        bool updated = s_inTrainingRace ? updateTrainingEntry(arrival) : updateArrivalTime(arrival);
                        char dbg2[64];
                        snprintf(dbg2, sizeof(dbg2), "[UNIKON] updateTime ring=%.11s %s t=%.6s%s",
                                 arrival.ring, updated ? "OK" : "NOT FOUND", arrival.arrivalTime,
                                 s_inTrainingRace ? " (TRAINING)" : "");
                        sendVirtualMessage(String(dbg2), WARN, false, true);
                    } else {
                        // New arrival not yet seen in X-phase — add the full record.
                        addArrival(arrival);
                        s_status.totalKnown++;
                        s_status.lastNewCount++;
                    }
                    extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);
                    char line[56];
                    // Format: "CHEGADA RING CHIP HH:MM:SS" or "CHEGADA RING HH:MM:SS" (no chip)
                    const char* t = arrival.arrivalTime;  // HHMMSS
                    if (arrival.hasChip) {
                        snprintf(line, sizeof(line), "CHEGADA %s %s %c%c:%c%c:%c%c\r\n",
                                 arrival.ring, arrival.chip,
                                 t[0],t[1], t[2],t[3], t[4],t[5]);
                    } else {
                        snprintf(line, sizeof(line), "CHEGADA %s %c%c:%c%c:%c%c\r\n",
                                 arrival.ring,
                                 t[0],t[1], t[2],t[3], t[4],t[5]);
                    }
                    if (!alreadyKnown) injectDataToRXBuffer(String(line), String(F("UNIKON")));
                    char dbg[64];
                    snprintf(dbg, sizeof(dbg), "[UNIKON] W-phase arrival: %s at %c%c:%c%c:%c%c",
                             arrival.ring, t[0],t[1], t[2],t[3], t[4],t[5]);
                    sendVirtualMessage(String(dbg), WARN, false, true);
                }
            }
        } else if (fLen >= 30 && frameBuf[2] == 0x5A && frameBuf[3] == 0x50) {
            // ZP in W-phase represents the RAMBOX registration snapshot (all pigeons
            // registered for the fancier), not necessarily the latest training events.
            // Keep it in a temporary snapshot and replace the pombal set at the end.
            // Do NOT add to arrivals/new counters.
            UnikonArrival reg;
            if (parseZpFrame(frameBuf, fLen, &reg)) {
                bool exists = false;
                bool isNewReg = false;
                if (canStoreWRegistrations) {
                    gotWRegistrationSnapshot = true;
                    for (uint16_t i = 0; i < wRegistrationCount; i++) {
                        if (memcmp(wRegistrations[i].ring, reg.ring, 12) == 0) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists && wRegistrationCount < UNIKON_MAX_ARRIVALS) {
                        wRegistrations[wRegistrationCount++] = reg;
                        isNewReg = true;
                    }
                }

                char tmsg[88];
                snprintf(tmsg, sizeof(tmsg),
                         "[UNIKON] W-phase registration: ring=%.11s chip=%.11s %s",
                         reg.ring, reg.chip, isNewReg ? "SNAPSHOT" : "DUP");
                sendVirtualMessage(String(tmsg), WARN, false, true);
            }
        } else if (fLen >= 4 && frameBuf[2] == 0x53) {
            // Some RAMBOX units include S1/S2/S3 metadata in W-phase as well.
            if (frameBuf[3] == 0x31) {
                parseS1Frame(frameBuf, fLen, s_status.clubName, sizeof(s_status.clubName));
                cacheSFrame(frameBuf, fLen);
            } else if (frameBuf[3] == 0x32) {
                parseS2Frame(frameBuf, fLen, s_status.soltaName, sizeof(s_status.soltaName));
                parseSClubCode(frameBuf, fLen, s_status.clubCode, sizeof(s_status.clubCode));
                cacheSFrame(frameBuf, fLen);
            } else if (frameBuf[3] == 0x33) {
                parseS3Frame(frameBuf, fLen, s_status.fancierName, sizeof(s_status.fancierName));
                parseSClubCode(frameBuf, fLen, s_status.clubCode, sizeof(s_status.clubCode));
                cacheSFrame(frameBuf, fLen);
            }
            char smsg[96];
            snprintf(smsg, sizeof(smsg), "[UNIKON] W S-frame type=%02X club='%.24s' code='%.7s' fancier='%.24s'",
                     frameBuf[3], s_status.clubName, s_status.clubCode, s_status.fancierName);
            sendVirtualMessage(String(smsg), WARN, false, true);
        } else {
            // Unknown frame type — dump the FULL frame (up to 48 bytes) in two log lines.
            // This is the key diagnostic for training records and other undocumented frame types.
            uint8_t half = fLen < 24 ? fLen : 24;
            char wukn1[88], wukn2[88];
            int p1 = snprintf(wukn1, sizeof(wukn1), "[UNIKON] W unknown type=%02X len=%u hex[0..%u]=",
                              fLen >= 3 ? frameBuf[2] : 0x00, fLen, (unsigned)(half - 1));
            for (uint8_t i = 0; i < half && p1 + 3 < (int)sizeof(wukn1); i++)
                p1 += snprintf(wukn1 + p1, sizeof(wukn1) - p1, "%02X ", frameBuf[i]);
            sendVirtualMessage(String(wukn1), WARN, false, true);
            if (fLen > 24) {
                // Second half — bytes [24..end]
                int p2 = snprintf(wukn2, sizeof(wukn2), "[UNIKON] W unknown type=%02X hex[24..%u]=",
                                  frameBuf[2], (unsigned)(fLen - 1));
                for (uint8_t i = 24; i < fLen && p2 + 3 < (int)sizeof(wukn2); i++)
                    p2 += snprintf(wukn2 + p2, sizeof(wukn2) - p2, "%02X ", frameBuf[i]);
                sendVirtualMessage(String(wukn2), WARN, false, true);
            }
            // Also dump as ASCII to help identify string fields (ring/chip/name).
            char wasc[52];
            int pa = snprintf(wasc, sizeof(wasc), "[UNIKON] W unknown ascii=");
            for (uint8_t i = 0; i < fLen && pa + 2 < (int)sizeof(wasc); i++) {
                uint8_t c = frameBuf[i];
                wasc[pa++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
            }
            wasc[pa] = '\0';
            sendVirtualMessage(String(wasc), WARN, false, true);
        }
    }
    if (!gotRaceInfo) {
        sendVirtualMessage(F("[UNIKON] W-phase: no frame '3' received"), WARN, false, true);
    }

    // Replace pombal with the latest W-phase snapshot when ZP records are present.
    if (gotWRegistrationSnapshot && canStoreWRegistrations) {
        clearRegistrationsStorage();
        s_registrationCount = wRegistrationCount;
        if (wRegistrationCount > 0 && s_registrations) {
            memcpy(s_registrations, wRegistrations, wRegistrationCount * sizeof(UnikonArrival));
        }
        s_status.registeredCount = s_registrationCount;

        char msg[72];
        snprintf(msg, sizeof(msg), "[UNIKON] W-phase: pombal replaced by snapshot (%u)",
                 (unsigned)s_registrationCount);
        sendVirtualMessage(String(msg), WARN, false, true);
    }

    if (wRegistrations) free(wRegistrations);

    return true;
}

// ----- X-phase: fetch registered pigeons and club metadata -----
// Sends v-frame + X-frame, then reads ZP/S frames until EOT.
static bool runXPhase() {
    // Step 1: Handshake — send v-frame, RAMBOX replies with ident frame.
    sendVirtualMessage(F("[UNIKON] Step 1: sending v-frame (identify)"), WARN, false, true);
    if (!sendVFrameAndReadIdent(s_status.moduleId, sizeof(s_status.moduleId))) {
        directLogPrintln("Unikon: no ident frame from RAMBOX");
        sendVirtualMessage(F("[UNIKON] FAIL: no ident frame — check cable & baudrate"), WARN, false, true);
        return false;
    }
    sendVirtualMessage(F("[UNIKON] Step 1: ident OK"), WARN, false, true);

    // Step 2: Send X-frame and wait for ACK/STX.
    sendVirtualMessage(F("[UNIKON] Step 2: sending X-frame (status)"), WARN, false, true);
    {
        uint8_t xFrame[64];
        uint8_t xLen = buildFrame(CONTENT_X, sizeof(CONTENT_X), xFrame);
        bool xDone = false;
        for (uint8_t attempt = 0; attempt < UNIKON_MAX_RETRIES && !xDone; attempt++) {
            sendVirtualMessage(String(F("[UNIKON] TX [")) + String(attempt + 1) + F("]: ") + hexDump(xFrame, xLen), WARN, false, true);
            RS232.write(xFrame, xLen);
            RS232.flush();
            uint32_t t = millis();
            while (millis() - t < UNIKON_TIMEOUT_MS) {
                if (!RS232.available()) { unikonYield(); continue; }
                int b = RS232.read();
                char tmp[40];
                snprintf(tmp, sizeof(tmp), "[UNIKON] RX: %02X (%s)", (uint8_t)b,
                         b == UNIKON_ACK ? "ACK" : (b == UNIKON_STX ? "STX(frames-early)" : (b == UNIKON_NAK ? "NAK" : "OTHER")));
                sendVirtualMessage(String(tmp), WARN, false, true);
                if (b == UNIKON_ACK) { xDone = true; break; }
                if (b == UNIKON_STX) {
                    // RAMBOX skipped X-frame ACK and already started sending S/ZP frames.
                    s_prefetchedByte = UNIKON_STX;
                    xDone = true;
                    break;
                }
                if (b == UNIKON_NAK) break;
            }
            if (!xDone) delay(30);
        }
        if (!xDone) {
            directLogPrintln("Unikon: no ACK to X-frame");
            sendVirtualMessage(F("[UNIKON] FAIL: no ACK to X-frame"), WARN, false, true);
            return false;
        }
    }
    sendVirtualMessage(F("[UNIKON] Step 2: ok — reading pigeon frames"), WARN, false, true);

    clearRegistrationsStorage();
    s_registrationCount = 0;
    s_status.registeredCount = 0;

    uint8_t frameBuf[128];
    uint16_t rxCount = 0;
    while (true) {
        bool isEOT = false;
        uint8_t fLen = readFrame(frameBuf, sizeof(frameBuf), &isEOT);
        if (isEOT) break;
        if (fLen == 0) {
            directLogPrintln("Unikon: frame timeout while reading list");
            break;
        }

        RS232.write(UNIKON_ACK);

        if (fLen >= 4 && frameBuf[2] == 0x5A && frameBuf[3] == 0x50) {
            UnikonArrival arrival;
            if (parseZpFrame(frameBuf, fLen, &arrival)) {
                rxCount++;
                bool isNew = !isRegistered(arrival.ring);
                if (isNew) addRegistration(arrival);
                s_status.registeredCount = s_registrationCount;
                char zpdbg[48];
                snprintf(zpdbg, sizeof(zpdbg), "[UNIKON] ZP #%u ring=%.11s chip=%.11s %s",
                         rxCount, arrival.ring, arrival.chip, isNew ? "NEW" : "DUP");
                sendVirtualMessage(String(zpdbg), WARN, false, true);
            } else {
                uint8_t dumpLen = fLen < 12 ? fLen : 12;
                char zpfail[56];
                int pos = snprintf(zpfail, sizeof(zpfail), "[UNIKON] ZP-frame parse FAIL len=%u bytes=", fLen);
                for (uint8_t i = 0; i < dumpLen && pos + 3 < (int)sizeof(zpfail); i++)
                    pos += snprintf(zpfail + pos, sizeof(zpfail) - pos, "%02X ", frameBuf[i]);
                sendVirtualMessage(String(zpfail), WARN, false, true);
            }
        } else if (fLen >= 4 && frameBuf[2] == 0x53) {
            if (frameBuf[3] == 0x31) {
                parseS1Frame(frameBuf, fLen, s_status.clubName, sizeof(s_status.clubName));
                cacheSFrame(frameBuf, fLen);
                char smsg[48];
                snprintf(smsg, sizeof(smsg), "[UNIKON] X S1-frame: club=%.24s", s_status.clubName);
                sendVirtualMessage(String(smsg), WARN, false, true);
            } else if (frameBuf[3] == 0x32) {
                parseS2Frame(frameBuf, fLen, s_status.soltaName, sizeof(s_status.soltaName));
                parseSClubCode(frameBuf, fLen, s_status.clubCode, sizeof(s_status.clubCode));
                cacheSFrame(frameBuf, fLen);
                char smsg[48];
                snprintf(smsg, sizeof(smsg), "[UNIKON] X S2-frame: solta=%.24s", s_status.soltaName);
                sendVirtualMessage(String(smsg), WARN, false, true);
            } else if (frameBuf[3] == 0x33) {
                parseS3Frame(frameBuf, fLen, s_status.fancierName, sizeof(s_status.fancierName));
                parseSClubCode(frameBuf, fLen, s_status.clubCode, sizeof(s_status.clubCode));
                cacheSFrame(frameBuf, fLen);
                char smsg[64];
                snprintf(smsg, sizeof(smsg), "[UNIKON] X S3-frame: fancier=%.31s", s_status.fancierName);
                sendVirtualMessage(String(smsg), WARN, false, true);
            } else {
                uint8_t dumpLen = fLen < 12 ? fLen : 12;
                char sunknown[56];
                int pos = snprintf(sunknown, sizeof(sunknown), "[UNIKON] X S-frame subtype=%02X len=%u bytes=", frameBuf[3], fLen);
                for (uint8_t i = 0; i < dumpLen && pos + 3 < (int)sizeof(sunknown); i++)
                    pos += snprintf(sunknown + pos, sizeof(sunknown) - pos, "%02X ", frameBuf[i]);
                sendVirtualMessage(String(sunknown), WARN, false, true);
            }
        } else {
            uint8_t dumpLen = fLen < 16 ? fLen : 16;
            char xukn[72];
            int pos = snprintf(xukn, sizeof(xukn), "[UNIKON] X unknown type=%02X len=%u bytes=",
                               fLen >= 3 ? frameBuf[2] : 0x00, fLen);
            for (uint8_t i = 0; i < dumpLen && pos + 3 < (int)sizeof(xukn); i++)
                pos += snprintf(xukn + pos, sizeof(xukn) - pos, "%02X ", frameBuf[i]);
            sendVirtualMessage(String(xukn), WARN, false, true);
        }
    }

    {
        char xsum[64];
        snprintf(xsum, sizeof(xsum), "[UNIKON] X-phase done: rxCount=%u registered=%u club='%s'",
                 rxCount, (unsigned)s_registrationCount, s_status.clubName[0] ? s_status.clubName : "(none)");
        sendVirtualMessage(String(xsum), WARN, false, true);
    }

    s_status.connected = true;
    s_status.totalKnown = s_arrivalCount;
    s_status.lastNewCount = 0;
    s_status.registeredCount = s_registrationCount;

    // If RAMBOX returned an empty set while we had known arrivals, a new race likely started.
    if (rxCount == 0 && s_arrivalCount > 0) {
        directLogPrintln("Unikon: RAMBOX empty — resetting known set (new race)");
        uint16_t savedEmailStart = s_pendingEmailStart;
        unsigned long savedEmailAttemptMs = s_lastEmailAttemptMs;
        unikonResetState();
        s_pendingEmailStart = savedEmailStart;
        s_lastEmailAttemptMs = savedEmailAttemptMs;
    }

    return true;
}

// ----- IC-phase: fetch contest schedule via IC0-frame -----
// Sends v-frame (handshake) + IC0-frame, reads all I-frames until EOT.
// The resulting list matches the contest/session overview returned by the
// original Unikon software.
static void runIcPhase() {
    char tmpModuleId[44];
    delay(50);
    flushRx();
    clearContestsStorage();
    s_contestCount = 0;
    s_status.contestCount = 0;

    sendVirtualMessage(F("[UNIKON] IC-phase: sending v-frame (identify)"), WARN, false, true);
    if (!sendVFrameAndReadIdent(tmpModuleId, sizeof(tmpModuleId))) {
        sendVirtualMessage(F("[UNIKON] IC-phase: no ident — skipping contest list"), WARN, false, true);
        return;
    }

    sendVirtualMessage(F("[UNIKON] IC-phase: sending IC0-frame (contest list)"), WARN, false, true);
    if (!sendAndWaitAck(CONTENT_IC, sizeof(CONTENT_IC))) {
        sendVirtualMessage(F("[UNIKON] IC-phase: no ACK to IC0-frame — skipping contest list"), WARN, false, true);
        return;
    }

    uint8_t frameBuf[128];  // must fit frame '3' (83 bytes) as well as standard I-frames
    while (true) {
        bool isEOT = false;
        uint8_t fLen = readFrame(frameBuf, sizeof(frameBuf), &isEOT);
        if (isEOT) {
            char dbg[56];
            snprintf(dbg, sizeof(dbg), "[UNIKON] IC-phase: EOT — contests=%u", (unsigned)s_contestCount);
            sendVirtualMessage(String(dbg), WARN, false, true);
            break;
        }
        if (fLen == 0) {
            sendVirtualMessage(F("[UNIKON] IC-phase: timeout waiting for I-frame"), WARN, false, true);
            break;
        }

        RS232.write(UNIKON_ACK);

        if (fLen >= 22 && frameBuf[2] == 0x49) {
            // Standard I-frame (confirmed against sniffer captures 1.txt / 6.txt)
            UnikonContest contest;
            if (parseIFrame(frameBuf, fLen, &contest)) {
                bool isNew = !isContestKnown(contest);
                if (isNew) addContest(contest);
                char msg[80];
                snprintf(msg, sizeof(msg), "[UNIKON] IC I-frame: code=%s %c%c/%c%c '%s' %s",
                         contest.code,
                         contest.date[0], contest.date[1], contest.date[2], contest.date[3],
                         contest.name, isNew ? "NEW" : "DUP");
                sendVirtualMessage(String(msg), WARN, false, true);
            } else {
                // I-frame parse failed — dump raw.
                uint8_t dumpLen = fLen < 16 ? fLen : 16;
                char ifail[64];
                int pos = snprintf(ifail, sizeof(ifail), "[UNIKON] IC I-frame FAIL len=%u bytes=", fLen);
                for (uint8_t i = 0; i < dumpLen && pos + 3 < (int)sizeof(ifail); i++)
                    pos += snprintf(ifail + pos, sizeof(ifail) - pos, "%02X ", frameBuf[i]);
                sendVirtualMessage(String(ifail), WARN, false, true);
            }
        } else if (fLen >= 3 && frameBuf[2] == 0x33) {
            // Frame '3': race-info frame returned by some RAMBOX models in IC0 session instead of I-frames.
            // Date at offset 59 is "DD.MM" (5 chars with dot) — convert to "DDMM" for UnikonContest.
            // Name at offset 65 (9 chars, right-padded with spaces).
            // Dump first 24 bytes for verification.
            {
                uint8_t dumpLen = fLen < 24 ? fLen : 24;
                char ic3raw[80];
                int pos = snprintf(ic3raw, sizeof(ic3raw), "[UNIKON] IC frame3 len=%u bytes=", fLen);
                for (uint8_t i = 0; i < dumpLen && pos + 3 < (int)sizeof(ic3raw); i++)
                    pos += snprintf(ic3raw + pos, sizeof(ic3raw) - pos, "%02X ", frameBuf[i]);
                sendVirtualMessage(String(ic3raw), WARN, false, true);
            }
            if (fLen >= 83) {
                UnikonContest contest;
                contest.code[0] = '0'; contest.code[1] = '0'; contest.code[2] = '0'; contest.code[3] = '\0';
                contest.date[0] = (char)frameBuf[59]; contest.date[1] = (char)frameBuf[60];
                contest.date[2] = (char)frameBuf[62]; contest.date[3] = (char)frameBuf[63];
                contest.date[4] = '\0';  // skip dot at offset 61
                memcpy(contest.name, frameBuf + 65, 9); contest.name[9] = '\0';
                trimSpaces(contest.name);
                bool isNew = !isContestKnown(contest);
                if (isNew) addContest(contest);
                char msg[80];
                snprintf(msg, sizeof(msg), "[UNIKON] IC frame3 contest: %c%c/%c%c '%s' %s",
                         contest.date[0], contest.date[1], contest.date[2], contest.date[3],
                         contest.name, isNew ? "NEW" : "DUP");
                sendVirtualMessage(String(msg), WARN, false, true);
            } else {
                // Frame '3' shorter than expected (83 bytes) — log the discrepancy.
                char ic3short[56];
                snprintf(ic3short, sizeof(ic3short), "[UNIKON] IC frame3 too short: len=%u (need 83)", fLen);
                sendVirtualMessage(String(ic3short), WARN, false, true);
            }
        } else {
            // Unknown frame type in IC-phase — dump up to 24 bytes + ASCII for identification.
            uint8_t dumpLen = fLen < 24 ? fLen : 24;
            char icukn[80];
            int pos = snprintf(icukn, sizeof(icukn), "[UNIKON] IC unknown type=%02X len=%u bytes=",
                               fLen >= 3 ? frameBuf[2] : 0x00, fLen);
            for (uint8_t i = 0; i < dumpLen && pos + 3 < (int)sizeof(icukn); i++)
                pos += snprintf(icukn + pos, sizeof(icukn) - pos, "%02X ", frameBuf[i]);
            sendVirtualMessage(String(icukn), WARN, false, true);
            char icasc[52];
            int pa = snprintf(icasc, sizeof(icasc), "[UNIKON] IC unknown ascii=");
            for (uint8_t i = 0; i < fLen && pa + 2 < (int)sizeof(icasc); i++) {
                uint8_t c = frameBuf[i];
                icasc[pa++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
            }
            icasc[pa] = '\0';
            sendVirtualMessage(String(icasc), WARN, false, true);
        }
    }

    s_status.contestCount = s_contestCount;
}

// ----- Public API -----

// Storage init kept as a no-op to preserve call sites.
static void unikonAllocatePsram() {
    // Allocate the three large arrays in PSRAM. Fall back to heap if PSRAM is unavailable.
    // ps_calloc zeroes memory, so no explicit clear is needed after the first allocation.
    bool hasPsram = psramFound();
    if (!s_arrivals) {
        s_arrivals = (UnikonArrival*)(hasPsram
            ? ps_calloc(UNIKON_MAX_ARRIVALS, sizeof(UnikonArrival))
            : calloc(UNIKON_MAX_ARRIVALS, sizeof(UnikonArrival)));
        if (!s_arrivals) Serial.println("[UNIKON] FATAL: s_arrivals alloc failed");
    }
    if (!s_registrations) {
        s_registrations = (UnikonArrival*)(hasPsram
            ? ps_calloc(UNIKON_MAX_ARRIVALS, sizeof(UnikonArrival))
            : calloc(UNIKON_MAX_ARRIVALS, sizeof(UnikonArrival)));
        if (!s_registrations) Serial.println("[UNIKON] FATAL: s_registrations alloc failed");
    }
    if (!s_contests) {
        s_contests = (UnikonContest*)(hasPsram
            ? ps_calloc(UNIKON_MAX_CONTESTS, sizeof(UnikonContest))
            : calloc(UNIKON_MAX_CONTESTS, sizeof(UnikonContest)));
        if (!s_contests) Serial.println("[UNIKON] FATAL: s_contests alloc failed");
    }
}

bool unikonIsEnabled() { return s_cfg.enabled; }

// The RAMBOX speaks 9600 8N1; Bricon speaks 9600 8N2. Selecting a module must switch
// the port automatically, so each module owns its profile and applies it on enable.
void unikonApplySerialProfile() {
#if ENABLE_RS232 == 1
    if (s_cfg.manualSerial) {
        // Bench mode: use the RS232 page settings instead of the fixed 9600 8N1.
        // Only the RUNTIME globals are loaded from the RS232 section -- the module's
        // own config (s_cfg) is never rewritten.
        extern const int RS232_BAUDRATES[];
        extern const int RS232_BAUDRATES_COUNT;
        rs232_enabled = true;        // the module cannot run with the port closed
        rs232_baudrate_index = configManager.getRs232BaudrateIndex();
        if (rs232_baudrate_index < 0 || rs232_baudrate_index >= RS232_BAUDRATES_COUNT) rs232_baudrate_index = 3;
        rs232_baudrate  = RS232_BAUDRATES[rs232_baudrate_index];
        rs232_data_bits = configManager.getRs232DataBits();
        {
            String p = configManager.getRs232Parity();
            p.toLowerCase();
            if (p == "even" || p == "odd" || p == "none") rs232_parity = p;
        }
        rs232_stop_bits = configManager.getRs232StopBits();
        rs232_invert_pins = configManager.getRs232InvertPins();
        _pendingRS232Apply = true;                // applied on the main loop
        sendVirtualMessage(F("[UNIKON] RS232 profile: using the RS232 page settings (manual_serial)"),
                           WARN, false, true);
        return;
    }
    extern const int RS232_BAUDRATES[];
    extern const int RS232_BAUDRATES_COUNT;
    rs232_baudrate = 9600;
    rs232_baudrate_index = 3;                 // 9600 slot in RS232_BAUDRATES
    for (int i = 0; i < RS232_BAUDRATES_COUNT; i++) {
        if (RS232_BAUDRATES[i] == 9600) { rs232_baudrate_index = i; break; }
    }
    rs232_data_bits = 8;
    rs232_parity    = "none";
    rs232_stop_bits = 1;                      // Unikon = 8N1 (Bricon = 8N2)
    _pendingRS232Apply = true;                // applied on the main loop (avoids UART race)
    sendVirtualMessage(F("[UNIKON] RS232 profile requested: 9600 8N1 (RAMBOX)"), WARN, false, true);
#endif
}

// One Rambox module at a time: enabling Unikon disables Bricon (and vice versa).
static void enforceBriconExclusion() {
#if ENABLE_BRICON == 1
    if (briconGetConfig().enabled) {
        briconSetEnabled(false);
        sendVirtualMessage(F("[UNIKON] Bricon disabled (exclusive module rule)"), WARN, false, true);
    }
#endif
}

void unikonBegin() {
    // Initialize storage before loading persisted data.
    unikonAllocatePsram();
    
    memset(&s_status, 0, sizeof(s_status));
    loadConfig();
    // Wiring: 3-wire crossed cable (MAX3232 pin3->RAMBOX pin2, MAX3232 pin2->RAMBOX pin3,
    // GND->GND) + internal jumper pin9->pin5(GND) inside the RAMBOX-side DB9 connector.
    // Pins 4, 6, 7, 8 must be left disconnected. No GPIO-driven RTS needed.
    if (s_cfg.enabled) {
        // Boot with Unikon already selected: own the port and drop the other module.
        enforceBriconExclusion();
        unikonApplySerialProfile();
    }
    if (s_cfg.enabled && s_cfg.pollIntervalSec > 0) {
        s_pollingActive = true;
        // Defer the first poll by one full interval so the boot RS232 echo test
        // or other early traffic is not consumed by an immediate Unikon probe.
        s_lastPollMs = millis();
        char _init_msg[64];
        snprintf(_init_msg, sizeof(_init_msg),
                 "[UNIKON] enabled — interval=%us",
                 (unsigned)s_cfg.pollIntervalSec);
        sendVirtualMessage(String(_init_msg), WARN, false, true);
    } else {
        char _init_msg[64];
        snprintf(_init_msg, sizeof(_init_msg),
                 "[UNIKON] disabled (enabled=%d interval=%u)",
                 (int)s_cfg.enabled, (unsigned)s_cfg.pollIntervalSec);
        sendVirtualMessage(String(_init_msg), WARN, false, true);
    }
}

// ----- Passive listener -----
// Called from unikonUpdate() when idle (between polls).
// When cfg.passiveListen is true, this function checks whether the RAMBOX sent any
// spontaneous frames without being polled (push model). If a valid P-frame or ZP-frame
// is found, it is saved exactly like polling-sourced records.
//
// How it works:
//   1. Peek at the first byte in the RX buffer; expect STX (0x02).
//   2. Consume STX, then peek at the second byte; expect ADDR (0x81).
//   3. If both match: block unikonIsPolling(), set s_prefetchedByte=STX so readFrame()
//      can reconstruct the frame, and read until ETX.
//   4. Parse the frame type and save if recognized. ACK the frame.
//   5. If the bytes don't match RAMBOX signature: log them and discard.
//
// Note: this is synchronous and short-circuits before readFrame() unless a RAMBOX frame
// is confirmed. The 700 ms per-byte timeout in readFrame() only applies once inside a frame.
static void tryPassiveListen() {
    // Runs when passive listening is enabled in config, or whenever the push is active
    // (the RAMBOX was explicitly asked to push, so the frames are expected).
    if (!s_cfg.passiveListen && s_pushState != UNIKON_PUSH_ACTIVE) return;
    if (!_rs232PortInitialized) return;
    if (RS232.available() < 2) return;

    // Peek first byte: must be STX (0x02) to be a RAMBOX frame.
    uint8_t b1 = (uint8_t)RS232.peek();
    if (b1 != UNIKON_STX) {
        // Unexpected byte — consume and log it so we can see what arrives spontaneously.
        RS232.read();
        char tmp[40];
        snprintf(tmp, sizeof(tmp), "[UNIKON] passive: spurious byte 0x%02X", b1);
        sendVirtualMessage(String(tmp), WARN, false, true);
        return;
    }
    RS232.read();  // consume STX

    // Wait briefly for the second byte (ADDR = 0x81).
    uint32_t t2 = millis();
    while (!RS232.available() && millis() - t2 < 20) yield();
    if (!RS232.available()) {
        sendVirtualMessage(F("[UNIKON] passive: STX but no ADDR byte within 20ms"), WARN, false, true);
        return;
    }
    uint8_t b2 = (uint8_t)RS232.peek();
    if (b2 != UNIKON_ADDR) {
        // STX present but next byte is not the RAMBOX address — not our frame.
        RS232.read();  // consume the unknown byte
        char tmp[48];
        snprintf(tmp, sizeof(tmp), "[UNIKON] passive: STX but addr=0x%02X (not RAMBOX 0x81)", b2);
        sendVirtualMessage(String(tmp), WARN, false, true);
        return;
    }

    // Confirmed RAMBOX frame start. Block the RS232 data path and read the full frame.
    s_passiveListening = true;
    s_prefetchedByte   = UNIKON_STX;  // readFrame() re-injects STX as frameBuf[0]
    uint8_t frameBuf[128];
    bool    isEOT = false;
    uint8_t fLen  = readFrame(frameBuf, sizeof(frameBuf), &isEOT);
    s_passiveListening = false;
    s_prefetchedByte   = 0;

    if (isEOT || fLen == 0) {
        char tmp[48];
        snprintf(tmp, sizeof(tmp), "[UNIKON] passive: EOT or timeout (fLen=%u)", fLen);
        sendVirtualMessage(String(tmp), WARN, false, true);
        return;
    }

    uint8_t frameType = fLen >= 3 ? frameBuf[2] : 0;
    char dbg[56];
    snprintf(dbg, sizeof(dbg), "[UNIKON] passive: frame type=0x%02X len=%u", frameType, fLen);
    sendVirtualMessage(String(dbg), WARN, false, true);

    if (fLen >= 39 && frameType == 0x50) {
        // P-frame: spontaneous arrival record (HHMMSS + DDMM).
        UnikonArrival arrival;
        if (parsePFrame(frameBuf, fLen, &arrival)) {
            bool arrived = (arrival.arrivalTime[0] != '\0' &&
                            memcmp(arrival.arrivalTime, "000000", 6) != 0);
            char msg[72];
            snprintf(msg, sizeof(msg), "[UNIKON] passive P ring=%.11s t=%.6s d=%.4s arrived=%d",
                     arrival.ring, arrival.arrivalTime, arrival.arrivalDate, (int)arrived);
            sendVirtualMessage(String(msg), WARN, false, true);
            if (arrived && !isArrivalKnown(arrival.ring)) {
                addArrival(arrival);
                s_status.totalKnown = s_arrivalCount;
                s_status.lastNewCount++;
                sendVirtualMessage(
                    String(F("[UNIKON] passive: NEW arrival ring=")) + String(arrival.ring),
                    WARN, false, true);
            } else if (arrived) {
                // Known ring: refresh time/date in case it changed.
                updateArrivalTime(arrival);
            }
            RS232.write(UNIKON_ACK);
        }
    } else if (fLen >= 30 && frameType == 0x5A && fLen >= 4 && frameBuf[3] == 0x50) {
        // ZP-frame: spontaneous pigeon registration (ring + chip, no arrival time).
        UnikonArrival reg;
        if (parseZpFrame(frameBuf, fLen, &reg)) {
            char msg[56];
            snprintf(msg, sizeof(msg), "[UNIKON] passive ZP ring=%.11s chip=%.11s", reg.ring, reg.chip);
            sendVirtualMessage(String(msg), WARN, false, true);
            if (!isRegistered(reg.ring)) {
                addRegistration(reg);
                s_status.registeredCount = s_registrationCount;
            }
            RS232.write(UNIKON_ACK);
        }
    } else {
        // Unknown frame type — dump raw bytes so we can analyse what the RAMBOX sent.
        uint8_t dumpLen = (fLen < 24) ? fLen : 24;
        char hex[80];
        int pos = snprintf(hex, sizeof(hex), "[UNIKON] passive unknown type=0x%02X len=%u hex=",
                           frameType, fLen);
        for (uint8_t i = 0; i < dumpLen && pos + 3 < (int)sizeof(hex); i++)
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", frameBuf[i]);
        sendVirtualMessage(String(hex), WARN, false, true);
    }
}

static bool sendTrainingSession(const char* name);
static bool clearTrainingSession();

void unikonUpdate() {
    if (!s_cfg.enabled) return;

    // Push mode (device-initiated arrivals): probe on request. While active, the periodic
    // poll is skipped further below -- explicit poll/sync/training requests still run, so the
    // buttons keep working with the push on.
    if (s_pushPending >= 0) {
        bool want = (s_pushPending == 1);
        s_pushPending = -1;
        s_pollInProgress = true;
        pushProbe(want);
        s_pollInProgress = false;
        s_lastPollEndMs = 0;   // no blackout after a probe: pushed frames must not be flushed
        return;
    }

    // While the post-poll blackout is active, keep draining residual UART bytes.
    // The loop-level RS232 reader already stays blocked during this window, but if
    // we do not actively consume the tail bytes here they remain queued in the UART
    // FIFO and leak into bufferDados as soon as the blackout expires.
    // Skipped while the push is active: those bytes are pushed frames, not leftovers.
    if (!s_pollInProgress && s_lastPollEndMs != 0 && s_pushState != UNIKON_PUSH_ACTIVE) {
        unsigned long sincePollEnd = millis() - s_lastPollEndMs;
        if (sincePollEnd < UNIKON_POST_POLL_BLACKOUT_MS) {
            flushRx();
            return;
        }
        // One final drain when leaving blackout, then clear the marker.
        flushRx();
        s_lastPollEndMs = 0;
    }

    // Process an IC-only poll triggered by the web handler.
    if (s_icPollPending) {
        s_icPollPending = false;
        s_forceIcOnlyPoll = true;
        unikonPollNow();
        return;
    }

    // Process a W-only poll triggered by the web handler.
    if (s_wPollPending) {
        s_wPollPending = false;
        s_forceWOnlyPoll = true;
        unikonPollNow();
        return;
    }

    // Process an X-only poll triggered by the web handler.
    if (s_xPollPending) {
        s_xPollPending = false;
        s_forceXOnlyPoll = true;
        unikonPollNow();
        return;
    }

    // Process a full poll triggered by the web handler.
    if (s_pollPending) {
        s_pollPending = false;
        unikonPollNow();
        return;
    }

    // Training management: the training IS a programmed race (UNIKON_TRAINING_CODE
    // + pombal ZPs). Re-enabled 2026-10-17 — sends the fictitious race on
    // explicit request only, never automatically.
    // Process a pending training send request.
    if (s_sendTrainingPending) {
        s_sendTrainingPending = false;
        if (s_cfg.enabled) {
            s_pollInProgress = true;
            char name[10];
            memset(name, ' ', 9);
            name[9] = '\0';
            struct tm timeinfo;
            if (getLocalTime(&timeinfo, 10)) {
                // Training tag: TDDMMHHMM (I-frame name holds 9 chars).
                snprintf(name, 10, "T%02d%02d%02d%02d",
                         timeinfo.tm_mday, timeinfo.tm_mon + 1,
                         timeinfo.tm_hour, timeinfo.tm_min);
            } else {
                memcpy(name, "TREINO_NO", 9);
            }
            sendTrainingSession(name);
            s_pollInProgress = false;
            s_lastPollEndMs = millis();
        }
        return;
    }

    // Process a pending clear-training request.
    if (s_clearTrainingPending) {
        s_clearTrainingPending = false;
        if (s_cfg.enabled) {
            s_pollInProgress = true;
            clearTrainingSession();
            s_pollInProgress = false;
            s_lastPollEndMs = millis();
        }
        return;
    }

    // Auto-poll when interval is configured. The push is ADDITIVE: this poll stays the
    // guarantee, while pushed frames land through the passive listener the moment the RAMBOX
    // sends them -- so an arrival can show up well before the next poll (e.g. a 5-min interval).
    if (!s_pollingActive || s_cfg.pollIntervalSec == 0) {
        if (s_pushState == UNIKON_PUSH_ACTIVE) tryPassiveListen();   // still read pushed frames
        return;
    }
    // If a previous poll found no RAMBOX, wait for the backoff to expire before retrying.
    if (s_noDeviceBackoffUntilMs && millis() < s_noDeviceBackoffUntilMs) {
        if (s_pushState == UNIKON_PUSH_ACTIVE) tryPassiveListen();
        return;
    }
    if (millis() - s_lastPollMs >= (uint32_t)s_cfg.pollIntervalSec * 1000UL) {
        s_forceWOnlyPoll = true;
        unikonPollNow();
        return;
    }

    // Passive listener: check for spontaneous RAMBOX frames while idle (always on with the push).
    tryPassiveListen();
}

bool unikonPollNow() {
    if (!s_cfg.enabled) return false;
#if ENABLE_RS232 == 1
    // unikonUpdate() runs before the loop-level deferred RS232 apply. If the
    // user just changed invert/baud settings, the config variables can already
    // be updated while the hardware UART still uses the old pins/framing.
    if (_pendingRS232Apply) {
        _pendingRS232Apply = false;
        applyRS232Config();
    }
#endif
    // Guard: the UART driver must be installed before any RS232 I/O.
    // If RS232 was disabled or is mid-reconfiguration, _rs232PortInitialized is false
    // and calling RS232.available()/read() would emit "uart driver error" from ESP-IDF.
    //
    // IMPORTANT (Req 6 / Req 7): This function uses the RS232 port as-is.
    // It does NOT change baudrate, parity, stop-bits, or any other UART parameter.
    // The caller is responsible for ensuring the port is configured correctly
    // (9600 8N1, no flow control) via the RS232 settings page before enabling Unikon.
    if (!_rs232PortInitialized) {
        sendVirtualMessage(F("[UNIKON] poll skipped: RS232 not ready"), WARN, false, true);
        s_pollInProgress = false;
        return false;
    }

    s_pollInProgress       = true;
    s_lastPollMs           = millis();
    s_status.lastPollMs    = s_lastPollMs;
    s_status.connected     = false;
    s_status.lastNewCount  = 0;
    bool icOnlyPollCycle = s_forceIcOnlyPoll;
    s_forceIcOnlyPoll = false;
    bool wOnlyPollCycle = s_forceWOnlyPoll;
    s_forceWOnlyPoll = false;
    bool xOnlyPollCycle = s_forceXOnlyPoll;
    s_forceXOnlyPoll = false;

    // Snapshot list size before polling. If this poll is the first successful
    // sync after boot/reset and RAMBOX returns a full historical list, treat it
    // as baseline and do not email everything as "new".
    const uint16_t arrivalsBeforePoll = s_arrivalCount;

    s_prefetchedByte = 0;  // clear any stale byte from a previous aborted poll
    // Always flush RX before starting. s_status.connected was just set to false above,
    // so the old conditional check was always false (bug). Flush unconditionally instead;
    // in Unikon mode the RS232 port is dedicated to the RAMBOX.
    flushRx();
    char _baud_msg[144];
    snprintf(_baud_msg,
             sizeof(_baud_msg),
             "[UNIKON] poll start - baud=%lu cfg=%d%s%d RX=%d TX=%d invert=%s",
             (unsigned long)RS232.baudRate(),
             rs232_data_bits,
             rs232_parity.c_str(),
             rs232_stop_bits,
             rs232_rx_pin,
             rs232_tx_pin,
             rs232_invert_pins ? "YES" : "NO");
    sendVirtualMessage(String(_baud_msg), WARN, false, true);
    sendVirtualMessage(F("[UNIKON] --- poll start ---"), WARN, false, true);
    if (rs232_data_bits != 8 || !rs232_parity.equalsIgnoreCase("none") || rs232_stop_bits != 1) {
        sendVirtualMessage(F("[UNIKON] WARN: expected RS232 config 8none1 for RAMBOX"), WARN, false, true);
    }

    if (icOnlyPollCycle) {
        sendVirtualMessage(F("[UNIKON] mode: calendar-only poll (IC-phase)"), WARN, false, true);
        runIcPhase();
        flushRx();
        s_status.connected = true;
        s_noDeviceBackoffUntilMs = 0;
        s_pollInProgress = false;
        s_lastPollEndMs = millis();

        char msg[64];
        snprintf(msg, sizeof(msg), "Unikon: IC-only poll ok — contests=%u", (unsigned)s_contestCount);
        directLogPrintln(msg);
            unikonPersistCalendarAndPombal();  // Save contests to config
            return true;
    }

    if (xOnlyPollCycle) {
        sendVirtualMessage(F("[UNIKON] mode: pombal-only poll (X-phase)"), WARN, false, true);

        clearRegistrationsStorage();
        s_registrationCount = 0;

        bool xOk = runXPhase();
        flushRx();

        if (!xOk) {
            s_pollInProgress = false;
            s_lastPollEndMs = millis();
            s_noDeviceBackoffUntilMs = millis() + UNIKON_NO_DEVICE_BACKOFF_MS;
            return false;
        }

        s_status.connected = true;
        s_noDeviceBackoffUntilMs = 0;
        s_pollInProgress = false;
        s_lastPollEndMs = millis();

        char msg[72];
        snprintf(msg, sizeof(msg), "Unikon: X-only poll ok — pombal=%u", (unsigned)s_registrationCount);
        directLogPrintln(msg);
        unikonPersistCalendarAndPombal();  // Save pombal to config
        return true;
    }

    if (wOnlyPollCycle) {
        sendVirtualMessage(F("[UNIKON] mode: arrivals-only poll (W-phase)"), WARN, false, true);

        const uint16_t arrivalsBeforeW = s_arrivalCount;
        bool hadRaceInfoBeforeW = (s_status.raceDate[0] != '\0') || (s_status.raceName[0] != '\0');

        bool wOk = runWPhase();
        flushRx();

        bool hasRaceInfoAfterW = (s_status.raceDate[0] != '\0') || (s_status.raceName[0] != '\0');
        bool gotAnyArrivalInW = s_arrivalCount > arrivalsBeforeW;

        // Some RAMBOX units answer the first W poll with EOT/no frame3 and only
        // return race/arrival frames on the immediate next poll. Retry once in the
        // same cycle so users do not need to trigger a second manual/auto poll.
        if (wOk && !gotAnyArrivalInW && !hasRaceInfoAfterW && !hadRaceInfoBeforeW) {
            sendVirtualMessage(F("[UNIKON] W-phase empty on first attempt - retrying once"), WARN, false, true);
            delay(120);
            flushRx();
            wOk = runWPhase();
            flushRx();
            hasRaceInfoAfterW = (s_status.raceDate[0] != '\0') || (s_status.raceName[0] != '\0');
            gotAnyArrivalInW = s_arrivalCount > arrivalsBeforeW;
            if (wOk && (gotAnyArrivalInW || hasRaceInfoAfterW)) {
                sendVirtualMessage(F("[UNIKON] W-phase retry recovered race/arrival data"), WARN, false, true);
            }
        }

        if (!wOk) {
            s_pollInProgress = false;
            s_lastPollEndMs = millis();
            s_noDeviceBackoffUntilMs = millis() + UNIKON_NO_DEVICE_BACKOFF_MS;
            return false;
        }

        s_status.connected = true;
        s_status.totalKnown = s_arrivalCount;
        s_noDeviceBackoffUntilMs = 0;
        s_pollInProgress = false;
        s_lastPollEndMs = millis();

        char msg[88];
        snprintf(msg, sizeof(msg),
                 "Unikon: W-only poll ok — arrivals total=%u new=%u",
                 (unsigned)s_arrivalCount, (unsigned)s_status.lastNewCount);
        directLogPrintln(msg);

#if ENABLE_SMTP == 1 || ENABLE_GOOGLE_EMAIL == 1
        {
            if (arrivalsBeforePoll == 0 && s_pendingEmailStart == 0 && s_arrivalCount > 0) {
                // First data arrival - just mark this for email delivery on this poll.
                // Do NOT update s_pendingEmailStart yet; keep it at 0 so pending > 0 below.
                s_lastEmailAttemptMs = 0;  // Allow immediate send attempt on this same poll
                char bmsg[96];
                snprintf(bmsg, sizeof(bmsg),
                         "[UNIKON] Baseline sync: %u registo(s) carregado(s); processando emails",
                         (unsigned)s_arrivalCount);
                sendVirtualMessage(String(bmsg), WARN, false, true);
            }

            uint16_t pending = s_arrivalCount - s_pendingEmailStart;
            if (pending > 0) {
                unsigned long now = millis();
                bool canAttempt = (s_lastEmailAttemptMs == 0) ||
                                  ((now - s_lastEmailAttemptMs) >= UNIKON_EMAIL_RETRY_MS);
                if (canAttempt) {
                    uint16_t pendingRace = 0, pendingTraining = 0;
                    for (uint16_t i = s_pendingEmailStart; i < s_arrivalCount; i++) {
                        if (!hasValidTime(s_arrivals[i])) continue;
                        if (s_arrivals[i].isTraining) pendingTraining++;
                        else pendingRace++;
                    }

                    if (pendingRace == 0 && pendingTraining == 0) {
                        s_pendingEmailStart = s_arrivalCount;
                        s_lastEmailAttemptMs = now;
                        char skipMsg[96];
                        snprintf(skipMsg, sizeof(skipMsg),
                                 "[UNIKON] Email skip: %u pending registo(s) sem chegada valida",
                                 (unsigned)pending);
                        sendVirtualMessage(String(skipMsg), WARN, false, true);
                        return true;
                    }

                    String raceName1 = (s_status.raceName[0]) ? String(s_status.raceName) : String(F(""));
                    String subject = ramboxEmailSubject("Unikon", raceName1.c_str(),
                                                       (uint16_t)(pendingRace + pendingTraining));
                    String body;
                    ramboxEmailHeader(body, "Unikon", s_status.clubName, s_status.clubCode,
                                      s_status.fancierName, s_status.raceName,
                                      s_status.raceDate, s_status.soltaName);

                    String secRaceNew, secRaceOld, secTrainNew;
                    appendArrivalSectionEmail(secRaceNew, ramboxEmailSecNew(), false, s_pendingEmailStart, s_arrivalCount);
                    if (secRaceNew.length() > 0) body += secRaceNew + String(F("\n"));
                    appendArrivalSectionEmail(secTrainNew, ramboxEmailSecTrain(), true, s_pendingEmailStart, s_arrivalCount);
                    if (secTrainNew.length() > 0) body += secTrainNew + String(F("\n"));

                    if (s_pendingEmailStart > 0) {
                        body += String(F("\n")) + String(ramboxEmailSecHistory()) + String(F("\n\n"));
                        appendArrivalSectionEmail(secRaceOld, ramboxEmailSecOld(), false, 0, s_pendingEmailStart);
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
                        char buf[64];
                        snprintf(buf, sizeof(buf), "[UNIKON] Email queued (%u chegada(s))", (unsigned)pendingRace);
                        sendVirtualMessage(String(buf), WARN, false, true);
                    } else {
                        char buf[80];
                        snprintf(buf, sizeof(buf),
                                 "[UNIKON] Email deferred (%u pigeon(s) pending, retry in 60s)",
                                 (unsigned)pending);
                        sendVirtualMessage(String(buf), WARN, false, true);
                    }
#elif ENABLE_GOOGLE_EMAIL == 1
                    {
                        String dest = configManager.getEmailDestination();
                        if (dest.length() > 0 && googleEmailIsAvailable()) {
                            // T24: arquivo .txt do corpo (UNIKON, hora de envio), SD ou LittleFS.
                            { extern bool emailArchiveSaveBody(const String&, const String&); emailArchiveSaveBody(F("UNIKON"), body); }
                            googleEmailSendSimple(dest, subject, body);
                            s_pendingEmailStart = s_arrivalCount;
                        }
                    }
#endif
                }
            }
        }
#endif
        return true;
    }

    // Step 1: Handshake — send v-frame, RAMBOX replies with ident frame (no ACK byte first).
    // NOTE: W-phase runs AFTER the X-phase (see Step 3 below) so that ZP-entries already
    // exist in s_arrivals[] when updateArrivalTime() is called.
    sendVirtualMessage(F("[UNIKON] Step 1: sending v-frame (identify)"), WARN, false, true);
    if (!sendVFrameAndReadIdent(s_status.moduleId, sizeof(s_status.moduleId))) {
        directLogPrintln("Unikon: no ident frame from RAMBOX");
        sendVirtualMessage(F("[UNIKON] FAIL: no ident frame — check cable & baudrate"), WARN, false, true);
        s_pollInProgress = false;
        s_lastPollEndMs = millis();
        // No device found — back off to avoid locking the RS232 port on every poll interval.
        s_noDeviceBackoffUntilMs = millis() + UNIKON_NO_DEVICE_BACKOFF_MS;
        return false;
    }
    sendVirtualMessage(F("[UNIKON] Step 1: ident OK"), WARN, false, true);

    // Step 2: Send X-frame (status query), wait for RAMBOX response.
    // Normal case (2.txt/3.txt): RAMBOX responds with 0x06 ACK (sometimes twice).
    // Variant  (4.txt):          RAMBOX responds with 0x02 STX, meaning it skipped the
    //                            X-frame ACK and already started sending S/ZP frames.
    //                            Store the STX in s_prefetchedByte for readFrame to consume.
    sendVirtualMessage(F("[UNIKON] Step 2: sending X-frame (status)"), WARN, false, true);
    {
        uint8_t xFrame[64];
        uint8_t xLen = buildFrame(CONTENT_X, sizeof(CONTENT_X), xFrame);
        bool xDone = false;
        for (uint8_t attempt = 0; attempt < UNIKON_MAX_RETRIES && !xDone; attempt++) {
            sendVirtualMessage(String(F("[UNIKON] TX [")) + String(attempt+1) + F("]: ") + hexDump(xFrame, xLen), WARN, false, true);
            RS232.write(xFrame, xLen);
            RS232.flush();  // ensure all bytes are physically transmitted before waiting for response
            uint32_t t = millis();
            while (millis() - t < UNIKON_TIMEOUT_MS) {
                if (!RS232.available()) { unikonYield(); continue; }
                int b = RS232.read();
                char tmp[40];
                snprintf(tmp, sizeof(tmp), "[UNIKON] RX: %02X (%s)", (uint8_t)b,
                         b == UNIKON_ACK ? "ACK" : (b == UNIKON_STX ? "STX(frames-early)" : (b == UNIKON_NAK ? "NAK" : "OTHER")));
                sendVirtualMessage(String(tmp), WARN, false, true);
                if (b == UNIKON_ACK) { xDone = true; break; }
                if (b == UNIKON_STX) {
                    // RAMBOX skipped X-frame ACK and is already sending S/ZP frames.
                    s_prefetchedByte = UNIKON_STX;
                    xDone = true;
                    break;
                }
                if (b == UNIKON_NAK) break;  // retry
            }
            if (!xDone) delay(30);
        }
        if (!xDone) {
            directLogPrintln("Unikon: no ACK to X-frame");
            sendVirtualMessage(F("[UNIKON] FAIL: no ACK to X-frame"), WARN, false, true);
            s_pollInProgress = false;
            s_lastPollEndMs = millis();
            s_noDeviceBackoffUntilMs = millis() + UNIKON_NO_DEVICE_BACKOFF_MS;
            return false;
        }
    }
    sendVirtualMessage(F("[UNIKON] Step 2: ok — reading pigeon frames"), WARN, false, true);
    clearRegistrationsStorage();
    s_registrationCount = 0;
    s_status.registeredCount = 0;

    // Step 3: Receive all ZP / S1 / S2 frames until EOT.
    // Buffer sized for 128 bytes to handle all known frame types:
    //   ZP-frame = 30 bytes, S-frames up to ~50 bytes,
    //   frame '3' (race info) = 83 bytes — was the cause of the frameBuf[80] overflow bug.
    uint8_t frameBuf[128];
    uint16_t rxCount  = 0;
    uint16_t newCount = 0;

    while (true) {
        bool isEOT = false;
        uint8_t fLen = readFrame(frameBuf, sizeof(frameBuf), &isEOT);
        if (isEOT) break;
        if (fLen == 0) {
            directLogPrintln("Unikon: frame timeout while reading list");
            break;
        }

        // ACK every received frame.
        RS232.write(UNIKON_ACK);

        if (fLen >= 4 && frameBuf[2] == 0x5A && frameBuf[3] == 0x50) {
            // ZP frame — registered pigeon entry (ring + chip only).
            UnikonArrival arrival;
            if (parseZpFrame(frameBuf, fLen, &arrival)) {
                rxCount++;
                bool isNew = !isRegistered(arrival.ring);
                if (isNew) addRegistration(arrival);
                s_status.registeredCount = s_registrationCount;
                char zpdbg[48];
                snprintf(zpdbg, sizeof(zpdbg), "[UNIKON] ZP #%u ring=%.11s chip=%.11s %s",
                         rxCount, arrival.ring, arrival.chip, isNew ? "NEW" : "DUP");
                sendVirtualMessage(String(zpdbg), WARN, false, true);
            } else {
                // ZP frame failed to parse — log raw bytes for diagnosis.
                uint8_t dumpLen = fLen < 12 ? fLen : 12;
                char zpfail[56];
                int pos = snprintf(zpfail, sizeof(zpfail), "[UNIKON] ZP-frame parse FAIL len=%u bytes=", fLen);
                for (uint8_t i = 0; i < dumpLen && pos + 3 < (int)sizeof(zpfail); i++)
                    pos += snprintf(zpfail + pos, sizeof(zpfail) - pos, "%02X ", frameBuf[i]);
                sendVirtualMessage(String(zpfail), WARN, false, true);
            }
        } else if (fLen >= 4 && frameBuf[2] == 0x53) {
            // S1/S2/S3 frame — solta / club info.
            if (frameBuf[3] == 0x31) {
                parseS1Frame(frameBuf, fLen, s_status.clubName, sizeof(s_status.clubName));
                cacheSFrame(frameBuf, fLen);
                char smsg[48];
                snprintf(smsg, sizeof(smsg), "[UNIKON] X S1-frame: club=%.24s", s_status.clubName);
                sendVirtualMessage(String(smsg), WARN, false, true);
            } else if (frameBuf[3] == 0x32) {
                parseS2Frame(frameBuf, fLen, s_status.soltaName, sizeof(s_status.soltaName));
                parseSClubCode(frameBuf, fLen, s_status.clubCode, sizeof(s_status.clubCode));
                cacheSFrame(frameBuf, fLen);
                char smsg[48];
                snprintf(smsg, sizeof(smsg), "[UNIKON] X S2-frame: solta=%.24s", s_status.soltaName);
                sendVirtualMessage(String(smsg), WARN, false, true);
            } else if (frameBuf[3] == 0x33) {
                parseS3Frame(frameBuf, fLen, s_status.fancierName, sizeof(s_status.fancierName));
                parseSClubCode(frameBuf, fLen, s_status.clubCode, sizeof(s_status.clubCode));
                cacheSFrame(frameBuf, fLen);
                char smsg[64];
                snprintf(smsg, sizeof(smsg), "[UNIKON] X S3-frame: fancier=%.31s", s_status.fancierName);
                sendVirtualMessage(String(smsg), WARN, false, true);
            } else {
                // S-frame with unknown sub-type — dump first 12 bytes.
                uint8_t dumpLen = fLen < 12 ? fLen : 12;
                char sunknown[56];
                int pos = snprintf(sunknown, sizeof(sunknown), "[UNIKON] X S-frame subtype=%02X len=%u bytes=", frameBuf[3], fLen);
                for (uint8_t i = 0; i < dumpLen && pos + 3 < (int)sizeof(sunknown); i++)
                    pos += snprintf(sunknown + pos, sizeof(sunknown) - pos, "%02X ", frameBuf[i]);
                sendVirtualMessage(String(sunknown), WARN, false, true);
            }
        } else {
            // Unknown frame type in X-phase — dump up to 16 bytes for identification.
            uint8_t dumpLen = fLen < 16 ? fLen : 16;
            char xukn[72];
            int pos = snprintf(xukn, sizeof(xukn), "[UNIKON] X unknown type=%02X len=%u bytes=",
                               fLen >= 3 ? frameBuf[2] : 0x00, fLen);
            for (uint8_t i = 0; i < dumpLen && pos + 3 < (int)sizeof(xukn); i++)
                pos += snprintf(xukn + pos, sizeof(xukn) - pos, "%02X ", frameBuf[i]);
            sendVirtualMessage(String(xukn), WARN, false, true);
        }
    }

    {
        char xsum[64];
        snprintf(xsum, sizeof(xsum), "[UNIKON] X-phase done: rxCount=%u registered=%u club='%s'",
                 rxCount, (unsigned)s_registrationCount, s_status.clubName[0] ? s_status.clubName : "(none)");
        sendVirtualMessage(String(xsum), WARN, false, true);
    }
    s_status.connected    = true;
    s_status.totalKnown   = s_arrivalCount;
    s_status.lastNewCount = newCount;
    s_status.registeredCount = s_registrationCount;
    s_noDeviceBackoffUntilMs = 0;  // RAMBOX responded successfully — clear any backoff

    // If RAMBOX returned 0 pigeons and we had a known set, a new race has started —
    // clear the known set so the next poll starts fresh.
    // NOTE: Preserve email state so the next poll doesn't re-send emails for the same race.
    if (rxCount == 0 && (s_registrationCount > 0 || s_arrivalCount > 0)) {
        directLogPrintln("Unikon: RAMBOX empty — resetting known set (new race)");
        uint16_t savedEmailStart = s_pendingEmailStart;
        unsigned long savedEmailAttemptMs = s_lastEmailAttemptMs;
        unikonResetState();
        s_pendingEmailStart = savedEmailStart;
        s_lastEmailAttemptMs = savedEmailAttemptMs;
    }

    // Step 3: W-phase — fetch race date/name and update arrival times for ZP-entries.
    // Runs AFTER the X-phase so all ZP-entries already exist in s_arrivals[]. That way
    // updateArrivalTime() reliably updates every pigeon, and new arrivals not in the
    // ZP list are added via addArrival() as a fall-through.
    const uint16_t arrivalsBeforeW = s_arrivalCount;
    bool hadRaceInfoBeforeW = (s_status.raceDate[0] != '\0') || (s_status.raceName[0] != '\0');
    bool wOk = runWPhase();
    flushRx();

    bool hasRaceInfoAfterW = (s_status.raceDate[0] != '\0') || (s_status.raceName[0] != '\0');
    bool gotAnyArrivalInW = s_arrivalCount > arrivalsBeforeW;

    // Same RAMBOX quirk can happen in full (X+W) polls: first W attempt may return
    // EOT/no frame3 and only provide race/arrival data on the immediate next W.
    if (wOk && !gotAnyArrivalInW && !hasRaceInfoAfterW && !hadRaceInfoBeforeW) {
        sendVirtualMessage(F("[UNIKON] W-phase empty in full poll - retrying once"), WARN, false, true);
        delay(120);
        flushRx();
        wOk = runWPhase();
        flushRx();
        hasRaceInfoAfterW = (s_status.raceDate[0] != '\0') || (s_status.raceName[0] != '\0');
        gotAnyArrivalInW = s_arrivalCount > arrivalsBeforeW;
        if (wOk && (gotAnyArrivalInW || hasRaceInfoAfterW)) {
            sendVirtualMessage(F("[UNIKON] W-phase full-poll retry recovered race/arrival data"), WARN, false, true);
        }
    }

    runIcPhase();
    flushRx();
    unikonPersistCalendarAndPombal();
    // Refresh totals in case W-phase added new entries.
    s_status.totalKnown = s_arrivalCount;
    s_status.registeredCount = s_registrationCount;
    s_status.contestCount = s_contestCount;

    s_pollInProgress = false;
    s_lastPollEndMs = millis();  // mark end time for post-poll RS232 blackout

    char msg[96];
    snprintf(msg, sizeof(msg),
             "Unikon: poll ok — %u ZP cached, %u arrivals total, %u new",
             rxCount, (unsigned)s_arrivalCount, newCount);
    directLogPrintln(msg);
    // Email delivery with pending-arrival accumulation.
    //
    // startUnikonEmailAsync() has its own dedicated task slot (separate from the
    // test-email slot) so arrival emails and manual test emails do not block each
    // other. The handle is properly cleared when the task finishes, so the guard
    // in startUnikonEmailAsync() works correctly on every subsequent poll.
    //
    // Track s_pendingEmailStart — the first s_arrivals[] index not yet included
    // in a successfully dispatched email. Advance only on success; retry after
    // UNIKON_EMAIL_RETRY_MS on failure so no arrival is silently dropped.
#if ENABLE_SMTP == 1 || ENABLE_GOOGLE_EMAIL == 1
    {
        if (arrivalsBeforePoll == 0 && s_pendingEmailStart == 0 && s_arrivalCount > 0) {
            // First data arrival - just mark this for email delivery on this poll.
            // Do NOT update s_pendingEmailStart yet; keep it at 0 so pending > 0 below.
            s_lastEmailAttemptMs = 0;  // Allow immediate send attempt on this same poll
            char bmsg[96];
            snprintf(bmsg, sizeof(bmsg),
                     "[UNIKON] Baseline sync: %u registo(s) carregado(s); processando emails",
                     (unsigned)s_arrivalCount);
            sendVirtualMessage(String(bmsg), WARN, false, true);
        }

        uint16_t pending = s_arrivalCount - s_pendingEmailStart;
        if (pending > 0) {
            unsigned long now = millis();
            bool canAttempt = (s_lastEmailAttemptMs == 0) ||
                              ((now - s_lastEmailAttemptMs) >= UNIKON_EMAIL_RETRY_MS);
            if (canAttempt) {
                uint16_t pendingRace = 0, pendingTraining = 0;
                for (uint16_t i = s_pendingEmailStart; i < s_arrivalCount; i++) {
                    if (!hasValidTime(s_arrivals[i])) continue;
                    if (s_arrivals[i].isTraining) pendingTraining++;
                    else pendingRace++;
                }

                // Registration snapshots and non-arrival entries must never be sent by email.
                // If there are no emailable arrivals (race or training) in the pending window, consume it.
                if (pendingRace == 0 && pendingTraining == 0) {
                    s_pendingEmailStart = s_arrivalCount;
                    s_lastEmailAttemptMs = now;
                    char skipMsg[96];
                    snprintf(skipMsg, sizeof(skipMsg),
                             "[UNIKON] Email skip: %u pending registo(s) sem chegada valida",
                             (unsigned)pending);
                    sendVirtualMessage(String(skipMsg), WARN, false, true);
                    return true;
                }

String raceName2 = (s_status.raceName[0]) ? String(s_status.raceName) : String(F(""));
                String subject = ramboxEmailSubject("Unikon", raceName2.c_str(),
                                                   (uint16_t)(pendingRace + pendingTraining));
                String body;
                ramboxEmailHeader(body, "Unikon", s_status.clubName, s_status.clubCode,
                                  s_status.fancierName, s_status.raceName,
                                  s_status.raceDate, s_status.soltaName);
                
                // Include registered pigeons (pombal/ZP snapshot) for context
                appendRegistrationsSectionEmail(body);

                String secRaceNew, secRaceOld, secTrainNew;
                appendArrivalSectionEmail(secRaceNew, ramboxEmailSecNew(), false, s_pendingEmailStart, s_arrivalCount);
                if (secRaceNew.length() > 0) body += secRaceNew + String(F("\n"));
                appendArrivalSectionEmail(secTrainNew, ramboxEmailSecTrain(), true, s_pendingEmailStart, s_arrivalCount);
                if (secTrainNew.length() > 0) body += secTrainNew + String(F("\n"));

                // Previous arrivals below a separator, so the full history is always visible.
                if (s_pendingEmailStart > 0) {
                    body += String(F("\n")) + String(ramboxEmailSecHistory()) + String(F("\n\n"));
                    appendArrivalSectionEmail(secRaceOld, ramboxEmailSecOld(), false, 0, s_pendingEmailStart);
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
                // Preferred path: dedicated async task handles both SMTP and Google.
                if (startUnikonEmailAsync(subject, body)) {
                    // Task started — mark all current arrivals as dispatched.
                    s_pendingEmailStart = s_arrivalCount;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "[UNIKON] Email queued (%u chegada(s))", (unsigned)pendingRace);
                    sendVirtualMessage(String(buf), WARN, false, true);
                } else {
                    // Task busy — keep s_pendingEmailStart; retry in UNIKON_EMAIL_RETRY_MS.
                    char buf[80];
                    snprintf(buf, sizeof(buf),
                             "[UNIKON] Email deferred (%u pigeon(s) pending, retry in 60s)",
                             (unsigned)pending);
                    sendVirtualMessage(String(buf), WARN, false, true);
                }
#elif ENABLE_GOOGLE_EMAIL == 1
                // Google-only build: synchronous direct call.
                {
                    String dest = configManager.getEmailDestination();
                    if (dest.length() > 0 && googleEmailIsAvailable()) {
                        // T24: arquivo .txt do corpo (UNIKON, hora de envio), SD ou LittleFS.
                        { extern bool emailArchiveSaveBody(const String&, const String&); emailArchiveSaveBody(F("UNIKON"), body); }
                        googleEmailSendSimple(dest, subject, body);
                        s_pendingEmailStart = s_arrivalCount;
                    }
                }
#endif
            }
        }
    }
#endif
    // Push: this poll just identified the RAMBOX, so ask it for the push now -- once per device.
    // Later polls that see the same ident send nothing extra (pushCheckDevice() binds the state).
    if (s_pushAuto && s_pushState == UNIKON_PUSH_OFF) pushProbe(true);
    return true;
}

void unikonRequestPoll() {
    s_pollPending = true;
}

void unikonRequestWOnlyPoll() {
    s_wPollPending = true;
}

void unikonRequestIcOnlyPoll() {
    s_icPollPending = true;
}

void unikonRequestXOnlyPoll() {
    s_xPollPending = true;
}

void unikonRequestSendTraining() {
    s_sendTrainingPending = true;
}

static uint16_t setTrainingFilter(const char* text);

uint16_t unikonSetTrainingFilter(const char* text) {
    return setTrainingFilter(text);
}

void unikonRequestClearTraining() {
    s_clearTrainingPending = true;
}

// ----- Push (device-initiated arrivals) -----

// Ask the RAMBOX to also push arrivals ("P1"), or to stop ("P0").
// Non-blocking: sets a flag processed by unikonUpdate(). Handshakes first, so the push
// state stays bound to the identified device (see pushCheckDevice()).
void unikonRequestPush(bool on) {
    s_pushPending = on ? 1 : 0;
}

// When enabled, the push is re-armed automatically on every newly seen RAMBOX, so plugging
// the hub into another unit asks again instead of assuming the previous answer.
void unikonSetPushAuto(bool on) {
    s_pushAuto = on;
    if (on && s_pushState != UNIKON_PUSH_ACTIVE) s_pushPending = 1;
}

bool        unikonIsPushAuto()     { return s_pushAuto; }
bool        unikonIsPushActive()   { return s_pushState == UNIKON_PUSH_ACTIVE; }
uint8_t     unikonGetPushState()   { return s_pushState; }
uint16_t    unikonGetPushCode()    { return s_pushCode; }
const char* unikonGetPushDevice()  { return s_pushDevice; }
const char* unikonGetPushVersion() { return s_pushVersion; }

// Send one I-frame (contest entry) to the RAMBOX.
// Content: ADDR 'I' CODE[3] DDMM[4] NAME[8] N[1]  (18 bytes total; 22-byte frame).
// Matches the original builder "I0%02u%02u%02u%-8s%01u": the name field is EIGHT
// characters and the last byte is a digit (never a space).
// Returns true when RAMBOX ACKs.
static bool sendIFrame(const char* code, const char* date, const char* name, char tail) {
    uint8_t content[18];
    content[0] = UNIKON_ADDR;
    content[1] = 0x49;
    memcpy(content + 2, code, 3);
    memcpy(content + 5, date, 4);
    memset(content + 9, ' ', 8);               // NAME[8]
    uint8_t nLen = (uint8_t)strlen(name);
    if (nLen > 9) nLen = 9;
    if (nLen == 9) {
        // A 9-character tag (e.g. "TDDMMHHMM") fills NAME[8] + the N field; keep it as
        // long as its last character really is a digit, otherwise fall back to tail.
        memcpy(content + 9, name, 9);
        if (content[17] < '0' || content[17] > '9') content[17] = (uint8_t)tail;
    } else {
        memcpy(content + 9, name, nLen);
        content[17] = (uint8_t)tail;           // N[1] -- always a digit
    }
    return sendAndWaitAck(content, sizeof(content));
}

// Send one pigeon record to the RAMBOX as a POUL UPLOAD line.
//
// The vendor driver's uniUploadPoulData() prefixes EVERY data line with 'M' -- 'M' is NOT a
// standalone command: unikonDrv.dll FUN_10005ee0 skips the command frame when cmd[0] == 'M'
// and then builds "M" + <line> for each line of the file. Without the prefix the RAMBOX does
// not store the record, so a programmed race has no entries and reports no arrivals.
// (Evidence: rea-work/ucuk/artifacts/drv/COMMAND_MAP.md)
//
//   UNIKON_ZP_WRITE_PREFIX_Z=1 -> 'M' "ZP" + RING[11] + ' ' + CHIP[11]  (27 B, 31-B frame)
//   UNIKON_ZP_WRITE_PREFIX_Z=0 -> 'M' "P"  + RING[11] + ' ' + CHIP[11]  (26 B, 30-B frame)
// Returns true when RAMBOX ACKs.
static bool sendZPFrame(const char* ring, const char* chip) {
#if UNIKON_ZP_WRITE_PREFIX_Z
    uint8_t content[27];
    content[0] = UNIKON_ADDR;
    content[1] = 0x4D;          // 'M' -- poul upload line prefix
    content[2] = 0x5A;          // 'Z'
    content[3] = 0x50;          // 'P'
    memcpy(content + 4, ring, 11);
    content[15] = 0x20;
    memcpy(content + 16, chip, 11);
#else
    uint8_t content[26];
    content[0] = UNIKON_ADDR;
    content[1] = 0x4D;          // 'M' -- poul upload line prefix
    content[2] = 0x50;          // 'P'
    memcpy(content + 3, ring, 11);
    content[14] = 0x20;
    memcpy(content + 15, chip, 11);
#endif
    return sendAndWaitAck(content, sizeof(content));
}

// Replace the training entry filter from newline-separated text (S4 lines
// or plain rings). Empty text clears the filter (= whole pombal).
// Returns the number of rings accepted.
static uint16_t setTrainingFilter(const char* text) {
    s_trainingFilterCount = 0;
    s_trainingMode = UNIKON_TRAIN_MODE_ALL;   // legacy default
    if (!text || text[0] == '\0') return 0;   // empty -> whole pombal (unchanged)
    if (strcmp(text, UNIKON_TRAIN_SEL_NONE) == 0) {   // "-" -> race only, no pigeons
        s_trainingMode = UNIKON_TRAIN_MODE_NONE;
        return 0;
    }
    if (strcmp(text, UNIKON_TRAIN_SEL_ALL) == 0) {    // "*" -> explicit whole pombal
        s_trainingMode = UNIKON_TRAIN_MODE_ALL;
        return 0;
    }
    s_trainingMode = UNIKON_TRAIN_MODE_LIST;
    const char* p = text;
    char line[64];
    while (*p && s_trainingFilterCount < UNIKON_MAX_ARRIVALS) {
        size_t n = 0;
        while (p[n] && p[n] != '\n' && n < sizeof(line) - 1) {
            line[n] = p[n];
            n++;
        }
        line[n] = '\0';
        p += n;
        if (*p == '\n') p++;
        char ring[12];
        if (parseTrainingEntryLine(line, ring)) {
            memcpy(s_trainingFilter[s_trainingFilterCount], ring, 12);
            s_trainingFilterCount++;
        }
    }
    return s_trainingFilterCount;
}

// Write a full training session to the RAMBOX (fictitious race UNIKON_TRAINING_CODE):
//   1. V-frame handshake
//   2. IC0 + I-frame (contest entry for today)
//   3. S1/S2/S3 replay (club/solta/fancier context cached from X/W reads)
//   4. Pigeon records for the selected entry list:
//        s_trainingMode == ALL  -> every registered pigeon
//        s_trainingMode == LIST -> only the listed rings
//        s_trainingMode == NONE -> no pigeons at all (race only)
// Mirrors the original software order (IC0+I+S+ZP).
// Returns true when the RACE was programmed (I-frame ACKed), independently of how many
// pigeon records were accepted, so "race only" and "race + pigeons" can be told apart.
// Never touches the RAMBOX training state: the training IS the programmed race.
// FNV-1a over the current pombal ring set. Used by the training read-back to compare what we
// allocated with what the module actually reports, without keeping a second copy of the list.
static uint32_t pombalRingHash() {
    uint32_t h = 2166136261u;
    for (uint16_t i = 0; i < s_registrationCount; i++) {
        for (uint8_t k = 0; k < 11; k++) { h ^= (uint8_t)s_registrations[i].ring[k]; h *= 16777619u; }
    }
    return h;
}

static bool sendTrainingSession(const char* name) {
    if (!unikonStorageReady()) {
        sendVirtualMessage(F("[UNIKON] training: storage not ready"), WARN, false, true);
        return false;
    }
    // Three modes, chosen on the page:
    //   "-"  (UNIKON_TRAIN_MODE_NONE) -> RACE-ONLY test: no pigeon records at all
    //   "*"  (UNIKON_TRAIN_MODE_ALL)  -> the WHOLE pombal (the normal run)
    //   list (UNIKON_TRAIN_MODE_LIST) -> only the ticked pigeons (BENCH TEST ONLY: the poul
    //        upload REPLACES the device's list, so a subset leaves the RAMBOX with fewer)
    const bool raceOnly = (s_trainingMode == UNIKON_TRAIN_MODE_NONE);
    const bool subset   = (s_trainingMode == UNIKON_TRAIN_MODE_LIST);

    // A race is only usable when its pigeons are ALLOCATED to it: the races that come from
    // the club arrive with their pigeons already assigned, so a race written alone does not
    // open and reports no arrivals.
    if (!raceOnly && s_registrationCount == 0) {
        sendVirtualMessage(F("[UNIKON] training: no pombal registrations — sync the pombal first "
                             "(the race needs its pigeons allocated)"), WARN, false, true);
        return false;
    }
    if (raceOnly) {
        sendVirtualMessage(F("[UNIKON] training: RACE ONLY (test) — no pigeon records are sent, "
                             "the race is not expected to open"), WARN, false, true);
    } else if (subset) {
        char wmsg[96];
        snprintf(wmsg, sizeof(wmsg),
                 "[UNIKON] training: SUBSET (%u of %u pigeons) — WARNING: the write REPLACES the "
                 "device's list", (unsigned)s_trainingFilterCount, (unsigned)s_registrationCount);
        sendVirtualMessage(String(wmsg), WARN, false, true);
    }
    if (!raceOnly && !subset) {
        char mmsg[72];
        snprintf(mmsg, sizeof(mmsg), "[UNIKON] training: allocating the whole pombal (%u pigeons)",
                 (unsigned)s_registrationCount);
        sendVirtualMessage(String(mmsg), WARN, false, true);
    }
    char tmpModuleId[44];
    if (!sendVFrameAndReadIdent(tmpModuleId, sizeof(tmpModuleId))) {
        sendVirtualMessage(F("[UNIKON] training: handshake failed"), WARN, false, true);
        return false;
    }
    if (!sendAndWaitAck(CONTENT_IC, sizeof(CONTENT_IC))) {
        sendVirtualMessage(F("[UNIKON] training: IC0 failed"), WARN, false, true);
        return false;
    }
    char dateStr[5] = "0101";
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        snprintf(dateStr, sizeof(dateStr), "%02d%02d", timeinfo.tm_mday, timeinfo.tm_mon + 1);
    }
    if (!sendIFrame(UNIKON_TRAINING_CODE, dateStr, name, UNIKON_I_TAIL_RACE)) {
        sendVirtualMessage(F("[UNIKON] training: I-frame failed"), WARN, false, true);
        return false;
    }
    // Nothing to persist: the training tag is the fixed UNIKON_TRAINING_CODE
    // constant, so a reboot still classifies this race's arrivals as training.
    sendVirtualMessage(String(F("[UNIKON] training: tag=code ")) + String(UNIKON_TRAINING_CODE)
                       + String(F(" date=")) + String(dateStr)
                       + String(F(" name=")) + String(name), WARN, false, true);
    // Replay club context. Skipped (with warning) when nothing was ever read.
    const uint8_t* sFrames[3] = { s_cachedS1, s_cachedS2, s_cachedS3 };
    const uint8_t sLens[3] = { s_cachedS1Len, s_cachedS2Len, s_cachedS3Len };
    for (uint8_t i = 0; i < 3; i++) {
        if (sLens[i] == 0) continue;
        // Never replay an EMPTY context: S1/S2/S3 are a WRITE of the club/solta on the
        // module (the sim proves it -- an empty S1 replaced the club id with spaces).
        // Replaying a blank frame from a module that had no club would erase it.
        bool anyContent = false;
        for (uint8_t k = 3; k < sLens[i]; k++) {
            if (sFrames[i][k] > ' ') { anyContent = true; break; }
        }
        if (!anyContent) {
            char smsg[64];
            snprintf(smsg, sizeof(smsg),
                     "[UNIKON] training: S%u is empty on the module -- not replayed (would erase it)",
                     (unsigned)(i + 1));
            sendVirtualMessage(String(smsg), WARN, false, true);
            continue;
        }
        char smsg[48];
        snprintf(smsg, sizeof(smsg), "[UNIKON] training: replaying S%u (%u bytes)", (unsigned)(i + 1), (unsigned)sLens[i]);
        sendVirtualMessage(String(smsg), WARN, false, true);
        if (!sendAndWaitAck(sFrames[i], sLens[i])) {
            sendVirtualMessage(F("[UNIKON] training: S-frame replay failed"), WARN, false, true);
            return false;
        }
    }
    // Allocation: one pigeon record per registration -- the whole pombal, or only the ticked
    // ones in the subset test mode. Padded to 11 chars so a short/corrupt registration can
    // never leak adjacent memory on the wire. Skipped entirely in the RACE-ONLY mode.
    uint16_t okCount = 0, sentCount = 0;
    for (uint16_t i = 0; !raceOnly && i < s_registrationCount; i++) {
        if (subset && !trainingFilterAllows(s_registrations[i].ring)) continue;
        char ring[12];
        char chip[12];
        memset(ring, '0', 11); ring[11] = '\0';
        memset(chip, '0', 11); chip[11] = '\0';
        size_t ringLen = strlen(s_registrations[i].ring);
        if (ringLen > 11) ringLen = 11;
        memcpy(ring, s_registrations[i].ring, ringLen);
        if (s_registrations[i].hasChip) {
            size_t chipLen = strlen(s_registrations[i].chip);
            if (chipLen > 11) chipLen = 11;
            memcpy(chip, s_registrations[i].chip, chipLen);
        }
        if (sendZPFrame(ring, chip)) okCount++;
        if (sentCount < UNIKON_MAX_ARRIVALS) {
            // Guardar a anilha enviada: no modo LIST o read-back verifica presenca
            // (o modulo nao tem comando "apagar pombo", logo a lista dele e maior).
            strlcpy(s_sentRings[sentCount], ring, sizeof(s_sentRings[0]));
        }
        sentCount++;
        if (sentCount % 10 == 0) {
            char pmsg[48];
            snprintf(pmsg, sizeof(pmsg), "[UNIKON] training: ZP %u/%u (ok %u)",
                     (unsigned)sentCount, (unsigned)s_registrationCount, (unsigned)okCount);
            sendVirtualMessage(String(pmsg), WARN, false, true);
        }
    }
    // Read-back (the DOS program's menu 4, "Receber dados ... do Unikon-Modulo"): re-read the
    // pombal and compare it with what we just allocated. The ACK only proves the frame was
    // accepted -- this is the only way to know the module really stored the records.
    if (sentCount > 0 && subset) {
        // Subconjunto: o protocolo capturado NAO tem "apagar pombo", por isso a RAMBOX
        // fica com o pombal antigo + os que enviamos. Comparar o hash daria sempre
        // MISMATCH; o que interessa e confirmar que TODAS as anilhas enviadas existem la.
        if (runXPhase()) {
            uint16_t present = 0;
            for (uint16_t i = 0; i < sentCount && i < UNIKON_MAX_ARRIVALS; i++) {
                if (isRegistered(s_sentRings[i])) present++;
            }
            char vmsg[128];
            snprintf(vmsg, sizeof(vmsg),
                     "[UNIKON] training: read-back (subset) — %u/%u anilhas enviadas presentes na RAMBOX — %s",
                     (unsigned)present, (unsigned)sentCount,
                     (present == sentCount) ? "OK" : "FALTAM");
            sendVirtualMessage(String(vmsg), WARN, false, true);
        } else {
            sendVirtualMessage(F("[UNIKON] training: read-back failed (no ident/X answer)"), WARN, false, true);
        }
    } else if (sentCount > 0) {
        uint32_t sentHash = pombalRingHash();
        uint16_t sentTotal = s_registrationCount;
        if (runXPhase()) {
            uint32_t backHash = pombalRingHash();
            uint16_t backTotal = s_registrationCount;
            char vmsg[128];
            snprintf(vmsg, sizeof(vmsg),
                     "[UNIKON] training: read-back — sent %u (h=%08lX), module has %u (h=%08lX) — %s",
                     (unsigned)sentTotal, (unsigned long)sentHash,
                     (unsigned)backTotal, (unsigned long)backHash,
                     (backHash == sentHash && backTotal == sentTotal) ? "MATCH" : "MISMATCH");
            sendVirtualMessage(String(vmsg), WARN, false, true);
        } else {
            sendVirtualMessage(F("[UNIKON] training: read-back failed (no ident/X answer)"), WARN, false, true);
        }
    }
    char donemsg[96];
    snprintf(donemsg, sizeof(donemsg), "[UNIKON] training: done — contest=%s date=%s pigeons ok=%u/%u",
             name, dateStr, (unsigned)okCount, (unsigned)sentCount);
    sendVirtualMessage(String(donemsg), WARN, false, true);
    // Success means the RACE was programmed; the pigeon count is reported separately so
    // a "race only" run can be judged on its own.
    return true;
}

// Clear the training contest (UNIKON_TRAINING_CODE) from the RAMBOX calendar.
// Only writes an I-frame with invalid date — never touches ZP/pombal data.
static bool clearTrainingSession() {
    char tmpModuleId[44];
    if (!sendVFrameAndReadIdent(tmpModuleId, sizeof(tmpModuleId))) {
        sendVirtualMessage(F("[UNIKON] clear-training: handshake failed"), WARN, false, true);
        return false;
    }
    if (!sendAndWaitAck(CONTENT_IC, sizeof(CONTENT_IC))) {
        sendVirtualMessage(F("[UNIKON] clear-training: IC0 failed"), WARN, false, true);
        return false;
    }
    if (!sendIFrame(UNIKON_TRAINING_CODE, "0000", "         ", UNIKON_I_TAIL_CLEAR)) {
        sendVirtualMessage(F("[UNIKON] clear-training: I-frame failed"), WARN, false, true);
        return false;
    }
    s_inTrainingRace = false;
    sendVirtualMessage(String(F("[UNIKON] clear-training: done — contest "))
                       + String(UNIKON_TRAINING_CODE) + String(F(" cleared")), WARN, false, true);
    return true;
}

bool unikonIsPolling() {
    return s_pollInProgress || s_passiveListening;
}

bool unikonIsPostPollBlackout() {
    if (s_lastPollEndMs == 0) return false;
    return (millis() - s_lastPollEndMs) < UNIKON_POST_POLL_BLACKOUT_MS;
}

void unikonStartPolling() {
    s_pollingActive = true;
    s_lastPollMs    = 0;  // fire immediately on next unikonUpdate()
}

void unikonStopPolling() {
    s_pollingActive = false;
}

void unikonResetState() {
    s_arrivalCount        = 0;
    s_registrationCount   = 0;
    s_contestCount        = 0;
    s_status.totalKnown   = 0;
    s_status.lastNewCount = 0;
    s_status.registeredCount = 0;
    s_status.contestCount = 0;
    s_status.clubCode[0] = '\0';
    s_status.fancierName[0] = '\0';
    s_status.raceDate[0]  = '\0';  // cleared so next poll re-fetches race info via W-phase
    s_status.raceName[0]  = '\0';
    clearArrivalsStorage();
    clearRegistrationsStorage();
    clearContestsStorage();
    s_pendingEmailStart  = 0;   // reset pending window after counter reset
    s_lastEmailAttemptMs = 0;  // allow immediate send on next arrivals
}

void unikonResetArrivalsOnly() {
    s_arrivalCount = 0;
    s_status.totalKnown = 0;
    s_status.lastNewCount = 0;
    clearArrivalsStorage();
    s_pendingEmailStart = 0;
    s_lastEmailAttemptMs = 0;
}

UnikonStatus unikonGetStatus() {
    return s_status;
}

uint16_t unikonGetArrivals(UnikonArrival* buf, uint16_t maxCount) {
    if (!buf || !unikonStorageReady()) return 0;
    uint16_t count = min(s_arrivalCount, maxCount);
    memcpy(buf, s_arrivals, count * sizeof(UnikonArrival));
    return count;
}

uint16_t unikonGetRegistrations(UnikonArrival* buf, uint16_t maxCount) {
    if (!buf || !unikonStorageReady()) return 0;
    uint16_t count = min(s_registrationCount, maxCount);
    memcpy(buf, s_registrations, count * sizeof(UnikonArrival));
    return count;
}

uint16_t unikonGetContests(UnikonContest* buf, uint16_t maxCount) {
    uint16_t count = min(s_contestCount, maxCount);
    memcpy(buf, s_contests, count * sizeof(UnikonContest));
    return count;
}

UnikonConfig unikonGetConfig() {
    return s_cfg;
}

bool unikonQueuePombalSeasonEmail(String& messageOut) {
    messageOut = "";

    if (!s_cfg.enabled) {
        messageOut = "Modulo Unikon esta desativado.";
        return false;
    }

    if (s_registrationCount == 0) {
        messageOut = "Sem pombos no pombal para enviar.";
        return false;
    }

    String subject = String(F("Rambox32 - Unikon - "))
                   + String(rmbT("Pombal da Epoca (", "Season loft ("))
                   + String((unsigned)s_registrationCount)
                   + String(F(")"));

    String body;
    body.reserve(640 + (size_t)s_registrationCount * 96u);
    ramboxEmailSourceLine(body, "Unikon");
    body += String(rmbT("Relatorio manual do pombal da epoca\n", "Manual season-loft report\n"));

    char moduleId[44];
    char clubName[32];
    char clubCode[8];
    char fancierName[32];
    char soltaName[14];
    sanitizeAsciiText(s_status.moduleId, moduleId, sizeof(moduleId));
    sanitizeAsciiText(s_status.clubName, clubName, sizeof(clubName));
    sanitizeAsciiText(s_status.clubCode, clubCode, sizeof(clubCode));
    sanitizeAsciiText(s_status.fancierName, fancierName, sizeof(fancierName));
    sanitizeAsciiText(s_status.soltaName, soltaName, sizeof(soltaName));

    if (moduleId[0]) body += String(rmbT("Modulo: ", "Module: ")) + String(moduleId) + String(F("\n"));
    if (clubName[0]) body += String(rmbT("Clube: ", "Club: ")) + String(clubName) + String(F("\n"));
    if (clubCode[0]) body += String(rmbT("Codigo Clube: ", "Club code: ")) + String(clubCode) + String(F("\n"));
    if (fancierName[0]) body += String(rmbT("Proprietario: ", "Owner: ")) + String(fancierName) + String(F("\n"));
    if (soltaName[0]) body += String(rmbT("Solta: ", "Release: ")) + String(soltaName) + String(F("\n"));
    body += String(F("\n"));

    const uint8_t noColWidth = 4;
    const uint8_t ringColWidth = 12;
    const uint8_t chipColWidth = 11;

    String sep;
    sep.reserve(96);
    sep += '+';
    for (uint8_t i = 0; i < (uint8_t)(noColWidth + 2); i++) sep += '-';
    sep += '+';
    for (uint8_t i = 0; i < (uint8_t)(ringColWidth + 2); i++) sep += '-';
    sep += '+';
    for (uint8_t i = 0; i < (uint8_t)(chipColWidth + 2); i++) sep += '-';
    sep += "+\n";

    char header[128];
    snprintf(header, sizeof(header), "| %-4s | %-12s | %-11s |\n",
             "No", "Ring", "Chip");

    body += sep;
    body += String(header);
    body += sep;

    for (uint16_t i = 0; i < s_registrationCount; i++) {
        char ringDisplay[16];
        char ringAscii[24];
        char chipAscii[24];
        char idx[8];

        buildDisplayRing(s_registrations[i].ring, ringDisplay, sizeof(ringDisplay));
        sanitizeAsciiText(ringDisplay, ringAscii, sizeof(ringAscii));
        formatPombalChipForAscii(s_registrations[i], chipAscii, sizeof(chipAscii));
        snprintf(idx, sizeof(idx), "%u", (unsigned)(i + 1));

        char row[160];
        snprintf(row, sizeof(row), "| %-4.4s | %-12.12s | %-11.11s |\n",
                 idx,
                 ringAscii[0] ? ringAscii : "-",
                 chipAscii[0] ? chipAscii : "-");
        body += String(row);
    }
    body += sep;
    body += String(rmbT("Total: ", "Total: ")) + String((unsigned)s_registrationCount)
          + String(rmbT(" pombo(s)\n", " pigeon(s)\n"));
    body += buildEmailSignature();

#if ENABLE_SMTP == 1
    if (startUnikonEmailAsync(subject, body)) {
        messageOut = "Email do pombal colocado na fila de envio.";
        return true;
    }
    messageOut = "Email ocupado neste momento. Tente novamente em alguns segundos.";
    return false;
#elif ENABLE_GOOGLE_EMAIL == 1
    String dest = configManager.getEmailDestination();
    if (dest.length() == 0) {
        messageOut = "Destino de email nao configurado.";
        return false;
    }
    if (!googleEmailIsAvailable()) {
        messageOut = "Transporte Google Email indisponivel.";
        return false;
    }
    // T24: arquivo .txt do corpo (UNIKON, hora de envio), SD ou LittleFS.
    { extern bool emailArchiveSaveBody(const String&, const String&); emailArchiveSaveBody(F("UNIKON"), body); }
    googleEmailSendSimple(dest, subject, body);
    messageOut = "Email do pombal enviado.";
    return true;
#else
    messageOut = "Transporte de email indisponivel.";
    return false;
#endif
}

void unikonReloadConfig() {
    // Keep allocation idempotent and ensure storage exists before loadConfig().
    unikonAllocatePsram();
    loadConfig();
    s_pollingActive = s_cfg.enabled && s_cfg.pollIntervalSec > 0;
    if (s_pollingActive && s_lastPollMs == 0) {
        // Keep startup behavior: defer first poll by one interval after reload.
        s_lastPollMs = millis();
    }
}

void unikonPersistCalendarAndPombal() {
    saveConfig();
}
bool unikonFlushPendingConfig() {
    if (!s_configWritePending) return true;
    bool saved = saveConfig();
    if (saved) {
        s_configWritePending = false;
        sendVirtualMessage(F("[UNIKON] Deferred config persisted to LittleFS"), WARN, false, true);
        return true;
    }
    return false;
}

void unikonSetConfig(const UnikonConfig& cfg) {
    bool wasOn = s_cfg.enabled;
    bool wasManual = s_cfg.manualSerial;
    s_cfg = cfg;
    // Keep manual mode as 0 and migrate short intervals to the default (3 minutes).
    if (s_cfg.pollIntervalSec > 0 && s_cfg.pollIntervalSec < 60) {
        s_cfg.pollIntervalSec = 180;
    }
    bool saved = saveConfig();
    if (!saved) {
        s_configWritePending = true;
        sendVirtualMessage(F("[UNIKON] ⚠️ saveConfig failed - config will not persist after reboot"), WARN, false, true);
    } else {
        s_configWritePending = false;
    }
    s_pollingActive = s_cfg.enabled && s_cfg.pollIntervalSec > 0;
    if (s_cfg.enabled && (!wasOn || s_cfg.manualSerial != wasManual)) {
        // Selecting Unikon switches the RS232 port to the RAMBOX profile (or to the
        // RS232 page settings in manual/bench mode) and releases the other module.
        enforceBriconExclusion();
        unikonApplySerialProfile();
    }
}

#endif // ENABLE_UNIKON == 1
