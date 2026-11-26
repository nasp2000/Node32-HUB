#pragma once
// screen_cards_pack.h — who builds each line-3 card.
//
// ScreenManager (screen.cpp) owns line 1 (network) and line 2 (uptime/heap/
// storage) and only rotates the card list. Every module card is composed here,
// so the core status path has no per-pack #if and adding a module means adding
// one builder below (plus its card in the module itself when it has state).
//
// Order matters: the list is pushed most-important first and the core gives
// card 0 two turns per rotation cycle.
//
// Header-only, like the rest of src/screen/ (no new build units, no
// build_src_filter changes).

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1

#include <Arduino.h>
#include <string.h>
#include <time.h>

#include "screen/screen_cards.h"
#include "languages/lang_active.h"
#include "languages/lang_screen.h"
#include "config_manager_runtime.h"

#if ENABLE_UNIKON==1
#include "unikon_protocol.h"
#endif
#if ENABLE_BRICON==1
#include "bricon_protocol.h"
#endif
#if ENABLE_TDK_LAMBDA==1
#include "tdk_lambda.h"      // TdkStatus / TdkValues / tdkLambdaGet*
#endif
#if ENABLE_AHT20_BMP280==1
#include "sensors/sensor_config.h"
#endif
#if ENABLE_MQ135==1
#include "sensors/sensor_mq135.h"
#endif
#if ENABLE_PROXIMITY==1
#include "sensors/proximity_manager.h"
#endif
#if ENABLE_MODBUS==1
#include "modbus_runtime.h"
#endif
#if ENABLE_CAN==1
#include "can_runtime.h"
#endif
#if ENABLE_USB_HOST==1
#include "usb_host_manager.h"
#endif
#if ENABLE_UDA1334A==1 && defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)
#include "radio_audio_runtime.h"
#endif

// RAMBOX/Unikon builds keep line 3 on the clock module only: the RS232 port
// belongs to Unikon or Bricon, so the generic module cards would be noise.
inline bool screenLine3UnikonOnly() {
#if (defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX)) || (ENABLE_UNIKON==1)
    return true;
#else
    return false;
#endif
}

// True when the Bricon clock owns the RS232 port (only one module can at a time).
inline bool screenLine3BriconActive() {
#if ENABLE_BRICON==1
    return (strcmp(briconActiveModuleName(), "bricon") == 0);
#else
    return false;
#endif
}

// ---------------------------------------------------------------- TDK-Lambda
#if ENABLE_TDK_LAMBDA==1
// PSU card: one glance shows the state. Full form for the wide panel and a
// <=10-character form for the 0.96" row; the core picks between them from the
// active display profile. Older than 3 poll intervals = stale ('~').
inline void screenAddTdkCard(ScreenCardList& cards) {
    TdkStatus st = tdkLambdaGetStatus();
    TdkValues v  = tdkLambdaGetValues();

    char full[SCREEN_CARD_TEXT_MAX];
    char compact[SCREEN_CARD_COMPACT_MAX];

    if (st.listen) {
        snprintf(full, sizeof(full), "TDK listen");
        snprintf(compact, sizeof(compact), "listen");
    } else if (st.alertActive) {
        snprintf(full, sizeof(full), "TDK ALERT");
        snprintf(compact, sizeof(compact), "ALERT");
    } else if (st.lastError[0] && st.lastErrorAgeMs < 120000UL) {
        snprintf(full, sizeof(full), "TDK err %s", st.lastError);
        snprintf(compact, sizeof(compact), "%s", st.lastError);
    } else if (!v.valid) {
        snprintf(full, sizeof(full), "TDK waiting");
        snprintf(compact, sizeof(compact), "wait");
    } else {
        snprintf(full, sizeof(full), "TDK %.0fV %.2fA %s", v.vMeas, v.iMeas, v.mode);
        snprintf(compact, sizeof(compact), "%.0fV %.2fA", v.vMeas, v.iMeas);   // <=10 chars
    }

    const bool stale = (st.polling && st.pollIntervalMs > 0 &&
                        st.lastRxAgeMs > (st.pollIntervalMs * 3UL));
    cards.addCompact(full, compact, 1, stale);
}
#endif

// ------------------------------------------------------------------ builders
inline void screenBuildPackCards(ScreenCardList& cards) {
    const bool unikonOnly   = screenLine3UnikonOnly();
    const bool briconActive = screenLine3BriconActive();
#if ENABLE_UNIKON==0 && ENABLE_BRICON==0
    (void)briconActive;   // no clock module line in this build
#endif

#if ENABLE_UNIKON==1
    if (!briconActive) {
        UnikonStatus us = unikonGetStatus();
        char buf[SCREEN_CARD_TEXT_MAX];
        if (us.connected) {
            snprintf(buf, sizeof(buf), "%s%u total %u reg %u",
                     FPSTR(activeLang->screenUnikonPlus),
                     (unsigned)us.lastNewCount, (unsigned)us.totalKnown,
                     (unsigned)us.registeredCount);
        } else {
            snprintf(buf, sizeof(buf), "%s%u",
                     FPSTR(activeLang->screenUnikonOffline), (unsigned)us.totalKnown);
        }
        cards.add(buf);
    }
#endif

#if ENABLE_BRICON==1
    if (briconActive) {
        BriconStatus bs = briconGetStatus();
        const uint16_t briconTotal = briconGetArrivalCount();
        char buf[SCREEN_CARD_TEXT_MAX];
        if (bs.connected) {
            snprintf(buf, sizeof(buf), "%s%u total %u reg %u",
                     FPSTR(activeLang->screenBriconPlus),
                     (unsigned)bs.lastNewCount, (unsigned)briconTotal,
                     (unsigned)briconGetRegistrationCount());
        } else {
            snprintf(buf, sizeof(buf), "%s%u",
                     FPSTR(activeLang->screenBriconOffline), (unsigned)briconTotal);
        }
        cards.add(buf);
    }
#endif

    if (!unikonOnly) {
#if ENABLE_TDK_LAMBDA==1
        screenAddTdkCard(cards);   // first: this is the pack's main instrument
#endif

#if ENABLE_AHT20_BMP280==1
        {
            const SensorConfig &cfg = sensorConfigManager.getConfig();
            if (cfg.last_reading_time > 0) {
                const ScreenCardLabels& lbl = screenCardLabels();
                char envBuf[SCREEN_CARD_TEXT_MAX];
                snprintf(envBuf, sizeof(envBuf), "%s %.1fC %s %.0f%% %s %.0fhPa",
                         lbl.envTemp, cfg.last_temp_avg, lbl.envHum,
                         cfg.last_humidity, lbl.envPress, cfg.last_pressure_hpa);
                // Stale marker: a sensor that stopped updating must not show its
                // last value as if it were live. '~' = older than 5 minutes.
                bool envStale = false;
                const time_t nowT = time(nullptr);
                if (nowT > 100000 && (nowT - (time_t)cfg.last_reading_time) > 300) {
                    envStale = true;
                }
                cards.add(envBuf, 1, envStale);
            } else {
                cards.add(FPSTR(activeLang->screenTempNoReading));
            }
        }
#endif

#if ENABLE_MQ135==1
        {
            char gasBuf[SCREEN_CARD_TEXT_MAX];
            if (mq135Sensor.isInitialized()) {
                MQ135Reading gas = mq135Sensor.getLastReading();
                if (gas.valid) {
                    snprintf(gasBuf, sizeof(gasBuf), "Gas: CO2 %.0fppm AQI %.0f",
                             gas.ppm_co2_corrected, gas.aqi);
                    cards.add(gasBuf);
                } else {
                    const unsigned long warmupSec = mq135Sensor.getWarmupRemaining() / 1000UL;
                    snprintf(gasBuf, sizeof(gasBuf), "%s%lus",
                             FPSTR(activeLang->screenGasWarmup), warmupSec);
                    cards.add(gasBuf);
                }
            } else {
                cards.add(FPSTR(activeLang->screenGasNoInit));
            }
        }
#endif

#if ENABLE_PROXIMITY==1
        {
            const uint8_t nCh = proximityChannelCount();
            if (nCh > 0) {
                char proxBuf[SCREEN_CARD_TEXT_MAX];
                if (nCh == 1) {
                    ProximityChannel ch0 = proximityGetChannel(0);
                    snprintf(proxBuf, sizeof(proxBuf), "Prox %s: IN:%lu OUT:%lu %.1frpm",
                             ch0.name, (unsigned long)ch0.count_in,
                             (unsigned long)ch0.count_out, ch0.rate_ppm);
                } else {
                    uint32_t totalIn = 0, totalOut = 0;
                    for (uint8_t c = 0; c < nCh; c++) {
                        ProximityChannel ch = proximityGetChannel(c);
                        totalIn  += ch.count_in;
                        totalOut += ch.count_out;
                    }
                    snprintf(proxBuf, sizeof(proxBuf), "Prox: %u ch IN:%lu OUT:%lu",
                             (unsigned)nCh, (unsigned long)totalIn, (unsigned long)totalOut);
                }
                cards.add(proxBuf);
            }
        }
#endif

#if ENABLE_MODBUS==1
        {
            if (g_modbusDeviceCount > 0) {
                uint32_t totalPolls = 0, totalErrors = 0;
                for (uint8_t d = 0; d < g_modbusDeviceCount; d++) {
                    totalPolls  += g_modbusDevices[d].pollCount;
                    totalErrors += g_modbusDevices[d].errorCount;
                }
                char mbBuf[SCREEN_CARD_TEXT_MAX];
                snprintf(mbBuf, sizeof(mbBuf), "Modbus: %u dev poll:%lu err:%lu",
                         (unsigned)g_modbusDeviceCount,
                         (unsigned long)totalPolls, (unsigned long)totalErrors);
                cards.add(mbBuf);
            }
        }
#endif

#if ENABLE_CAN==1
        {
            if (canRuntimeIsRunning()) {
                CanRuntimeStats cs = canRuntimeGetStats();
                char canBuf[SCREEN_CARD_TEXT_MAX];
                snprintf(canBuf, sizeof(canBuf), "CAN: RX:%lu TX:%lu err:%lu",
                         (unsigned long)cs.total_rx, (unsigned long)cs.total_tx,
                         (unsigned long)cs.total_tx_errors);
                cards.add(canBuf);
            }
        }
#endif

#if ENABLE_USB_HOST==1
        {
            if (usbHostIsRunning()) {
                UsbHostStats us = usbHostGetStats();
                char usbBuf[SCREEN_CARD_TEXT_MAX];
                snprintf(usbBuf, sizeof(usbBuf), "USB:%u dev RX:%lu TX:%lu",
                         (unsigned)us.devices_connected,
                         (unsigned long)us.total_rx_bytes,
                         (unsigned long)us.total_tx_bytes);
                cards.add(usbBuf);
            }
        }
#endif

#if ENABLE_UDA1334A==1 && defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)
        {
            char radioBuf[SCREEN_CARD_TEXT_MAX];
            if (radioAudioIsActive()) {
                const char* title  = radioAudioGetCurrentTitle();
                const char* artist = radioAudioGetCurrentArtist();
                if (title && title[0]) {
                    if (artist && artist[0] && strcmp(artist, title) != 0) {
                        snprintf(radioBuf, sizeof(radioBuf), "%s%s - %s",
                                 FPSTR(activeLang->screenRadioLabel), artist, title);
                    } else {
                        snprintf(radioBuf, sizeof(radioBuf), "%s%s",
                                 FPSTR(activeLang->screenRadioLabel), title);
                    }
                } else {
                    snprintf(radioBuf, sizeof(radioBuf), "%s",
                             FPSTR(activeLang->screenRadioPlaying));
                }
            } else {
                snprintf(radioBuf, sizeof(radioBuf), "%s",
                         FPSTR(activeLang->screenRadioStopped));
            }
            cards.add(radioBuf);
        }
#endif

        // Scheduler/email summary — always present as a fallback card.
        {
            int intervalActiveCount = 0;
            for (int i = 1; i <= 10; i++) {
                String cmd, unit, days, tpl;
                int interval = 0;
                bool en = false;
                if (configManager.getSchedCmdSlot(i, cmd, interval, unit, days, tpl, en)) {
                    if (en && cmd.length()) intervalActiveCount++;
                }
            }
            int datetimeActiveCount = 0;
            for (int i = 1; i <= 16; i++) {
                String c, d, t, tpl;
                bool en = false;
                if (configManager.getSchedDtSlot(i, c, d, t, tpl, en)) {
                    if (en && c.length()) datetimeActiveCount++;
                }
            }
            const size_t recipientsCount = configManager.countRecipientSlotsNonEmpty();
            const size_t templatesCount  = configManager.getRs232Templates().size();
            const int totalSched = intervalActiveCount + datetimeActiveCount;
            const ScreenCardLabels& lbl = screenCardLabels();
            char schedBuf[SCREEN_CARD_TEXT_MAX];
            snprintf(schedBuf, sizeof(schedBuf), "%s%d%s%d%s%d",
                     lbl.sched, totalSched,
                     lbl.schedDest, (int)recipientsCount,
                     lbl.schedTpl, (int)templatesCount);
            cards.add(schedBuf);
        }
    }
}

#endif // ENABLE_LCD_SCREEN
