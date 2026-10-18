#pragma once

// T24 — Arquivo .txt do corpo de TODOS os emails.
// Header puro (sem deps Arduino): a logica exata e compilada pelo teste
// nativo (tests/native/test_email_body_archive.cpp).
//
// Padrao segue o turno_data T8b:
//   /{sys}/<tipo>_<YYYY-MM-DD>_<HH-MM>.txt   (hora de envio)
//   SD quando (sdMode && mounted), senao o mesmo path no LittleFS.
//
// Tipos: EMAILDAY, LOGDAY, SENSORDAY, SHIFTDAY, SHIFTEND, STREAM,
//        INACTIVITY, ALARM, TEST, UNIKON, EMAIL (fallback).

#include <cctype>
#include <cstdio>
#include <string>

static inline std::string emailArchiveSanitizeToken(const std::string& in,
                                                    size_t maxLen) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size() && out.size() < maxLen; i++) {
        char c = in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            out += c;
        } else if (c == ' ' || c == '.') {
            out += '_';
        }
        // outros chars: descarta (acentos UTF-8 caem aqui, sem quebrar o path)
    }
    return out;
}

static inline std::string emailArchiveLower(const std::string& s) {
    std::string o = s;
    for (size_t i = 0; i < o.size(); i++) o[i] = (char)tolower((unsigned char)o[i]);
    return o;
}

// Infere o tipo a partir do subject (case-insensitive). Devolve fallback
// quando nada casa (default "EMAIL").
static inline std::string emailArchiveTipoFromSubjectOr(const std::string& subject,
                                                        const std::string& fallback) {
    std::string l = emailArchiveLower(subject);
    if (l.find("sensorday") != std::string::npos) return "SENSORDAY";
    if (l.find("logday") != std::string::npos) return "LOGDAY";
    if (l.find("shiftday") != std::string::npos) return "SHIFTDAY";
    if (l.find("shiftend") != std::string::npos) return "SHIFTEND";
    if (l.find("fimturno") != std::string::npos) return "SHIFTEND";
    if (l.find("fim_turno") != std::string::npos) return "SHIFTEND";
    if (l.find("fim-turno") != std::string::npos) return "SHIFTEND";
    if (l.find("inactiv") != std::string::npos) return "INACTIVITY";
    if (l.find("inatividade") != std::string::npos) return "INACTIVITY";
    if (l.find("unikon") != std::string::npos) return "UNIKON";
    if (l.find("sensorday") != std::string::npos) return "SENSORDAY";
    if (l.find("alarm") != std::string::npos) return "ALARM";
    if (l.find("alarme") != std::string::npos) return "ALARM";
    if (l.find("alert") != std::string::npos) return "ALARM";  // "Sensor Alert", "Alerta"
    if (l.find("motion") != std::string::npos) return "MOTION";
    if (l.find("movimento") != std::string::npos) return "MOTION";
    if (l.find("test") != std::string::npos) return "TEST";
    if (l.find("teste") != std::string::npos) return "TEST";
    if (l.find("stream") != std::string::npos) return "STREAM";
    if (l.find("emailday") != std::string::npos) return "EMAILDAY";
    if (l.find("log_day") != std::string::npos) return "LOGDAY";
    if (l.find("sensor_day") != std::string::npos) return "SENSORDAY";
    return fallback.empty() ? std::string("EMAIL") : fallback;
}

static inline std::string emailArchiveTipoFromSubject(const std::string& subject) {
    return emailArchiveTipoFromSubjectOr(subject, "EMAIL");
}

// Path final: /{sys}/<tipo>_<YYYY-MM-DD>_<HH-MM>.txt
// dateLabel "YYYY-MM-DD", timeLabel "HH-MM" (ja formatados pelo caller).
static inline std::string emailArchivePath(const std::string& sys,
                                           const std::string& tipo,
                                           const std::string& dateLabel,
                                           const std::string& timeLabel) {
    std::string s = emailArchiveSanitizeToken(sys, 32);
    if (s.empty()) s = "SD_LOG";
    std::string t = emailArchiveSanitizeToken(tipo, 16);
    if (t.empty()) t = "EMAIL";
    std::string d = emailArchiveSanitizeToken(dateLabel, 16);
    if (d.empty()) d = "nodate";
    std::string h = emailArchiveSanitizeToken(timeLabel, 16);
    if (h.empty()) h = "notime";
    // upper do tipo para nome estavel (EMAILDAY, SENSORDAY, ...)
    for (size_t i = 0; i < t.size(); i++) t[i] = (char)toupper((unsigned char)t[i]);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "/%s/%s_%s_%s.txt",
                  s.c_str(), t.c_str(), d.c_str(), h.c_str());
    return std::string(buf);
}
