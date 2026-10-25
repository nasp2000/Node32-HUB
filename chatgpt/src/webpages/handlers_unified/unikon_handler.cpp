#include "unikon_handler.h"
#include "../../feature_flags.h"
#include "../../languages/lang_active.h"

#if ENABLE_UNIKON == 1

#include <Arduino.h>
#include "../../unikon_protocol.h"
#include "../../bricon_protocol.h"     // activeModule ("rambox"|"bricon"|"none")
#include "../../ConfigManager.h"
#include "../../psram_helpers.h"

extern ConfigManager configManager;

static UnikonArrival* allocTempArray() {
    size_t bytes = (size_t)UNIKON_MAX_ARRIVALS * sizeof(UnikonArrival);
    return (UnikonArrival*)(psramFound() ? ps_malloc(bytes) : malloc(bytes));
}

void handleUnikonStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    UnikonStatus s  = unikonGetStatus();
    UnikonConfig  c = unikonGetConfig();
    uint32_t agoSec = s.lastPollMs > 0 ? (millis() - s.lastPollMs) / 1000UL : 0;
    uint16_t raceKnownCount = 0;
    uint16_t trainingKnownCount = 0;
    UnikonArrival* arrivals = allocTempArray();
    if (arrivals) {
        uint16_t arrivalCount = unikonGetArrivals(arrivals, UNIKON_MAX_ARRIVALS);
        for (uint16_t i = 0; i < arrivalCount; i++) {
            if (arrivals[i].arrivalTime[0] == '\0' || memcmp(arrivals[i].arrivalTime, "000000", 6) == 0) continue;
            if (arrivals[i].isTraining) trainingKnownCount++;
            else raceKnownCount++;
        }
        free(arrivals);
    } else {
        raceKnownCount = s.totalKnown;
    }

    String emailDest = configManager.getEmailDestination();
    char buf[1400];
    snprintf(buf, sizeof(buf),
        "{"
        "\"connected\":%s,"
        "\"lastPollAgoSec\":%lu,"
        "\"totalKnown\":%u,"
        "\"raceKnownCount\":%u,"
        "\"trainingKnownCount\":%u,"
        "\"lastNewCount\":%u,"
        "\"registeredCount\":%u,"
        "\"contestCount\":%u,"
        "\"moduleId\":\"%s\","
        "\"clubName\":\"%s\","
        "\"soltaName\":\"%s\","
        "\"clubCode\":\"%s\","
        "\"fancierName\":\"%s\","
        "\"raceDate\":\"%s\","
        "\"raceName\":\"%s\","
        "\"enabled\":%s,"
        "\"intervalSec\":%u,"
        "\"activeModule\":\"%s\","
        "\"manualSerial\":%s,"
        "\"polling\":%s,"
        "\"passiveListen\":%s,"
        "\"officialFormat\":%s,"
        "\"emailDest\":\"%s\""
        "}",
        s.connected          ? "true"  : "false",
        (unsigned long)agoSec,
        (unsigned)s.totalKnown,
        (unsigned)raceKnownCount,
        (unsigned)trainingKnownCount,
        (unsigned)s.lastNewCount,
        (unsigned)s.registeredCount,
        (unsigned)s.contestCount,
        s.moduleId,
        s.clubName,
        s.soltaName,
        s.clubCode,
        s.fancierName,
        s.raceDate,
        s.raceName,
        c.enabled            ? "true"  : "false",
        (unsigned)c.pollIntervalSec,
#if ENABLE_BRICON == 1
        briconActiveModuleName(),
#else
        (c.enabled ? "rambox" : "none"),
#endif
        c.manualSerial       ? "true"  : "false",
        (c.enabled && c.pollIntervalSec > 0) ? "true" : "false",
        c.passiveListen       ? "true"  : "false",
        c.officialFormat      ? "true"  : "false",
        emailDest.c_str()
    );

    res.code = 200;
    res.contentType = F("application/json");
    res.body = String(buf);
}

void handleUnikonArrivalsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    bool officialFormat = unikonGetConfig().officialFormat;
    UnikonArrival* arrivals = allocTempArray();
    if (!arrivals) {
        res.sendJson(F("{\"count\":0,\"arrivals\":[]}"));
        return;
    }
    uint16_t count = unikonGetArrivals(arrivals, UNIKON_MAX_ARRIVALS);

    String json;
    json.reserve(count * 40 + 20);
    json = "{\"count\":";
    json += count;
    json += ",\"arrivals\":[";
    for (uint16_t i = 0; i < count; i++) {
        char ringDisplay[16];
        char chipDisplay[16];
        unikonFormatDisplayRing(arrivals[i].ring, ringDisplay, sizeof(ringDisplay));
        unikonFormatDisplayChip(arrivals[i].chip, arrivals[i].hasChip, chipDisplay, sizeof(chipDisplay));
        if (i > 0) json += ',';
        json += "{\"ring\":\"";
        json += officialFormat ? ringDisplay : arrivals[i].ring;
        json += "\",\"chip\":\"";
        json += officialFormat ? chipDisplay : arrivals[i].chip;
        json += "\",\"time\":\"";
        json += arrivals[i].arrivalTime;
        json += "\",\"date\":\"";
        json += arrivals[i].arrivalDate;
        json += "\",\"kind\":\"";
        json += arrivals[i].isTraining ? "training" : "race";
        json += "\",\"race\":\"";
        json += arrivals[i].race;
        json += "\"}";
    }
    json += "]}";

    free(arrivals);
    res.code = 200;
    res.contentType = F("application/json");
    res.body = json;
}

void handleUnikonRegistrationsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    bool officialFormat = unikonGetConfig().officialFormat;
    UnikonArrival* regs = allocTempArray();
    if (!regs) {
        res.sendJson(F("{\"count\":0,\"registrations\":[]}"));
        return;
    }
    uint16_t count = unikonGetRegistrations(regs, UNIKON_MAX_ARRIVALS);

    String json;
    json.reserve(count * 36 + 24);
    json = "{\"count\":";
    json += count;
    json += ",\"registrations\":[";
    for (uint16_t i = 0; i < count; i++) {
        char ringDisplay[16];
        char chipDisplay[16];
        unikonFormatDisplayRing(regs[i].ring, ringDisplay, sizeof(ringDisplay));
        unikonFormatDisplayChip(regs[i].chip, regs[i].hasChip, chipDisplay, sizeof(chipDisplay));
        if (i > 0) json += ',';
        json += "{\"ring\":\"";
        json += officialFormat ? ringDisplay : regs[i].ring;
        json += "\",\"chip\":\"";
        json += officialFormat ? chipDisplay : regs[i].chip;
        json += "\"}";
    }
    json += "]}";

    free(regs);
    res.code = 200;
    res.contentType = F("application/json");
    res.body = json;
}

void handleUnikonContestsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    UnikonContest contests[UNIKON_MAX_CONTESTS];
    uint16_t count = unikonGetContests(contests, UNIKON_MAX_CONTESTS);

    String json;
    json.reserve(count * 48 + 20);
    json = "{\"count\":";
    json += count;
    json += ",\"contests\":[";
    for (uint16_t i = 0; i < count; i++) {
        if (i > 0) json += ',';
        json += "{\"code\":\"";
        json += contests[i].code;
        json += "\",\"date\":\"";
        json += contests[i].date;
        json += "\",\"name\":\"";
        json += contests[i].name;
        json += "\"}";
    }
    json += "]}";

    res.code = 200;
    res.contentType = F("application/json");
    res.body = json;
}

void handleUnikonPombalGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    UnikonArrival* registrations = allocTempArray();
    if (!registrations) {
        res.sendJson(F("{\"count\":0,\"registrations\":[]}"));
        return;
    }
    uint16_t count = unikonGetRegistrations(registrations, UNIKON_MAX_ARRIVALS);

    String json;
    json.reserve(count * 48 + 20);
    json = "{\"count\":";
    json += count;
    json += ",\"registrations\":[";
    for (uint16_t i = 0; i < count; i++) {
        if (i > 0) json += ',';
        json += "{\"ring\":\"";
        json += registrations[i].ring;
        json += "\",\"chip\":\"";
        json += registrations[i].chip;
        json += "\"}";
    }
    json += "]}";

    free(registrations);
    res.code = 200;
    res.contentType = F("application/json");
    res.body = json;
}

void handleUnikonSyncPombalPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    UnikonConfig c = unikonGetConfig();
    if (!c.enabled) {
        res.sendJson(F("{\"success\":false,\"message\":\"Unikon module is disabled.\"}"));
        return;
    }
    unikonRequestXOnlyPoll();
    res.sendJson(F("{\"success\":true,\"message\":\"Pigeon loft sync scheduled.\"}"));
}

void handleUnikonEmailPombalPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String msg;
    bool ok = unikonQueuePombalSeasonEmail(msg);
    String json = String(F("{\"success\":")) + (ok ? "true" : "false") +
                  String(F(",\"message\":\"")) + msg + String(F("\"}"));
    res.sendJson(json);
}

void handleUnikonPollPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    UnikonConfig c = unikonGetConfig();
    if (!c.enabled) {
        res.sendJson(F("{\"success\":false,\"message\":\"Unikon module is disabled.\"}"));
        return;
    }
    unikonRequestWOnlyPoll();
    res.sendJson(F("{\"success\":true,\"message\":\"Arrivals poll (W) scheduled.\"}"));
}

void handleUnikonSyncZpPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    UnikonConfig c = unikonGetConfig();
    if (!c.enabled) {
        res.sendJson(F("{\"success\":false,\"message\":\"Unikon module is disabled.\"}"));
        return;
    }
    unikonRequestPoll();
    res.sendJson(F("{\"success\":true,\"message\":\"Manual ZP sync (X+W) scheduled.\"}"));
}

void handleUnikonPollWPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    UnikonConfig c = unikonGetConfig();
    if (!c.enabled) {
        res.sendJson(F("{\"success\":false,\"message\":\"Unikon module is disabled.\"}"));
        return;
    }
    unikonRequestWOnlyPoll();
    res.sendJson(F("{\"success\":true,\"message\":\"W-poll (arrivals) scheduled.\"}"));
}

void handleUnikonSyncCalendarPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    UnikonConfig c = unikonGetConfig();
    if (!c.enabled) {
        res.sendJson(F("{\"success\":false,\"message\":\"Unikon module is disabled.\"}"));
        return;
    }
    unikonRequestIcOnlyPoll();
    res.sendJson(F("{\"success\":true,\"message\":\"Calendar update scheduled.\"}"));
}

void handleUnikonResetPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    unikonResetArrivalsOnly();
    res.sendJson(F("{\"success\":true,\"message\":\"Count reset.\"}"));
}


#else
// Stubs when ENABLE_UNIKON == 0
static void stubNotFound(HttpResponse& res) {
    res.code = 404;
    res.contentType = F("application/json");
    res.body = F("{\"error\":\"Unikon not enabled\"}");
}
void handleUnikonStatusGet(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleUnikonArrivalsGet(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleUnikonRegistrationsGet(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleUnikonContestsGet(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleUnikonPombalGet(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleUnikonSyncPombalPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleUnikonEmailPombalPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleUnikonPollPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleUnikonSyncZpPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleUnikonPollWPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleUnikonSyncCalendarPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
void handleUnikonResetPost(HttpRequest& req, HttpResponse& res) { (void)req; stubNotFound(res); }
#endif
