// unikon_protocol.h
// Unikon RAMBOX RS-232 protocol driver.
// Polls the RAMBOX for registered pigeons and detects new arrivals.
//
// Protocol: 9600 8N1 no flow control.
// Frame structure: STX(02) + ADDR(81) + PAYLOAD + CK1 + CK2 + ETX(03)
// Checksum: 16-bit sum of ADDR+payload bytes.
//   CK1 = low byte, escaped with |0x80 when < 0x20 (never emit a control byte inside
//         the frame; the RAMBOX sender does the same -- see computeChecksum()).
//   CK2 = 0x80 | (high byte & 0x7F)
//
// Polling strategy: RAMBOX is empty before each race. Any pigeon returned by
// the X-phase is only a registered ring/chip entry. Confirmed arrivals come
// from P-frames in the W-phase, where the RAMBOX provides HHMMSS/DDMM.

#pragma once
#include <Arduino.h>
#include "feature_flags.h"

#if ENABLE_UNIKON == 1

// Max total pigeons tracked per session (ring+chip+time+date+race, ~50 bytes each;
// PSRAM-backed: 500 entries x2 arrays fit comfortably; covers the 404-ZP capture)
#ifndef UNIKON_MAX_ARRIVALS
#define UNIKON_MAX_ARRIVALS 500
#endif

#ifndef UNIKON_MAX_CONTESTS
#define UNIKON_MAX_CONTESTS 64
#endif

// Protocol byte constants
#define UNIKON_STX   0x02
#define UNIKON_ETX   0x03
#define UNIKON_ADDR  0x81
#define UNIKON_ACK   0x06
#define UNIKON_NAK   0x15
#define UNIKON_EOT   0x04

// One pigeon record.
// Layout confirmed against sniffer captures 9.txt / 12.txt (P-frame) and 11.txt (ZP-frame).
//
// P-frame (W-operation) layout — 39 bytes:
//   STX(0) ADDR(1) 'P'(2) RING[11](3..13) SP(14) CHIP[11](15..25) HHMMSS[6](26..31) DDMM[4](32..35) CK1(36) CK2(37) ETX(38)
//
// ZP-frame (X-operation) layout — 30 bytes:
//   STX(0) ADDR(1) 'Z'(2) 'P'(3) RING[11](4..14) SP(15) CHIP[11](16..26) CK1(27) CK2(28) ETX(29)
// ZP-frames carry NO arrival time; arrivalTime is set to "" for those records.
// Struct reordered for minimal padding: strings + bools grouped. Size: 39 bytes (no padding waste).
struct UnikonArrival {
    char ring[12];        // 11-char ring number (e.g. "23003377475") + null
    char chip[12];        // 11-char ETS chip serial (e.g. "CD5E5464001") + null;
                          // "00000000000" when no transponder present
    char arrivalTime[7];  // HHMMSS arrival time (6 chars, e.g. "095518") + null;
                          // "000000" or "" when pigeon has not yet arrived
    char arrivalDate[5];  // DDMM arrival date (4 chars, e.g. "2306") + null;
                          // "0000" or "" when not available
    bool hasChip;         // true when chip field is not all zeros (packed after strings, no padding)
    bool isTraining;      // true when entry was captured as training (packed after strings)
    char race[12];        // race name from the enclosing frame '3' (e.g. "CANAME"), "" if unknown
};

// One contest/schedule record returned by the IC0/I session.
// Layout confirmed against sniffer captures 1.txt / 6.txt:
//   STX(0) ADDR(1) 'I'(2) CODE[3](3..5) DDMM[4](6..9) NAME[8](10..17) N[1](18) CK1 CK2 ETX
// The I-frame content is 'I' '0' RR[2] DD[2] MM[2] NAME[8] N[1] (the leading '0' is a
// literal -- see sendIFrame()): NAME is EIGHT characters and the last byte is a digit.
struct UnikonContest {
    char code[4];         // 3-digit contest index, e.g. "001"
    char date[5];         // DDMM date, e.g. "0703"
    char name[10];        // 9-char name, right-padded with spaces in RAMBOX
};

// Runtime state snapshot exposed to the web UI
struct UnikonStatus {
    bool     connected;          // true if last full poll succeeded
    uint32_t lastPollMs;         // millis() at end of last poll
    uint16_t totalKnown;         // total confirmed arrivals cached this session
    uint16_t lastNewCount;       // new confirmed arrivals found in the last poll
    uint16_t registeredCount;    // ZP registrations cached from the last X-phase
    uint16_t contestCount;       // IC0/I contest records cached from RAMBOX
    char     moduleId[44];       // RAMBOX identification string (from v-frame)
    char     clubName[32];       // club name from S1 frame
    char     soltaName[14];      // solta name from S2 frame
    char     clubCode[8];        // 7-digit club code from S2/S3 frames
    char     fancierName[32];    // fancier name from S3 frame
    char     raceDate[8];        // race date from W-frame '3' e.g. "15.06"
    char     raceName[12];       // race name from W-frame '3' e.g. "CANAME"
    char     raceCode[4];        // contest code from W-frame '3' bytes [9..11] e.g. "003"
};

// Persistent configuration stored in /config/unikon.json
struct UnikonConfig {
    bool     enabled;            // master enable flag
    uint16_t pollIntervalSec;    // auto-poll interval in seconds (0 = manual only)
    // When true, unikonUpdate() watches for spontaneous frames from the RAMBOX between polls.
    // Useful to determine whether the RAMBOX ever pushes arrival data without being asked.
    // Default: false (polling-only mode).
    bool     passiveListen;
    // When true, ring/chip values are rendered in the official federation-friendly format.
    // Example ring: 23003379958 -> 23-3379958
    // Example chip: C9933525001 -> C9933525
    bool     officialFormat;
};

// ----- Public API -----

// Initialize: load config from LittleFS and open the UART if enabled.
void unikonBegin();
// True when the Unikon module is the selected one (mutual exclusion with Bricon).
bool unikonIsEnabled();
// Apply the RAMBOX RS-232 profile (9600 8N1). Called when Unikon becomes active so
// choosing the module switches the port automatically (Bricon needs 8N2 instead).
void unikonApplySerialProfile();

// Called from loop(): drives the auto-poll timer and the pending-poll flag.
// Blocking (up to ~10 s) while a poll is in progress — only runs when interval fires.
void unikonUpdate();

// Trigger an immediate full poll cycle (X + W). Returns true on success.
// Use this for manual ZP snapshot refresh.
// NOTE: blocks for up to ~10 s while communicating with the RAMBOX.
bool unikonPollNow();

// Request a full poll (X + W) from a web handler (non-blocking — sets a flag for unikonUpdate).
void unikonRequestPoll();

// Request a W-only poll (arrivals refresh, skips X-phase ZP registration read).
// This is the default for periodic/normal polling.
// Non-blocking — sets a flag for unikonUpdate().
void unikonRequestWOnlyPoll();

// Request an IC-only poll (calendar/contests refresh, skips X/W phases).
// Non-blocking — sets a flag for unikonUpdate().
void unikonRequestIcOnlyPoll();

// Request an X-only poll (pombal/registrations refresh, skips W/IC phases).
// Non-blocking — sets a flag for unikonUpdate().
void unikonRequestXOnlyPoll();

// Start/stop the automatic polling timer.
void unikonStartPolling();
void unikonStopPolling();

// Clear the known-pigeon set (call when a new race is about to start).
void unikonResetState();

// Clear only arrivals counter/data. Keeps calendar (contests) and pombal (registrations).
void unikonResetArrivalsOnly();

// Returns true while a poll cycle is in progress (RS232 loop must not consume bytes during this time).
bool unikonIsPolling();

// Returns true for UNIKON_POST_POLL_BLACKOUT_MS after the last poll ended.
// Prevents residual RAMBOX bytes from leaking into bufferDados.
bool unikonIsPostPollBlackout();

// Read current status snapshot.
UnikonStatus unikonGetStatus();

// Copy up to maxCount arrival records into buf[]. Returns count copied.
uint16_t unikonGetArrivals(UnikonArrival* buf, uint16_t maxCount);

// Copy up to maxCount ZP-registration records into buf[]. Returns count copied.
// Registrations are pigeons read from the X-phase (ring + chip, no arrival time).
uint16_t unikonGetRegistrations(UnikonArrival* buf, uint16_t maxCount);

// Copy up to maxCount contest records into buf[]. Returns count copied.
uint16_t unikonGetContests(UnikonContest* buf, uint16_t maxCount);

// Config accessors.
UnikonConfig unikonGetConfig();
void unikonSetConfig(const UnikonConfig& cfg);
#include "unikon_checksum.h"
// Re-apply config from LittleFS after deferred mount (called from taskInitFlash).
void unikonReloadConfig();
// Persist pending in-memory config after LittleFS becomes available.
bool unikonFlushPendingConfig();

// Queue a manual email with all seasonal pigeons in a fixed-width ASCII table.
// Returns true when the email was accepted by the transport pipeline.
bool unikonQueuePombalSeasonEmail(String& messageOut);

// Request to send a training session to the RAMBOX: writes IC0 + I-frame (today's date)
// plus cached S1/S2/S3 and pigeon records for the selected entry list, so the W-phase
// returns arrivals.
// Non-blocking — sets a flag for unikonUpdate().
void unikonRequestSendTraining();

// Set the training entry list. Accepted forms:
//   "<ring>\n<ring>..."  or S4 lines  -> only those rings   (UNIKON_TRAIN_MODE_LIST)
//   "*"                               -> whole pombal       (UNIKON_TRAIN_MODE_ALL)
//   "-"                               -> no pigeons at all  (UNIKON_TRAIN_MODE_NONE)
//   "" (empty)                        -> whole pombal       (legacy meaning, unchanged)
// Returns the number of rings parsed (0 for "*", "-" and "").
uint16_t unikonSetTrainingFilter(const char* text);

// Request to clear the training contest (UNIKON_TRAINING_CODE) from the RAMBOX calendar.
// Non-blocking — sets a flag for unikonUpdate().
void unikonRequestClearTraining();

// ----- Push (device-initiated arrivals) -----
// The vendor driver's uniActivatePush()/uniDeactivatePush() send the frames "P1"/"P0".
// The push is ADDITIVE and AUTOMATIC: the configured auto-poll keeps running as the
// guarantee, and as soon as a RAMBOX is identified the hub asks it once for the push (per
// device, see pushCheckDevice()). The pushed frames are read by the passive listener, so an
// arrival can show up well before the next poll. Nothing is disabled by the push.

// Ask the RAMBOX to push ("P1") or to stop ("P0") — normally not needed: the automatic
// path does it on identification. Non-blocking — sets a flag for unikonUpdate().
void unikonRequestPush(bool on);

// Enable/disable the automatic per-device probe (default: enabled).
void unikonSetPushAuto(bool on);

bool        unikonIsPushAuto();
bool        unikonIsPushActive();
uint8_t     unikonGetPushState();    // 0 = off, 1 = active, 2 = unsupported/rejected
uint16_t    unikonGetPushCode();     // 0 = ACK, 0x3f1 = NAK, 0x3ef = no answer
const char* unikonGetPushDevice();   // ident the push was negotiated with
const char* unikonGetPushVersion();  // ident captured by the last probe

// Force save of current contests/registrations to persistent config.
// Called automatically after successful IC/X polls.
void unikonPersistCalendarAndPombal();

#endif // ENABLE_UNIKON == 1
