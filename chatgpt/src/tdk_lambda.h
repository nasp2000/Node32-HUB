#pragma once
// TDK-Lambda / Genesys SCPI console + CSV module (page /tdk-lambda).
//
// Design rules (see docs/Fonte/COMANDOS.md §9):
//  - The module NEVER consumes serial bytes: loop_impl.h copies each RS232 byte
//    in here (tdkLambdaOnRxByte) so /monitor keeps showing every line.
//  - TX goes through the shared RS232 queue, and the sent line is echoed into
//    the monitor buffer (TX visible next to RX).
//  - CSV follows the telemetry standard: dual sink (SD, else LittleFS at the
//    identical path /{sys}/TDK_DD-MM-YYYY.csv), configured cadence, ring-free
//    (rows are tiny), cleanup-covered by the dynamic /{sys}/ root.

#include <Arduino.h>
#include "feature_flags.h"
#include "tdk_lambda_scpi.h"   // pure parsers/helpers (dependency-free)

#if ENABLE_TDK_LAMBDA == 1

struct TdkCommandInfo {
    const char* cmd;      // "DVC?"
    const char* group;    // "Monitor"
    const char* usage;    // "DVC?"
    const char* desc;     // what it does
    const char* reply;    // typical reply
    const char* notes;    // caveats
};

struct TdkLogEntry {
    uint32_t ageMs;
    char direction[4];   // "TX" / "RX" / "SYS"
    char text[160];
};

struct TdkSample {
    uint32_t ts;   // millis()
    float    v;
    float    i;
};

struct TdkValues {
    bool    valid;
    float   vMeas;
    float   vSet;
    float   iMeas;
    float   iSet;
    float   ovp;
    float   uvl;
    char    mode[4];     // "CV" / "CC" / "OFF" / "--"
    bool    outOn;
    uint32_t lastUpdateMs;
};

struct TdkStatus {
    bool     enabled;
    bool     polling;
    bool     csvEnabled;
    bool     csvSensorFormat;   // true = key/value line like the sensor CSV
    uint32_t pollIntervalMs;
    uint32_t lastRxAgeMs;
    uint32_t csvRows;
    uint16_t logCount;
    char     lastCommand[32];
    char     profileMode[8];     // "dvc" | "split"
    bool     autoMap;            // automatic layout detection enabled
    bool     listen;             // true = no TX at all (passive listener)
    char     autoNote[64];       // last automatic adjustment
    char     mapUsed[20];        // effective map ("0,1,2,3,4,5")
    float    scaleVUsed;
    float    scaleIUsed;
    char     lastLine[96];
    char     idn[40];        // IDN? reply (model)
    char     rev[16];        // REV? reply (firmware)
    char     sn[24];         // SN? reply (serial)
    char     lastError[8];   // last C0x/E0x seen
    uint32_t lastErrorAgeMs;
    uint8_t  address;        // multi-drop address (0 = point-to-point)
    bool     inject;         // sensor-style line into the monitor pipeline
    bool     alertActive;    // a limit is being exceeded right now
    char     alertText[64];  // which limit (for the page badge)
    char     csvPath[64];
    bool     csvOnSd;
};

void tdkLambdaBegin();
void tdkLambdaUpdate();
void tdkLambdaOnRxByte(char c);
bool tdkLambdaSend(const String& command);
void tdkLambdaClearLog();
TdkStatus tdkLambdaGetStatus();
TdkValues tdkLambdaGetValues();
uint16_t tdkLambdaCopyLog(TdkLogEntry* out, uint16_t maxEntries);
// Recent samples (in-memory ring, newest last). Returns the copied count.
uint16_t tdkLambdaCopySamples(TdkSample* out, uint16_t maxEntries);
// One sensor-style line pending for the monitor/chart pipeline (opt-in).
bool tdkLambdaTakeInjectLine(char* out, size_t maxLen);
const TdkCommandInfo* tdkLambdaCatalog(size_t* count);
bool tdkLambdaCsvFlushNow();
String tdkLambdaCsvPath();
void tdkLambdaSetPoll(bool enabled, uint32_t intervalMs);
void tdkLambdaSetCsv(bool enabled);
void tdkLambdaSetCsvFormat(bool sensorFormat);
void tdkLambdaSetAutoMap(bool enabled);
void tdkLambdaSetListen(bool listen);
void tdkLambdaSetAddress(uint8_t address);
void tdkLambdaSetInject(bool enabled);
void tdkLambdaSetLimits(float vMin, float vMax, float iMax, bool enabled);
// Device profile: which queries to poll and how to map the reply fields.
//   mode      "dvc"  -> one query returns several numbers (default DVC?)
//             "split"-> separate voltage and current queries
//   map       "0,1,2,3,4,5" = indexes of Vmeas,Vset,Imeas,Iset,OVP,UVL
//   scale/off applied after the map (mV->V, calibration)
void tdkLambdaApplyProfile(const char* mode, const char* vCmd, const char* iCmd,
                           const char* stateCmd, const char* map,
                           float scaleV, float scaleI, float offV, float offI);

#endif // ENABLE_TDK_LAMBDA == 1
