#include "alerts_page_renderer.h"

#include "../../ConfigManager.h"
#include "../../feature_flags.h"
#include "page_render_common.h"
#include "../assets/page_alerts_asset.h"

extern ConfigManager configManager;

namespace {

size_t alertsCheckedAttr(Print& out, bool enabled) {
    if (!enabled) return 0;
    const char checked[] = "checked ";
    out.print(checked);
    return sizeof(checked) - 1;
}

size_t alertsEscapedHtml(Print& out, const String& input) {
    size_t n = 0;
    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        switch (c) {
            case '&':  out.print(F("&amp;"));  n += 5; break;
            case '<':  out.print(F("&lt;"));   n += 4; break;
            case '>':  out.print(F("&gt;"));   n += 4; break;
            case '"':  out.print(F("&quot;")); n += 6; break;
            case '\'': out.print(F("&#39;"));  n += 5; break;
            default:
                if ((uint8_t)c < 0x20 && c != '\t') {
                    out.print(' ');
                    n += 1;
                } else {
                    out.print(c);
                    n += 1;
                }
                break;
        }
    }
    return n;
}

static const char* const kTimeoutVals[]   = {"5","10","20","30","45","60","120","180","360","720","1440"};
static const char* const kTimeoutLabels[] = {"5 min","10 min","20 min","30 min","45 min","1h","2h","3h","6h","12h","24h"};
static const int kMaxPerDayVals[] = {3,6,9,12,15,20,25,30,45,60};

size_t alertsSlotRow(Print& out, int idx, bool enabled, int timeout, int maxPerDay, const String& emails) {
    size_t n = 0;
    char idBuf[8];
    itoa(idx, idBuf, 10);

    // <tr>
    out.print(F("<tr style='border-bottom:1px solid #1e3a8a'>"));
    n += 30;

    // Col 1: Enable checkbox + Timeout select
    out.print(F("<td style='padding:6px'><label style='display:flex;align-items:center;gap:6px'><input type='checkbox' id='alert_enabled_"));
    n += 55;
    out.print(idBuf);
    n += strlen(idBuf);
    out.print(F("' style='width:18px;height:18px;flex-shrink:0'"));
    n += 30;
    n += alertsCheckedAttr(out, enabled);
    out.print(F("><select id='alert_timeout_"));
    n += 24;
    out.print(idBuf);
    n += strlen(idBuf);
    out.print(F("' style='padding:6px;border-radius:6px;border:1px solid rgba(255,255,255,.18);background:#1e293b;color:#fff;font-size:12px;flex:1'>"));
    n += 110;
    for (int i = 0; i < 11; ++i) {
        out.print(F("<option value='"));
        n += 15;
        out.print(kTimeoutVals[i]);
        n += strlen(kTimeoutVals[i]);
        out.print('\'');
        n += 1;
        if (atoi(kTimeoutVals[i]) == timeout) { out.print(F(" selected")); n += 9; }
        out.print('>');
        n += 1;
        out.print(kTimeoutLabels[i]);
        n += strlen(kTimeoutLabels[i]);
        out.print(F("</option>"));
        n += 9;
    }
    out.print(F("</select></label></td>"));
    n += 17;

    // Col 2: Max per day select
    out.print(F("<td style='padding:6px'><select id='alert_max_"));
    n += 35;
    out.print(idBuf);
    n += strlen(idBuf);
    out.print(F("' style='padding:6px;border-radius:6px;border:1px solid rgba(255,255,255,.18);background:#1e293b;color:#fff;font-size:12px;width:100%'>"));
    n += 105;
    for (int i = 0; i < 10; ++i) {
        char numBuf[8];
        itoa(kMaxPerDayVals[i], numBuf, 10);
        out.print(F("<option value='"));
        n += 15;
        out.print(numBuf);
        n += strlen(numBuf);
        out.print('\'');
        n += 1;
        if (kMaxPerDayVals[i] == maxPerDay) { out.print(F(" selected")); n += 9; }
        out.print('>');
        n += 1;
        out.print(numBuf);
        n += strlen(numBuf);
        out.print(F(" alerts/day</option>"));
        n += 21;
    }
    out.print(F("</select></td>"));
    n += 13;

    // Col 3: Email input
    out.print(F("<td style='padding:6px'><input type='text' id='alert_emails_"));
    n += 44;
    out.print(idBuf);
    n += strlen(idBuf);
    out.print(F("' placeholder='email1@ex.com, email2@ex.com' style='width:100%;padding:6px;border-radius:6px;border:1px solid rgba(255,255,255,.18);background:#1e293b;color:#fff;font-size:12px' value='"));
    n += 120;
    n += alertsEscapedHtml(out, emails);
    out.print(F("'></td>"));
    n += 9;

    // </tr>
    out.print(F("</tr>"));
    n += 6;
    return n;
}

#if ENABLE_PROXIMITY==1
static const char* const kOeePersistVals[]   = {"5","10","15","20","30","45","60","120","240"};
static const char* const kOeePersistLabels[] = {"5 min","10 min","15 min","20 min","30 min","45 min","1h","2h","4h"};

size_t oeeAlertSlotRow(Print& out, int idx, bool enabled, int metric, int threshold, int persistMin, int maxPerDay, const String& emails) {
    size_t n = 0;
    char idBuf[8];
    itoa(idx, idBuf, 10);

    // <tr>
    out.print(F("<tr style='border-bottom:1px solid #4c1d95'>"));
    n += 30;

    // Col 1: Enable checkbox + Metric select
    out.print(F("<td style='padding:6px'><label style='display:flex;align-items:center;gap:6px'><input type='checkbox' id='oee_enabled_"));
    n += 55;
    out.print(idBuf);
    n += strlen(idBuf);
    out.print(F("' style='width:18px;height:18px;flex-shrink:0'"));
    n += 30;
    n += alertsCheckedAttr(out, enabled);
    out.print(F("><select id='oee_metric_"));
    n += 24;
    out.print(idBuf);
    n += strlen(idBuf);
    out.print(F("' style='padding:6px;border-radius:6px;border:1px solid rgba(255,255,255,.18);background:#1e293b;color:#fff;font-size:12px;flex:1'>"));
    n += 110;
    static const char* const kMetricVals[] = {"0", "1", "2"};
    static const char* const kMetricI18n[] = {"proxDashOeeShift", "proxDashOeeDay", "proxDashA"};
    static const char* const kMetricFb[] = {"Shift OEE", "Day OEE", "Availability"};
    for (int i = 0; i < 3; ++i) {
        out.print(F("<option value='"));
        n += 15;
        out.print(kMetricVals[i]);
        n += strlen(kMetricVals[i]);
        out.print(F("' data-i18n='"));
        n += 13;
        out.print(kMetricI18n[i]);
        n += strlen(kMetricI18n[i]);
        out.print('\'');
        n += 1;
        if (i == metric) { out.print(F(" selected")); n += 9; }
        out.print('>');
        n += 1;
        out.print(kMetricFb[i]);
        n += strlen(kMetricFb[i]);
        out.print(F("</option>"));
        n += 9;
    }
    out.print(F("</select></label></td>"));
    n += 17;

    // Col 2: Threshold % (number 1-100)
    out.print(F("<td style='padding:6px'><input type='number' id='oee_threshold_"));
    n += 45;
    out.print(idBuf);
    n += strlen(idBuf);
    out.print(F("' min='1' max='100' value='"));
    n += 24;
    char thBuf[8];
    itoa(threshold, thBuf, 10);
    out.print(thBuf);
    n += strlen(thBuf);
    out.print(F("' style='width:100%;padding:6px;border-radius:6px;border:1px solid rgba(255,255,255,.18);background:#1e293b;color:#fff;font-size:12px'></td>"));
    n += 120;

    // Col 3: Persist select
    out.print(F("<td style='padding:6px'><select id='oee_persist_"));
    n += 35;
    out.print(idBuf);
    n += strlen(idBuf);
    out.print(F("' style='padding:6px;border-radius:6px;border:1px solid rgba(255,255,255,.18);background:#1e293b;color:#fff;font-size:12px;width:100%'>"));
    n += 105;
    for (int i = 0; i < 9; ++i) {
        out.print(F("<option value='"));
        n += 15;
        out.print(kOeePersistVals[i]);
        n += strlen(kOeePersistVals[i]);
        out.print('\'');
        n += 1;
        if (atoi(kOeePersistVals[i]) == persistMin) { out.print(F(" selected")); n += 9; }
        out.print('>');
        n += 1;
        out.print(kOeePersistLabels[i]);
        n += strlen(kOeePersistLabels[i]);
        out.print(F("</option>"));
        n += 9;
    }
    out.print(F("</select></td>"));
    n += 13;

    // Col 4: Max per day select (same values as inactivity)
    out.print(F("<td style='padding:6px'><select id='oee_max_"));
    n += 35;
    out.print(idBuf);
    n += strlen(idBuf);
    out.print(F("' style='padding:6px;border-radius:6px;border:1px solid rgba(255,255,255,.18);background:#1e293b;color:#fff;font-size:12px;width:100%'>"));
    n += 105;
    for (int i = 0; i < 10; ++i) {
        char numBuf[8];
        itoa(kMaxPerDayVals[i], numBuf, 10);
        out.print(F("<option value='"));
        n += 15;
        out.print(numBuf);
        n += strlen(numBuf);
        out.print('\'');
        n += 1;
        if (kMaxPerDayVals[i] == maxPerDay) { out.print(F(" selected")); n += 9; }
        out.print('>');
        n += 1;
        out.print(numBuf);
        n += strlen(numBuf);
        out.print(F(" alerts/day</option>"));
        n += 21;
    }
    out.print(F("</select></td>"));
    n += 13;

    // Col 5: Email input
    out.print(F("<td style='padding:6px'><input type='text' id='oee_emails_"));
    n += 44;
    out.print(idBuf);
    n += strlen(idBuf);
    out.print(F("' placeholder='email1@ex.com, email2@ex.com' style='width:100%;padding:6px;border-radius:6px;border:1px solid rgba(255,255,255,.18);background:#1e293b;color:#fff;font-size:12px' value='"));
    n += 120;
    n += alertsEscapedHtml(out, emails);
    out.print(F("'></td>"));
    n += 9;

    // </tr>
    out.print(F("</tr>"));
    n += 6;
    return n;
}
#endif // ENABLE_PROXIMITY==1

} // namespace

size_t streamAlertsPageToPrint(Print& out) {
    size_t n = 0;

    out.print(FPSTR(PAGE_ALERTS_HEAD));
    n += strlen_P(PAGE_ALERTS_HEAD);

#if ENABLE_PROXIMITY==1
    // Tab bar (Inactivity + OEE). Other packs get no tab bar at all.
    out.print(FPSTR(PAGE_ALERTS_TABS));
    n += strlen_P(PAGE_ALERTS_TABS);
    out.print(FPSTR(PAGE_ALERTS_TABS_OEE));
    n += strlen_P(PAGE_ALERTS_TABS_OEE);
#endif

    out.print(FPSTR(PAGE_ALERTS_INACT_OPEN));
    n += strlen_P(PAGE_ALERTS_INACT_OPEN);

    out.print(FPSTR(PAGE_ALERTS_AFTER_SENSORS_RESET));
    n += strlen_P(PAGE_ALERTS_AFTER_SENSORS_RESET);

    // Emit 6 slot rows with dynamic values.
    for (int i = 0; i < ConfigManager::ALERT_SLOT_COUNT; ++i) {
        n += alertsSlotRow(out, i,
            configManager.getAlertSlotEnabled(i),
            configManager.getAlertSlotTimeout(i),
            configManager.getAlertSlotMaxPerDay(i),
            configManager.getAlertSlotEmails(i));
    }

    out.print(FPSTR(PAGE_ALERTS_INACT_CLOSE));
    n += strlen_P(PAGE_ALERTS_INACT_CLOSE);

#if ENABLE_PROXIMITY==1
    // OEE tab (alertas OEE) — proximity/OEE builds only.
    out.print(FPSTR(PAGE_ALERTS_OEE_TAB));
    n += strlen_P(PAGE_ALERTS_OEE_TAB);
    for (int i = 0; i < ConfigManager::OEE_ALERT_SLOT_COUNT; ++i) {
        n += oeeAlertSlotRow(out, i,
            configManager.getOeeAlertEnabled(i),
            configManager.getOeeAlertMetric(i),
            configManager.getOeeAlertThreshold(i),
            configManager.getOeeAlertPersistMin(i),
            configManager.getOeeAlertMaxPerDay(i),
            configManager.getOeeAlertEmails(i));
    }
    out.print(FPSTR(PAGE_ALERTS_OEE_CLOSE));
    n += strlen_P(PAGE_ALERTS_OEE_CLOSE);
#else
    out.print(F("</div>")); // close #tabInact (no OEE tab on this build)
    n += 6;
#endif

    out.print(FPSTR(PAGE_ALERTS_FOOTER));
    n += strlen_P(PAGE_ALERTS_FOOTER);

    out.print(F("<script src='/auto.js'></script>"));
    n += 32;

#if ENABLE_PROXIMITY==1
    // OEE tab switching + OEE save (proximity/OEE builds only).
    out.print(FPSTR(PAGE_ALERTS_OEE_JS));
    n += strlen_P(PAGE_ALERTS_OEE_JS);
#endif

    // Inactivity save handler (fragment lives in the asset so the fixture includes it).
    out.print(FPSTR(PAGE_ALERTS_INACT_JS));
    n += strlen_P(PAGE_ALERTS_INACT_JS);

    return n;
}
