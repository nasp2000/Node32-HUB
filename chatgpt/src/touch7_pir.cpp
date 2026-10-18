#include "touch7_pir.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include "touch7_config.h"
#include "touch7_display.h"
#include "touch7_peers.h"
#include "touch7_audio.h"

static unsigned long s_lastMotionMs = 0;
static bool s_pinHigh = false;
static unsigned long s_cooldownUntil = 0;
static bool s_dimApplied = false;
static bool s_saverShown = false;

void touch7PirBegin() {
    s_lastMotionMs = millis();
#if defined(TOUCH7_PIR_PIN) && TOUCH7_PIR_PIN >= 0
    pinMode(TOUCH7_PIR_PIN, INPUT_PULLDOWN);
#endif
}

void touch7PirTouchActivity() {
    s_lastMotionMs = millis();
    if (s_saverShown) {
        s_saverShown = false;
        touch7Display.showScreensaver(false);
    }
    if (s_dimApplied) {
        s_dimApplied = false;
        touch7Display.setDimmed(false);
    }
}

unsigned long touch7PirIdleSec() {
    return (millis() - s_lastMotionMs) / 1000UL;
}

void touch7PirTick() {
    if (!touch7ConfigManager.isInitialized()) return;
    const Touch7Config& cfg = touch7ConfigManager.getConfig();
    unsigned long now = millis();

#if defined(TOUCH7_PIR_PIN) && TOUCH7_PIR_PIN >= 0
    bool high = (digitalRead(TOUCH7_PIR_PIN) == HIGH);
    // Rising edge with 2 s retrigger guard (HC-SR501 holds HIGH ~2 s min).
    if (high && !s_pinHigh && now - s_lastMotionMs > 2000) {
        s_lastMotionMs = now;
        // F3b: snapshot the first live peer + toast + phrase (cooldown 60 s).
        if (now >= s_cooldownUntil) {
            s_cooldownUntil = now + 60000UL;
            touch7PeerRefreshNow(0);
            touch7Display.showToast("Movimento", "a verificar entrada...", "");
            if (!touch7AudioIsPlaying()) touch7AudioPlay("/falas/movimento.wav");
        }
        if (s_saverShown) {
            s_saverShown = false;
            touch7Display.showScreensaver(false);
        }
        if (s_dimApplied) {
            s_dimApplied = false;
            touch7Display.setDimmed(false);
        }
    }
    s_pinHigh = high;
#else
    (void)now;
#endif

    // Dim policy.
    if (cfg.pir_dim_sec > 0 && !s_dimApplied && touch7PirIdleSec() >= (unsigned)cfg.pir_dim_sec) {
        s_dimApplied = true;
        touch7Display.setDimmed(true);
    }
    // Screensaver policy.
    if (cfg.screensaver_sec > 0 && !s_saverShown && touch7PirIdleSec() >= (unsigned)cfg.screensaver_sec) {
        s_saverShown = true;
        touch7Display.showScreensaver(true);
    }
}

#endif // SELECTED_PACK == PACK_S3_TOUCH7
