// bricon_protocol.h
// Bricon loft-module driver — wire layer implemented from the REA capture
// (framing/CK, ActivateMaster/BA scan, GetFancier -> GetPigeon chain -> GetTimer,
// GetFlightNames, GetBandTable). Not yet validated against real hardware.
//
// Same pack as Rambox32: only ONE module may be active at a time
// (rambox XOR bricon). Enabling one disables the other and applies that
// module's RS232 profile automatically.

#pragma once
#include <Arduino.h>
#include "feature_flags.h"

#if ENABLE_BRICON == 1

// Storage caps (PSRAM-backed; cover a full loft dump).
#ifndef BRICON_MAX_ARRIVALS
#define BRICON_MAX_ARRIVALS 500
#endif
#ifndef BRICON_MAX_CONTESTS
#define BRICON_MAX_CONTESTS 64
#endif

// Persistent configuration stored in /config/bricon.json (atomic write).
struct BriconConfig {
    bool     enabled;            // master enable flag; exclusive with Unikon
    uint16_t pollIntervalSec;    // 0 = manual
    int      baudrate;           // 9600 base; 38400 only for the firmware-update path
    int      dataBits;           // 5..8 (default 8)
    char     parity[8];          // "none", "even", "odd" (default "none")
    // REA: Communication::StartComm builds SerialPort(port, baud, Parity.None, 8,
    // StopBits.Two) => the Bricon link is 9600 8N2. (Unikon is 8N1.)
    int      stopBits;           // 1..2 (default 2)
    // Record layout is decided by the COUNTRY, not by the device:
    // Utils::IsUKProtocol / GetPigeonCommand::HandleResponse. PT => FedBand 14 B,
    // 53-byte arrival record, non-UK fancier (104 B).
    char     country[4];         // "PT", "UK", "BE", ... (default "PT")
    // BA address of the clock: 0 = auto-scan the recovered list
    // [0x10, 0x80..0x85, 0x00]; else an explicit Adresses value.
    int      baAddress;          // 0 = auto, else 0x00/0x10/0x80..0x85
    // GetPigeon carries the club and flight codes. "0000" is the wildcard the app
    // itself tolerates (it substitutes the requested ids when the clock echoes 0000).
    char     clubId[5];          // 4 chars, default "0000"
    char     flightId[5];        // 4 chars, default "0000"
};

// One arrival/registration record. Fields mirror what Bricon actually carries
// (BCEData.xml + Print Manager pipe): ring, chip, sex, colour, age category,
// speed, antenna, club positions — not just Unikon's ring/chip/time.
struct BriconArrival {
    char     ring[16];        // federation ring, e.g. "BE-10-1100002" + null
    char     chip[12];        // ETS chip (8-hex on Bricon) + null, "" when absent
    char     arrivalTime[7];  // HHMMSS + null, "000000"/"" when not arrived
    char     arrivalDate[7];  // DDMMYY + null, "" when not available
    char     color[16];       // colour/mark, e.g. "BLUE" ("" when unknown)
    char     sex;             // 'M', 'F' or '?' (FEMALE in BCEData)
    uint8_t  category;        // 0 unknown, 1 old, 2 yearling, 3 young
    uint16_t speed;           // m/min (0 when unknown)
    uint16_t speed2;          // second speed field (Print Manager Speed2)
    uint8_t  antenna;         // antenna number that clocked (0 = unknown)
    uint16_t posArriveClub;   // arrival position in club (0 = none)
    uint16_t posEnterClub;    // basketing position in club (0 = none)
    bool     hasChip;
    char     evaluation;      // raw Evaluation byte from the wire: '1' clocked (valid
                              // arrival), '0' unclassified ("poul letter"). Labels like
                              // Reset/Basketed/Designated are UI strings, not enum values.
    bool     isTraining;      // training block (derived from the enclosing flight, NOT Eval)
    char     race[16];        // race name/code of the enclosing block, "" if unknown
};

// One contest (race/flight) announced by the device.
struct BriconContest {
    char     code[8];         // race code (Bricon: 4 chars, e.g. "NOY1")
    char     date[12];        // release date as shown (e.g. "16/06/2012")
    char     name[24];        // race name (e.g. "NOYON")
    char     club[24];        // club/federation label
    uint32_t distance;        // release distance in metres (0 = unknown)
    uint16_t pigeonCount;     // pigeons in the race (0 = unknown)
    bool     emailSent;       // already notified by email (Print Manager flag)
    bool     webSent;         // already published (Print Manager flag)
};

// Runtime snapshot exposed to the web UI.
struct BriconStatus {
    bool     connected;          // true once any RX byte was seen this session
    bool     hardwarePending;    // true until the wire has been exercised (no clock seen yet)
    uint32_t lastRxMs;           // millis() of last received byte
    uint32_t bytesRx;            // total bytes drained this session
    uint16_t framesSeen;         // frames exchanged on the wire (requests + replies)
    uint8_t  baAddress;          // address the clock answered on (0 = none yet)
    uint16_t lastNewCount;       // arrivals ingested by the last poll
    uint32_t lastPollMs;         // millis() of the last poll
    char     activeModule[8];    // "rambox", "bricon" or "none"
    char     fancierLic[16];     // licence from GetFancier (0x51)
    char     fancierName[32];    // fancier name from GetFancier (0x51)
};

// ----- Lifecycle -----
void briconBegin();   // load config, enforce exclusion, request RS232 profile
void briconUpdate();  // passive RX sniffer (non-blocking); logs hex to monitor

// ----- Module selection (mutual exclusion enforced here) -----
void briconSetEnabled(bool on);
bool briconIsActive();  // enabled && selected (i.e. Unikon not enabled)
bool briconIsEnabled();  // the raw enable flag (used by the Unikon side for exclusion)
const char* briconActiveModuleName();  // "rambox" | "bricon" | "none"

// ----- Config -----
BriconConfig briconGetConfig();
void briconSetConfig(const BriconConfig& cfg);  // enabling switches RS232 profile

// ----- Status snapshot -----
BriconStatus briconGetStatus();

// ----- Poll/calendar/pombal requests (structure ready, wire driver TBD) -----
// All non-blocking: set flags consumed by briconUpdate().
void briconRequestPoll();        // full: calendar + pombal + arrivals
void briconRequestWOnlyPoll();   // arrivals only (default periodic)
void briconRequestIcOnlyPoll();  // calendar only
void briconRequestXOnlyPoll();   // pombal only
bool briconPollNow();            // immediate (blocking) full poll
void briconStartPolling();
void briconStopPolling();

// ----- RS232 profiles -----
// Applies the active module's serial profile (baud/parity/data/stop, pins untouched)
// through the deferred _pendingRS232Apply mechanism.
void briconApplySerialProfile();

// ----- Data accessors (PSRAM-backed, like Unikon) -----
uint16_t briconGetArrivals(BriconArrival* buf, uint16_t maxCount);
uint16_t briconGetRegistrations(BriconArrival* buf, uint16_t maxCount);
uint16_t briconGetContests(BriconContest* buf, uint16_t maxCount);
uint16_t briconGetArrivalCount();
uint16_t briconGetRegistrationCount();

// ----- Ingestion funnel (single entry point for poll parser AND spontaneous frames) -----
// Returns true when the record was stored/updated. Never blocks.
bool briconIngestArrival(const BriconArrival& src);

#endif  // ENABLE_BRICON == 1
