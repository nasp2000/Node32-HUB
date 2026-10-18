#include "feature_flags.h"

#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1

#include "email_message_builder.h"
#include "email_parser.h"

#include <WiFi.h>
#include <time.h>

#include "config_manager_runtime.h"
#include "eth_w5500.h"
#include "languages/lang_active.h"

extern const char* FIRMWARE_VERSION;

String buildEmailTitlePrefix() {
    return configManager.getSystemName();
}

String buildEmailInfoTable(const String &line1,
                           const String &ruleCol1,
                           const String &ruleCol2,
                           const String &ruleCol3,
                           const String &freqLabel,
                           const String &sourceLine,
                           const String &highlightVal,
                           const String &eventTimestamp) {
    String displayLine = applyHighlightToSourceLine(sourceLine, highlightVal);

    IPAddress ip = WiFi.localIP();
    if (ip == IPAddress(0, 0, 0, 0)) {
        ip = ethernetLocalIP();
    }

    String networkLabel = L(activeLang->emailConnectedNA);
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID().length() > 0) {
        networkLabel = L(activeLang->emailConnectedWifi) + ": " + WiFi.SSID();
    } else if (ip != IPAddress(0, 0, 0, 0)) {
        networkLabel = L(activeLang->emailConnectedEth);
    }

    String resolvedEventTimestamp = eventTimestamp.length() > 0
        ? eventTimestamp
        : extractEventTimestampFromSourceLine(sourceLine);

    String out;
    out.reserve(320 + (int)sourceLine.length());
    out += "\n\n";
    out += L(activeLang->emailSentFrom) + ": " + configManager.getSystemName() + "\n";

    bool hasRule = (ruleCol1.length() > 0);
    if (hasRule) {
        out += "\n";
        out += L(activeLang->emailRuleChosen) + ": " + ruleCol1 + "\n";
        out += L(activeLang->emailOperator) + ": " + ruleCol2 + "\n";
        out += L(activeLang->emailValueFound) + ": " + ruleCol3 + "\n";
        if (resolvedEventTimestamp.length() > 0) {
            String dateToShow;
            String timeToShow;
            if (resolvedEventTimestamp.length() >= 19) {
                // "YYYY-MM-DD HH:MM:SS" — split date and time
                dateToShow = resolvedEventTimestamp.substring(0, 10);
                timeToShow = resolvedEventTimestamp.substring(11, 19);
            } else if (resolvedEventTimestamp.length() == 8) {
                // "HH:MM:SS" only — get current calendar date
                time_t nowTs = time(nullptr);
                struct tm ti;
                localtime_r(&nowTs, &ti);
                char dateBuf[12];
                snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%04d",
                         ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
                dateToShow = String(dateBuf);
                timeToShow = resolvedEventTimestamp;
            }
            if (dateToShow.length() > 0) {
                out += L(activeLang->emailDateLabel) + ": " + dateToShow + "\n";
            }
            out += L(activeLang->emailDate) + ": " + (timeToShow.length() > 0 ? timeToShow : resolvedEventTimestamp) + "\n";
        }
        if (sourceLine.length() > 0) {
            out += "\n";
            out += L(activeLang->emailLine) + ": " + displayLine + "\n";
        }
    } else if (line1.length() > 0) {
        out += "\n";
        out += line1 + "\n";
    }

    if (freqLabel.length() > 0) {
        out += "\n";
        out += L(activeLang->emailFreqNote) + ": " + freqLabel + "\n";
    }

    out += "\n------------------------------\n";
    out += String(F("Node32-HUB\n"));
    out += String(F("Modelo: "))   + String(ESP.getChipModel())  + "\n";
    out += L(activeLang->emailFirmware) + " " + String(FIRMWARE_VERSION) + "\n";
    out += networkLabel + "\n";
    out += L(activeLang->emailLocalIP) + ": " + ip.toString() + "\n";
    out += "------------------------------\n";

    return out;
}

String buildEmailSignature() {
    return buildEmailInfoTable("", "", "", "", "", "", "", "");
}
#endif // ENABLE_SMTP || ENABLE_GOOGLE_EMAIL