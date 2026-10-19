#pragma once
// rambox_email.h — shared email layout for every Rambox family (Unikon, Bricon and
// future clocks). Same subject and same header block everywhere, in the language
// selected on the hub (activeLangCode: "pt" or "en"):
//
//   Registos captados pelo módulo <device>:      /  Records captured by the <device> module:
//
//   Clube: <clube> (<código>)                    /  Club: ...
//   Concorrente: <columbófilo>                   /  Fancier: ...
//   Prova: <prova> (<data>)                      /  Race: ...
//   Solta: <local>                               /  Release: ...
//
// Empty fields are omitted. Each driver builds it from whatever it knows, so the
// reader sees the same document regardless of the clock.
//
// Header-only (inline) so both translation units can use it without linking.

#include <Arduino.h>
#include "languages/lang_active.h"   // activeLangCode ("en" / "pt")

// true when the UI language is English; every email label goes through rmbT().
static inline bool ramboxEmailEnglish() {
    return activeLangCode != nullptr && activeLangCode[0] == 'e';
}

// Pick the PT or EN variant of a label.
static inline const char* rmbT(const char* pt, const char* en) {
    return ramboxEmailEnglish() ? en : pt;
}

static inline String ramboxEmailSubject(const char* device, const char* race, uint16_t n) {
    String s = String(F("Rambox32 - "));
    s += String(device && device[0] ? device : "Rambox");
    if (race && race[0]) s += String(F(" - ")) + String(race);
    s += String(F(" - ")) + String((unsigned)n);
    s += String((n == 1) ? rmbT(" chegada", " arrival") : rmbT(" chegadas", " arrivals"));
    return s;
}

static inline void ramboxEmailHeader(String& body, const char* device,
                                     const char* club, const char* clubCode,
                                     const char* fancier, const char* race,
                                     const char* raceDate, const char* solta) {
    body += String(rmbT("Registos captados pelo m\u00f3dulo ",
                        "Records captured by the "));
    body += String(device && device[0] ? device : "?");
    body += String(F(":\n\n"));

    if (club && club[0]) {
        body += String(rmbT("Clube: ", "Club: ")) + String(club);
        if (clubCode && clubCode[0]) body += String(F(" (")) + String(clubCode) + String(F(")"));
        body += '\n';
    } else if (clubCode && clubCode[0]) {
        body += String(rmbT("Clube: ", "Club: ")) + String(clubCode) + String(F("\n"));
    }

    if (fancier && fancier[0]) {
        body += String(rmbT("Concorrente: ", "Fancier: ")) + String(fancier) + String(F("\n"));
    }

    if (race && race[0]) {
        body += String(rmbT("Prova: ", "Race: ")) + String(race);
        if (raceDate && raceDate[0]) body += String(F(" (")) + String(raceDate) + String(F(")"));
        body += '\n';
    } else if (raceDate && raceDate[0]) {
        body += String(rmbT("Prova (data): ", "Race (date): ")) + String(raceDate) + String(F("\n"));
    }

    if (solta && solta[0]) {
        body += String(rmbT("Solta: ", "Release: ")) + String(solta) + String(F("\n"));
    }

    body += String(F("\n"));
}

// Totals block (shared): "Totais: N pombo(s) (+ M treino)".
static inline void ramboxEmailTotals(String& body, uint16_t totalRace, uint16_t totalTraining) {
    body += String(rmbT("\nTotais: ", "\nTotals: "));
    body += String((unsigned)totalRace);
    body += String(rmbT(" pombo(s)", " pigeon(s)"));
    if (totalTraining > 0) {
        body += String(F(" + ")) + String((unsigned)totalTraining);
        body += String(rmbT(" treino", " training"));
    }
}

// Section titles (shared by both table builders).
static inline const char* ramboxEmailSecNew()   { return rmbT("Novos Registos - Pombos", "New Arrivals - Pigeons"); }
static inline const char* ramboxEmailSecTrain() { return rmbT("Treino", "Training"); }
static inline const char* ramboxEmailSecOld()   { return rmbT("Provas anteriores", "Previous races"); }
static inline const char* ramboxEmailSecHistory() { return rmbT("--- Hist\u00f3rico anterior ---", "--- Previous history ---"); }
