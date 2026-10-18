#include "radio_audio_runtime.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include <cstring>
#include <math.h>
#include <ArduinoJson.h>
#include "sd_lib.h"
#include <LittleFS.h>
#include <vector>
#include "feature_flags.h"
#include "upnp_client.h"

#if ENABLE_WIFI==1
#include <WiFi.h>
#endif

#include "audio_dac_uda1334a.h"
#include "audio_dac_uda1334a_config.h"
#include "commands.h"
#include "littlefs_manager.h"
#include "psram_helpers.h"
#include "radio_schedule.h"
#include "sdcard.h" // sdcardShouldUseSD — same SD mount/fallback check as camera/environment packs
#include <driver/gpio.h> // gpio_reset_pin / gpio_set_direction / gpio_set_level (idle pin grounding)

#include <HTTPClient.h>
#include "audio_core.h"            // first-party AudioSource/AudioOutput/AudioDecoder
#include "audio_source_buffer.h"   // first-party linear cache buffer
#include "audio_source_http_ring.h" // PSRAM ring-buffer HTTP source (async fill task)
#include "audio_source_sd_ring.h"   // PSRAM ring-buffer SD source (async fill task)
#include "audio_decoder_minimp3.h"  // minimp3 (CC0) MP3 decoder
#include "audio_decoder_flac.h"     // dr_flac (Unlicense) FLAC decoder
#include "audio_decoder_aac.h"      // esp_audio_codec AAC decoder (Espressif Modified MIT)
#include "audio_decoder_wav.h"      // first-party WAV (PCM) decoder
#include "audio_decoder_ogg.h"      // stb_vorbis (Unlicense) OGG Vorbis decoder (T16)
#include "audio_decoder_opus.h"     // libopus core via esp_audio_codec (T16)
#include "radio_favicon.h"          // first-party station favicon derivation

static const char* kRadioQueuePath = "/config/radio_queue.json";
static const char* kRadioFavoritesPath = "/config/radio_favorites.json";
static const char* kRadioStatePath = "/config/radio_state.json";
static const uint8_t kStreamReconnectFallbackAttempt = 4;
static const char* kDefaultRadioQueue[] = {
    "https://streams.80s80s.de/80s80sDAB/mp3-128/homepage/",
    "https://streams.80s80s.de/80s80sDAB/mp3-192/homepage/",
    "https://streams.80s80s.de/80s80sMV/mp3-128/homepage/",
    "https://streams.80s80s.de/80s80sMV/mp3-192/homepage/",
    "https://streams.80s80s.de/alternative/mp3-128/homepage/",
    "https://streams.80s80s.de/alternative/mp3-192/homepage/",
    "https://streams.80s80s.de/bawue/mp3-128/homepage/",
    "https://streams.80s80s.de/bawue/mp3-192/homepage/",
    "https://streams.80s80s.de/bayern/mp3-128/homepage/",
    "https://streams.80s80s.de/bayern/mp3-192/homepage/",
    "https://streams.80s80s.de/breakdance/mp3-128/homepage/",
    "https://streams.80s80s.de/breakdance/mp3-192/homepage/",
    "https://streams.80s80s.de/christmas/mp3-128/homepage/",
    "https://streams.80s80s.de/christmas/mp3-192/homepage/",
    "https://streams.80s80s.de/collins/mp3-128/homepage/",
    "https://streams.80s80s.de/collins/mp3-192/homepage/",
    "https://streams.80s80s.de/dance/mp3-128/homepage/",
    "https://streams.80s80s.de/dance/mp3-192/homepage/",
    "https://streams.80s80s.de/darkwave/mp3-128/homepage/",
    "https://streams.80s80s.de/darkwave/mp3-192/homepage/",
    "https://streams.80s80s.de/davidbowie/mp3-128/homepage/",
    "https://streams.80s80s.de/davidbowie/mp3-192/homepage/",
    "https://streams.80s80s.de/deutsch/mp3-128/homepage/",
    "https://streams.80s80s.de/deutsch/mp3-192/homepage/",
    "https://streams.80s80s.de/dinnerparty/mp3-128/homepage/",
    "https://streams.80s80s.de/dinnerparty/mp3-192/homepage/",
    "https://streams.80s80s.de/dm/mp3-128/homepage/",
    "https://streams.80s80s.de/dm/mp3-192/homepage/",
    "https://streams.80s80s.de/ebm/mp3-128/homepage/",
    "https://streams.80s80s.de/ebm/mp3-192/homepage/",
    "https://streams.80s80s.de/freestyle/mp3-128/homepage/",
    "https://streams.80s80s.de/freestyle/mp3-192/homepage/",
    "https://streams.80s80s.de/hessen/mp3-128/homepage/",
    "https://streams.80s80s.de/hessen/mp3-192/homepage/",
    "https://streams.80s80s.de/hiphop/mp3-128/homepage/",
    "https://streams.80s80s.de/hiphop/mp3-192/homepage/",
    "https://streams.80s80s.de/italodiscomix/mp3-128/homepage/",
    "https://streams.80s80s.de/italodiscomix/mp3-192/homepage/",
    "https://streams.80s80s.de/italohits/mp3-128/homepage/",
    "https://streams.80s80s.de/italohits/mp3-192/homepage/",
    "https://streams.80s80s.de/livemusic/mp3-128/homepage/",
    "https://streams.80s80s.de/livemusic/mp3-192/homepage/",
    "https://streams.80s80s.de/love/mp3-128/homepage/",
    "https://streams.80s80s.de/love/mp3-192/homepage/",
    "https://streams.80s80s.de/maxis/mp3-128/homepage/",
    "https://streams.80s80s.de/maxis/mp3-192/homepage/",
    "https://streams.80s80s.de/mix/mp3-128/homepage/",
    "https://streams.80s80s.de/mix/mp3-192/homepage/",
    "https://streams.80s80s.de/mj/mp3-128/homepage/",
    "https://streams.80s80s.de/mj/mp3-192/homepage/",
    "https://streams.80s80s.de/nds/mp3-128/homepage/",
    "https://streams.80s80s.de/nds/mp3-192/homepage/",
    "https://streams.80s80s.de/ndw/mp3-128/homepage/",
    "https://streams.80s80s.de/ndw/mp3-192/homepage/",
    "https://streams.80s80s.de/neo/mp3-128/homepage/",
    "https://streams.80s80s.de/neo/mp3-192/homepage/",
    "https://streams.80s80s.de/nrw/mp3-128/homepage/",
    "https://streams.80s80s.de/nrw/mp3-192/homepage/",
    "https://streams.80s80s.de/party/mp3-128/homepage/",
    "https://streams.80s80s.de/party/mp3-192/homepage/",
    "https://streams.80s80s.de/personald/mp3-128/homepage/",
    "https://streams.80s80s.de/personald/mp3-192/homepage/",
    "https://streams.80s80s.de/popstories/mp3-128/homepage/",
    "https://streams.80s80s.de/popstories/mp3-192/homepage/",
    "https://streams.80s80s.de/queen/mp3-128/homepage/",
    "https://streams.80s80s.de/queen/mp3-192/homepage/",
    "https://streams.80s80s.de/reggae/mp3-128/homepage/",
    "https://streams.80s80s.de/reggae/mp3-192/homepage/",
    "https://streams.80s80s.de/rock/mp3-128/homepage/",
    "https://streams.80s80s.de/rock/mp3-192/homepage/",
    "https://streams.80s80s.de/romanticrock/mp3-128/homepage/",
    "https://streams.80s80s.de/romanticrock/mp3-192/homepage/",
    "https://streams.80s80s.de/soul/mp3-128/homepage/",
    "https://streams.80s80s.de/soul/mp3-192/homepage/",
    "https://streams.80s80s.de/soulballads/mp3-128/homepage/",
    "https://streams.80s80s.de/soulballads/mp3-192/homepage/",
    "https://streams.80s80s.de/summerhits/mp3-128/homepage/",
    "https://streams.80s80s.de/summerhits/mp3-192/homepage/",
    "https://streams.80s80s.de/techno/mp3-128/homepage/",
    "https://streams.80s80s.de/techno/mp3-192/homepage/",
    "https://streams.80s80s.de/wave/mp3-128/homepage/",
    "https://streams.80s80s.de/wave/mp3-192/homepage/",
    "https://streams.80s80s.de/web/mp3-128/homepage/",
    "https://streams.80s80s.de/web/mp3-192/homepage/",
    "https://streams.80s80s.de/wgt/mp3-128/homepage/",
    "https://streams.80s80s.de/wgt/mp3-192/homepage/",
    "https://streams.90s90s.de/90s00s/mp3-128/homepage/",
    "https://streams.90s90s.de/90s00s/mp3-192/homepage/",
    "https://streams.90s90s.de/90s90s-sommerhits/mp3-128/homepage/",
    "https://streams.90s90s.de/90s90s-sommerhits/mp3-192/homepage/",
    "https://streams.90s90s.de/bawue/mp3-128/homepage/",
    "https://streams.90s90s.de/bawue/mp3-192/homepage/",
    "https://streams.90s90s.de/boygroup/mp3-128/homepage/",
    "https://streams.90s90s.de/boygroup/mp3-192/homepage/",
    "https://streams.90s90s.de/britpop/mp3-128/homepage/",
    "https://streams.90s90s.de/britpop/mp3-192/homepage/",
    "https://streams.90s90s.de/chillout/mp3-128/homepage/",
    "https://streams.90s90s.de/chillout/mp3-192/homepage/",
    "https://streams.90s90s.de/dab-national/mp3-128/homepage/",
    "https://streams.90s90s.de/dab-national/mp3-192/homepage/",
    "https://streams.90s90s.de/danceradio/mp3-128/homepage/",
    "https://streams.90s90s.de/danceradio/mp3-192/homepage/",
    "https://streams.90s90s.de/dinnerparty/mp3-128/homepage/",
    "https://streams.90s90s.de/dinnerparty/mp3-192/homepage/",
    "https://streams.90s90s.de/eurodance/mp3-128/homepage/",
    "https://streams.90s90s.de/eurodance/mp3-192/homepage/",
    "https://streams.90s90s.de/grunge/mp3-128/homepage/",
    "https://streams.90s90s.de/grunge/mp3-192/homepage/",
    "https://streams.90s90s.de/hessen/mp3-128/homepage/",
    "https://streams.90s90s.de/hessen/mp3-192/homepage/",
    "https://streams.90s90s.de/hiphop/mp3-128/homepage/",
    "https://streams.90s90s.de/hiphop/mp3-192/homepage/",
    "https://streams.90s90s.de/hiphop-german/mp3-128/homepage/",
    "https://streams.90s90s.de/hiphop-german/mp3-192/homepage/",
    "https://streams.90s90s.de/house/mp3-128/homepage/",
    "https://streams.90s90s.de/house/mp3-192/homepage/",
    "https://streams.90s90s.de/inthemix/mp3-128/homepage/",
    "https://streams.90s90s.de/inthemix/mp3-192/homepage/",
    "https://streams.90s90s.de/love/mp3-128/homepage/",
    "https://streams.90s90s.de/love/mp3-192/homepage/",
    "https://streams.90s90s.de/loveparade/mp3-128/homepage/",
    "https://streams.90s90s.de/loveparade/mp3-192/homepage/",
    "https://streams.90s90s.de/main/mp3-128/homepage/",
    "https://streams.90s90s.de/main/mp3-192/homepage/",
    "https://streams.90s90s.de/mayday/mp3-128/homepage/",
    "https://streams.90s90s.de/mayday/mp3-192/homepage/",
    "https://streams.90s90s.de/nrw/mp3-192/homepage/",
    "https://streams.90s90s.de/pop/mp3-128/homepage/",
    "https://streams.90s90s.de/pop/mp3-192/homepage/",
    "https://streams.90s90s.de/RAVE/mp3-128/homepage/",
    "https://streams.90s90s.de/RAVE/mp3-192/homepage/",
    "https://streams.90s90s.de/reggae/mp3-128/homepage/",
    "https://streams.90s90s.de/reggae/mp3-192/homepage/",
    "https://streams.90s90s.de/rnb/mp3-128/homepage/",
    "https://streams.90s90s.de/rnb/mp3-192/homepage/",
    "https://streams.90s90s.de/rnbballads/mp3-128/homepage/",
    "https://streams.90s90s.de/rnbballads/mp3-192/homepage/",
    "https://streams.90s90s.de/rock/mp3-128/homepage/",
    "https://streams.90s90s.de/rock/mp3-192/homepage/",
    "https://streams.90s90s.de/sachsenradio/mp3-128/homepage/",
    "https://streams.90s90s.de/sachsenradio/mp3-192/homepage/",
    "https://streams.90s90s.de/techno/mp3-128/homepage/",
    "https://streams.90s90s.de/techno/mp3-192/homepage/",
    "https://streams.90s90s.de/technoessentials/mp3-128/homepage/",
    "https://streams.90s90s.de/technoessentials/mp3-192/homepage/",
    "https://streams.90s90s.de/trance/mp3-128/homepage/",
    "https://streams.90s90s.de/trance/mp3-192/homepage/",
    "https://streams.90s90s.de/xmas/mp3-192/homepage/",
};

static const uint32_t kStreamCachePreferred[4] = {
    1024UL * 1024UL,    // 1 MB  (was 512 KB)
    2048UL * 1024UL,    // 2 MB
    2048UL * 1024UL,    // 2 MB
    2048UL * 1024UL,    // 2 MB
};
static const uint32_t kStreamCacheMin[4] = {
    256UL * 1024UL,
    512UL * 1024UL,
    512UL * 1024UL,
    512UL * 1024UL,
};
static const uint16_t kStreamPrebufferMs[4] = { 800, 1500, 2500, 3500 };
static const uint32_t kReconnectBackoffMs[6] = { 2000UL, 5000UL, 10000UL, 20000UL, 30000UL, 30000UL };

static const uint32_t kPrefer192MinHeapBytes = 180UL * 1024UL;
static const uint32_t kPrefer192MinPsramBytes = 2UL * 1024UL * 1024UL;

static const uint32_t kSdCacheMinBytes = 256UL * 1024UL;    // 256 KB min (was 128 KB)
static const uint16_t kSdPrebufferMs = 400;
static const uint32_t kSdRingBytes = 2UL * 1024UL * 1024UL; // 2 MB PSRAM ring for SD files (async fill task)

struct RadioFavorite {
    String name;
    String url;
    String favicon;
};

using RadioQueueVector = std::vector<String, PsramAllocatorStd<String>>;
using RadioFavoritesVector = std::vector<RadioFavorite, PsramAllocatorStd<RadioFavorite>>;

static AudioDecoder* s_decoder = nullptr;
static int8_t s_decoderOverride = -1; // -1 = auto-detect from display; else DEC_* hint from the original URL
static String s_playMimeHint; // optional audio/... MIME for the next play-url
                             // (from a DIDL protocolInfo — lets UPnP transcoded
                             // URLs with no .mp3/.aac/.flac extension pick the
                             // right decoder); one-shot, cleared on consume
static bool s_decoderStarted = false; // true after s_decoder->begin() succeeds
class EqAudioOutput;
static EqAudioOutput* s_output = nullptr;
static AudioSource* s_source = nullptr;
static AudioSource* s_buffered = nullptr;   // what the decoder reads (ring or AudioSourceBuffer)
static void* s_psramBuffer = nullptr;   // PSRAM buffer for stream cache (manual free)
static uint32_t s_cacheBytesInUse = 0;

static bool s_started = false;
static bool s_active = false;
static uint8_t s_volume = 30;
// Radio persistence runs only in the loop task. Async web handlers only flag a
// pending persist, so LittleFS writes never race ConfigManager saves (which
// used to intermittently fail: "Config partially saved (check LittleFS)").
// Dirty flags are per-file and debounced: dragging the volume slider must
// coalesce into ONE small state write instead of three LittleFS writes (state
// + queue/favorites, each up to 8KB) per slider tick — the I2S DMA buffers
// only ~23ms and a blocking write beyond that underruns the audio.
static const uint32_t kPersistDebounceMs = 300;
static volatile uint8_t s_persistDirty = 0;   // bit0=state, bit1=queue, bit2=favorites
static volatile uint32_t s_persistAtMs = 0;   // flush no earlier than this uptime

// ===== Library listing =====
// The library is a live directory walk of /library on every request — no PSRAM
// cache, no background scan, no index file. Files are never opened here (no tag
// reads); embedded metadata/art is read once per track on PLAYBACK.
static bool s_stream = false;
static String s_current;
static String s_lastError;

static bool s_pendingUnmute = false;
static uint32_t s_unmuteAtMs = 0;

static String s_lastStreamUrl;
static String s_lastSdPath;
static String s_stationFavicon;
static String s_stationName;
static bool s_manualStop = false;
static bool s_pendingReconnect = false;
static uint8_t s_reconnectAttempt = 0;
static uint32_t s_reconnectAtMs = 0;
static uint8_t s_streamProfile = 1;
static uint32_t s_streamStartMs = 0;
static uint32_t s_profileStepMs = 0; // separate anchor: 5-min steps between stream profile downgrades
// "Minutes playing" for a stream survives reconnects: s_streamPlayedMs holds the
// accumulated played time (added up in radioAudioCleanupSource right before the
// segment is torn down), while s_streamStartMs is the start of the current segment.
// s_streamSession is true while the current logical source is an internet stream
// (vs a local SD file, which reports a per-track elapsed instead). These counters
// are only read for the status/clock display and never feed the decode/I2S path,
// so sound quality is unaffected.
static uint32_t s_streamPlayedMs = 0;
static bool     s_streamSession = false;

static RadioQueueVector s_queue;
static int s_queueIndex = -1;
static RadioFavoritesVector s_favorites;

static bool s_resumePending = false;
static uint8_t s_resumeTries = 0;
static bool s_autoResume = true;    // master auto-resume switch (persisted)
static bool s_resumeFirst = false;  // resume first of playlist vs last source (persisted)
static uint32_t s_resumeAtMs = 0;
static String s_resumeMode;
static String s_resumeSource;
static bool s_resumeWasActive = false;

// ===== Deferred actions (cross-task safety) ================================
// Web handlers run in async_tcp (Core 0), radio state lives in loop (Core 1).
// To avoid use-after-free we post actions here; the loop picks them up.
enum RadioDeferredAction : uint8_t {
    DEFER_NONE = 0,
    DEFER_STOP,
    DEFER_PLAY_URL,
    DEFER_PLAY_SD,
    DEFER_NEXT,
    DEFER_RANDOM,
    DEFER_PLAY_FAV,
    DEFER_TOGGLE,
    DEFER_PLAY_ENTRY
};
static volatile RadioDeferredAction s_deferAction = DEFER_NONE;
static String s_deferParam;
static int s_deferIndex = -1;
static SemaphoreHandle_t s_deferMtx = nullptr;

static char s_metaTitle[96] = "Idle";
static char s_metaArtist[48] = "";
static char s_metaArtwork[16] = "";
static char s_metaCacheSource[192] = "";
// Set once real ICY metadata (StreamTitle) has arrived for the current stream.
// Guards radioAudioExtractMetadata() against overwriting the song/artist with
// URL-derived placeholders (station host + bitrate).
static bool s_metaIcyActive = false;
// Metadata strings are written by the radio loop (drained from the ring
// source's ICY parser) and read by the web/status handlers; a short mutex
// keeps readers from seeing a half-written copy.
static SemaphoreHandle_t s_metaMtx = nullptr;

// ===== Shuffle / Repeat =====
static bool s_shuffle = false;
static uint8_t s_repeatMode = 0; // 0=none, 1=one, 2=all

// ===== Sleep timer (backend-enforced, survives page refresh/reboot) =====
static time_t s_sleepEndEpoch = 0;      // 0 = off; epoch time when audio must stop
static uint32_t s_sleepDurationMin = 0; // configured minutes (0 = off)
static uint32_t s_sleepSetUptimeMs = 0; // millis() when set (pre-NTP-sync fallback)

// ===== Equalizer - 5-band peaking biquad chain =====
static const uint8_t kEqBands = 5;
static const float kEqFrequencies[5] = { 80.0f, 400.0f, 1000.0f, 4000.0f, 12000.0f };
static const float kEqQ = 1.41f; // standard octave peaking: clearly separated bands
static int8_t s_eqGains[5] = { 6, 3, -1, 3, 5 }; // -12..+12 dB (Rock default, matches the page's Rock preset)

struct Biquad {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
    void init() { b0=1;b1=0;b2=0;a1=0;a2=0;x1=0;x2=0;y1=0;y2=0; }
    void setPeaking(float fs, float f0, float Q, float gainDB) {
        float A = powf(10.0f, gainDB / 40.0f);
        float w0 = 2.0f * (float)M_PI * f0 / fs;
        float cosw0 = cosf(w0);
        float alpha = sinf(w0) / (2.0f * Q);
        float norm = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / norm;
        b1 = (-2.0f * cosw0) / norm;
        b2 = (1.0f - alpha * A) / norm;
        a1 = (-2.0f * cosw0) / norm;
        a2 = (1.0f - alpha / A) / norm;
    }
    float process(float x) {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x;
        y2=y1; y1=y;
        return y;
    }
};
struct EqState {
    Biquad bandsL[5], bandsR[5];
    bool dirty;
    float rateHz;
    EqState() : dirty(true), rateHz(44100.0f) {
        for (int i=0;i<5;i++) { bandsL[i].init(); bandsR[i].init(); }
    }
    void rebuild(float fs) {
        for (int i=0;i<5;i++) {
            float g = (float)s_eqGains[i];
            bandsL[i].setPeaking(fs, kEqFrequencies[i], kEqQ, g);
            bandsR[i].setPeaking(fs, kEqFrequencies[i], kEqQ, g);
        }
        dirty = false;
    }
    void process(int16_t samples[2]) {
        if (dirty) rebuild(rateHz);
        float l = (float)samples[0], r = (float)samples[1];
        for (int i=0;i<5;i++) { l = bandsL[i].process(l); r = bandsR[i].process(r); }
        // Soft-knee limiter instead of a hard clip. Peaking EQ boosts (the
        // Rock default +6 dB lows) push peaks past full scale on hot masters;
        // hard-clipping at ±32768 is audible as crackle/rasping at high volume.
        // A gentle compression curve keeps the tone but removes the distortion.
        l = softLimit(l);
        r = softLimit(r);
        samples[0] = (int16_t)constrain((int)(l+0.5f), -32768, 32767);
        samples[1] = (int16_t)constrain((int)(r+0.5f), -32768, 32767);
    }

private:
    // Soft-knee limiter: linear below 90% full scale, gentle 1/x compression
    // above it. At 1.0 the output is 1.0; the curve is monotonic and C1, so no
    // new harmonics are added in the linear region.
    static float softLimit(float x) {
        const float kKnee = 29491.0f;   // 0.9 * 32767
        const float kMax = 32767.0f;
        const float ax = (x < 0.0f) ? -x : x;
        if (ax <= kKnee) return x;
        float over = ax - kKnee;
        float pct = over / (kMax - kKnee);   // 0..1 above the knee
        // Compress the excess by 1/(1+pct) — reaches 1.0 asymptotically.
        float compressed = kKnee + over / (1.0f + pct);
        return (x < 0.0f) ? -compressed : compressed;
    }
};
static EqState s_eqState;

// Native audio output: extends the first-party AudioOutput interface
// (audio_core.h, header-only) and writes directly with the native I2S driver
// (driver/i2s.h). Applies the 5-band EQ (EqState) and the gain in
// ConsumeSample.
class EqAudioOutput : public AudioOutput {
public:
    EqAudioOutput() : AudioOutput(), i2sOn(false), _bclkPin(-1), _wclkPin(-1), _doutPin(-1) {
        bps = 16;
        channels = 2;
        hertz = 44100;
        SetGain(1.0f);
    }

    // Store the pins; if the I2S clock is already running, apply immediately.
    bool SetPinout(int bclkPin, int wclkPin, int doutPin) {
        _bclkPin = bclkPin;
        _wclkPin = wclkPin;
        _doutPin = doutPin;
        if (i2sOn) return applyPins();
        return true;
    }

    // The decoder reports the real sample rate here; rebuild the biquad chain
    // for that rate so the band centre frequencies stay correct (e.g. 48 kHz
    // FLAC vs 44.1 kHz MP3).
    bool SetRate(int hz) override {
        hertz = hz;
        if (hz > 0) {
            s_eqState.rateHz = (float)hz;
            s_eqState.dirty = true;
        }
        if (i2sOn) {
            esp_err_t err = i2s_set_clk(UDA1334A_I2S_PORT, (uint32_t)hz,
                                        I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
            if (err != ESP_OK) {
                char dbg[96];
                snprintf(dbg, sizeof(dbg), "[RADIO] i2s_set_clk(%d Hz) failed: %s",
                         hz, esp_err_to_name(err));
                sendVirtualMessage(String(dbg), WARN, false, true);
            }
        }
        return true;
    }

    bool ConsumeSample(int16_t samples[2]) override {
        if (!i2sOn) return false;
        // Apply the 5-band EQ in place, then mono->stereo / 8->16 as needed.
        s_eqState.process(samples);
        MakeSampleStereo16(samples);
        // Pack LEFT into the low 16 bits and RIGHT into the high 16 bits —
        // matches the I2S_CHANNEL_FMT_RIGHT_LEFT convention.
        uint32_t s32 = ((uint32_t)(Amplify(samples[RIGHTCHANNEL])) << 16)
                     | (uint32_t)(Amplify(samples[LEFTCHANNEL]) & 0xffff);
        size_t written = 0;
        // Non-blocking (timeout 0): returns 0 when the DMA buffer is full, so
        // the decoder backpressure can yield and retry.
        i2s_write(UDA1334A_I2S_PORT, &s32, sizeof(s32), &written, 0);
        return written > 0;
    }

    bool begin() override {
        s_eqState.dirty = true;
        // udaDac.begin() already installed the I2S driver (radioAudioEnsureOutput).
        // Just clear any stale DMA data, mark the output active and set the rate.
        i2s_zero_dma_buffer(UDA1334A_I2S_PORT);
        i2sOn = true;
        SetRate(hertz);
        return true;
    }

    bool stop() override {
        if (!i2sOn) return false;
        i2sOn = false;
        return true;
    }

    // Total silence when idle: stop the I2S clock AND ground the BCLK/LRCK/DOUT
    // pins. i2s_stop() alone leaves the pins floating (high-Z), and a passive
    // DAC like the UDA1334A picks that up as noise -> idle hum. Driving them to
    // a solid low silences the DAC output completely.
    void setIdle(bool idle) {
        if (idle) {
            // Soft-mute the DAC first (no click), then stop the clock and ground
            // the BCLK/LRCK/DOUT pins so a passive DAC sees solid lows, not
            // floating lines -> total silence when nothing is playing.
            udaDac.setMuted(true);
            i2s_stop(UDA1334A_I2S_PORT);
            const int pins[3] = { UDA1334A_I2S_BCLK_PIN, UDA1334A_I2S_LRCK_PIN, UDA1334A_I2S_DOUT_PIN };
            for (int i = 0; i < 3; i++) {
                if (pins[i] >= 0) {
                    gpio_reset_pin((gpio_num_t)pins[i]);
                    gpio_set_direction((gpio_num_t)pins[i], GPIO_MODE_OUTPUT);
                    gpio_set_level((gpio_num_t)pins[i], 0);
                }
            }
            i2sOn = false;
        } else {
            // Re-route the pins back to the I2S peripheral, start the clock,
            // then release the DAC mute.
            applyPins();
            i2s_start(UDA1334A_I2S_PORT);
            udaDac.setMuted(false);
            i2sOn = true;
        }
    }

private:
    bool applyPins() {
        if (_bclkPin < 0 || _wclkPin < 0 || _doutPin < 0) return false;
        i2s_pin_config_t pin_cfg = {};
        pin_cfg.bck_io_num = _bclkPin;
        pin_cfg.ws_io_num = _wclkPin;
        pin_cfg.data_out_num = _doutPin;
        pin_cfg.data_in_num = I2S_PIN_NO_CHANGE;
        pin_cfg.mck_io_num = UDA1334A_I2S_MCLK_PIN;
        return i2s_set_pin(UDA1334A_I2S_PORT, &pin_cfg) == ESP_OK;
    }

    bool i2sOn;
    int _bclkPin;
    int _wclkPin;
    int _doutPin;
};

// ===== Cover art PSRAM storage =====
static uint8_t* s_artworkData = nullptr;
static uint32_t s_artworkSize = 0;
static char s_artworkMime[32] = "";

// ===== Playlist directory =====
static const char* kPlaylistDir = "/playlists";
static const char* kLibraryDir = "/library";

static bool radioAudioSaveQueue();
static bool radioAudioSaveFavorites();
static bool radioAudioSaveState();
static void radioAudioLoadState();
static void radioAudioTouchFavoriteByUrl(const String& url, const String& preferredName);
static bool radioAudioPlayUrlInternal(const String& url, String& messageOut, bool fromReconnect);
static bool radioAudioPlaySdInternal(const String& path, String& messageOut, bool updateQueue);
static bool radioAudioPlayQueueEntry(const String& entry, String& messageOut, bool updateQueue);
static void radioAudioExtractMetadata();
static void radioAudioParseIcyMetadata(const String& meta, int metaLen);
static void radioAudioDrainIcyFromRing();
void radioAudioClearArtwork();
// Library (forward decls — implementation after listing helpers)
static bool     radioAudioEnsureLibraryDir();
static bool radioAudioEnsurePlaylistDir();
static bool radioAudioIsMusicFile(const String& name);
static bool radioAudioSdMounted();

// Search helper
static bool radioAudioSearchOpen(const char* query);

// Playlist helpers
static bool radioAudioEnsurePlaylistDir();
static bool radioAudioParseM3u(const String& path, std::vector<String, PsramAllocatorStd<String>>& entries);

static bool radioAudioIsStreamEntry(const String& entry) {
    return entry.startsWith("http://") || entry.startsWith("https://");
}

static bool radioAudioReplaceLegacy80s80sUrl(String& url) {
    if (url == F("https://streams.80s80s.de/100/mp3-128/homepage/")) {
        url = F("https://streams.80s80s.de/web/mp3-128/homepage/");
        return true;
    }
    if (url == F("https://streams.80s80s.de/100/mp3-192/homepage/")) {
        url = F("https://streams.80s80s.de/web/mp3-192/homepage/");
        return true;
    }
    return false;
}

static bool radioAudioHasHeadroomFor192() {
    uint32_t freeHeap = ESP.getFreeHeap();
    bool heapOk = freeHeap >= kPrefer192MinHeapBytes;

    uint32_t freePsram = ESP.getFreePsram();
    bool psramOk = (freePsram == 0) || (freePsram >= kPrefer192MinPsramBytes);

    return heapOk && psramOk;
}

static void safeCopy(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (src[i] && i < (dstSize - 1)) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void radioAudioReadId3Tags(const char* path, String& title, String& artist,
                                  String& album, bool& hasArt);

static void radioAudioExtractMetadata() {
    if (!s_active || !s_decoder) {
        safeCopy(s_metaTitle, sizeof(s_metaTitle), "Idle");
        safeCopy(s_metaArtist, sizeof(s_metaArtist), "");
        safeCopy(s_metaArtwork, sizeof(s_metaArtwork), "");
        s_metaCacheSource[0] = '\0';
        return;
    }

    const String& source = s_stream ? s_lastStreamUrl : s_lastSdPath;
    if (!source.length()) {
        safeCopy(s_metaTitle, sizeof(s_metaTitle), "Playing");
        safeCopy(s_metaArtist, sizeof(s_metaArtist), "");
        safeCopy(s_metaArtwork, sizeof(s_metaArtwork), "");
        return;
    }

    const char* src = source.c_str();
    if (strncmp(s_metaCacheSource, src, sizeof(s_metaCacheSource) - 1) == 0) {
        return;
    }

    safeCopy(s_metaCacheSource, sizeof(s_metaCacheSource), src);

    // If real ICY metadata has already arrived for this stream, keep the song/
    // artist from the stream and never clobber it with URL-derived placeholders
    // (station host + bitrate). Inline ICY parsing in the ring source can fill
    // these before the first status poll, so the guard has to run even on a
    // cache miss.
    if (s_stream && s_metaIcyActive) return;

    if (s_stream) {
        const char* dom = strstr(src, "streams.");
        if (dom) {
            dom += 8;
            const char* end = strchr(dom, '.');
            if (end && end > dom) {
                char temp[96];
                size_t len = (size_t)(end - dom);
                if (len >= sizeof(temp)) len = sizeof(temp) - 1;
                memcpy(temp, dom, len);
                temp[len] = '\0';
                if (temp[0] >= 'a' && temp[0] <= 'z') temp[0] = (char)(temp[0] - 32);
                safeCopy(s_metaTitle, sizeof(s_metaTitle), temp);
            } else {
                safeCopy(s_metaTitle, sizeof(s_metaTitle), "Stream");
            }
        } else {
            safeCopy(s_metaTitle, sizeof(s_metaTitle), "Stream");
        }

        if (strstr(src, "mp3-192")) {
            safeCopy(s_metaArtist, sizeof(s_metaArtist), "192 kbps");
        } else if (strstr(src, "mp3-128")) {
            safeCopy(s_metaArtist, sizeof(s_metaArtist), "128 kbps");
        } else {
            safeCopy(s_metaArtist, sizeof(s_metaArtist), "Stream");
        }

        // Streams never carry extractable embedded art: leave the artwork empty
        // so the page falls back to the station favicon (fanart), not a dead
        // /api/radio/artwork fetch.
        safeCopy(s_metaArtwork, sizeof(s_metaArtwork), "");
    } else {
        const char* file = strrchr(src, '/');
        file = file ? (file + 1) : src;
        String title, artist, album;
        bool hasArt = false;
        radioAudioReadId3Tags(src, title, artist, album, hasArt);
        safeCopy(s_metaTitle, sizeof(s_metaTitle),
                 title.length() ? title.c_str() : file);
        safeCopy(s_metaArtist, sizeof(s_metaArtist),
                 artist.length() ? artist.c_str() : "SD File");
        safeCopy(s_metaArtwork, sizeof(s_metaArtwork),
                 album.length() ? album.c_str() : "SD");
    }
}

static String radioAudioSelectBitrateUrl(const String& url) {
    int p128 = url.indexOf(F("/mp3-128/"));
    int p192 = url.indexOf(F("/mp3-192/"));
    if (p128 < 0 && p192 < 0) return url;

    bool allow192 = radioAudioHasHeadroomFor192();

    String selected = url;
    if (allow192 && p128 >= 0) {
        selected.replace(F("/mp3-128/"), F("/mp3-192/"));
        return selected;
    }

    if (!allow192 && p192 >= 0) {
        selected.replace(F("/mp3-192/"), F("/mp3-128/"));
        return selected;
    }

    return url;
}

static uint8_t clampStreamProfile(uint8_t p) {
    return (p > 3) ? 3 : p;
}

static void radioAudioScheduleReconnect(const String& reason) {
    if (!s_lastStreamUrl.length() || s_manualStop) return;

    if (s_reconnectAttempt >= kStreamReconnectFallbackAttempt && !s_queue.empty()) {
        s_pendingReconnect = false;
        s_lastError = F("Stream unstable, switching to SD queue");
        String nextMsg;
        if (!radioAudioPlayNext(nextMsg)) {
            s_lastError = nextMsg;
        }
        return;
    }

    uint8_t idx = s_reconnectAttempt;
    if (idx >= 6) idx = 5;
    s_reconnectAtMs = millis() + kReconnectBackoffMs[idx];
    s_pendingReconnect = true;
    s_reconnectAttempt++;

    if (s_reconnectAttempt >= 2 && s_streamProfile < 3) {
        s_streamProfile++;
    }

    s_lastError = reason;
}

static void radioAudioLoadQueue() {
    s_queue.clear();
    s_queueIndex = -1;

    auto ensureDefaultQueue = []() -> bool {
        bool changed = false;
        for (size_t i = 0; i < (sizeof(kDefaultRadioQueue) / sizeof(kDefaultRadioQueue[0])); i++) {
            const String candidate = kDefaultRadioQueue[i];
            bool exists = false;
            for (size_t j = 0; j < s_queue.size(); j++) {
                if (s_queue[j] == candidate) {
                    exists = true;
                    break;
                }
            }
            if (!exists && s_queue.size() < 256) {
                s_queue.push_back(candidate);
                changed = true;
            }
        }
        return changed;
    };

    if (!littlefsBegin()) return;
    File f = LittleFS.open(kRadioQueuePath, "r");
    if (!f) {
        if (ensureDefaultQueue()) {
            radioAudioSaveQueue();
        }
        return;
    }

    SpiRamJsonDocument doc(8192);
    if (deserializeJson(doc, f) != DeserializationError::Ok) {
        f.close();
        return;
    }
    f.close();

    bool migratedQueue = false;
    JsonArray arr = doc["items"].as<JsonArray>();
    for (JsonVariant v : arr) {
        String p = v.as<String>();
        if (!p.length()) continue;
        if (radioAudioReplaceLegacy80s80sUrl(p)) {
            migratedQueue = true;
        }

        bool exists = false;
        for (size_t i = 0; i < s_queue.size(); i++) {
            if (s_queue[i] == p) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            s_queue.push_back(p);
        }
        if (s_queue.size() >= 256) break;
    }

    int idx = doc["index"] | -1;
    if (!s_queue.empty() && idx >= 0 && idx < (int)s_queue.size()) {
        s_queueIndex = idx;
    }

    const bool queueSeeded = ensureDefaultQueue();
    if (migratedQueue || queueSeeded) {
        radioAudioSaveQueue();
    }
}

static void radioAudioLoadFavorites() {
    s_favorites.clear();

    auto ensureDefaultFavorites = []() {
        if (!s_favorites.empty()) return;

        RadioFavorite f1;
        f1.name = F("90s90s Radio");
        f1.url = F("https://streams.90s90s.de/main/mp3-128/homepage/");
        s_favorites.push_back(f1);

        RadioFavorite f2;
        f2.name = F("80s80s Radio");
        f2.url = F("https://streams.80s80s.de/web/mp3-128/homepage/");
        s_favorites.push_back(f2);

        radioAudioSaveFavorites();
    };

    if (!littlefsBegin()) return;
    File f = LittleFS.open(kRadioFavoritesPath, "r");
    if (!f) {
        ensureDefaultFavorites();
        return;
    }

    SpiRamJsonDocument doc(8192);
    if (deserializeJson(doc, f) != DeserializationError::Ok) {
        f.close();
        return;
    }
    f.close();

    JsonArray arr = doc["items"].as<JsonArray>();
    bool migratedFavorites = false;
    for (JsonVariant v : arr) {
        RadioFavorite fav;
        fav.name = v["name"].as<String>();
        fav.url = v["url"].as<String>();
        fav.favicon = v["favicon"].as<String>();
        if (!fav.url.length()) continue;

        if (radioAudioReplaceLegacy80s80sUrl(fav.url)) {
            migratedFavorites = true;
        }

        if (!fav.name.length()) fav.name = fav.url;

        bool exists = false;
        for (size_t i = 0; i < s_favorites.size(); i++) {
            if (s_favorites[i].url == fav.url) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            s_favorites.push_back(fav);
        }
        if (s_favorites.size() >= 64) break;
    }

    ensureDefaultFavorites();
    if (migratedFavorites) {
        radioAudioSaveFavorites();
    }
}

static void radioAudioLoadState() {
    s_resumePending = false;
    s_resumeTries = 0;
    s_resumeAtMs = 0;
    s_resumeMode = "";
    s_resumeSource = "";
    s_resumeWasActive = false;

    if (!littlefsBegin()) return;
    File f = LittleFS.open(kRadioStatePath, "r");
    if (!f) return;

    SpiRamJsonDocument doc(2048);
    if (deserializeJson(doc, f) != DeserializationError::Ok) {
        f.close();
        return;
    }
    f.close();

    int vol = doc["volume"] | 30;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    if (vol > 30) vol = 30;  // Boot cap: never start louder than 30 - only the user can raise it manually
    s_volume = (uint8_t)vol;

    s_streamProfile = clampStreamProfile((uint8_t)(doc["stream_profile"] | 1));
    s_lastStreamUrl = doc["last_stream_url"].as<String>();
    s_lastSdPath = doc["last_sd_path"].as<String>();
    s_stationFavicon = doc["station_favicon"].as<String>();
    s_stationName = doc["station_name"].as<String>();

    // EQ survives reboot: stored as the 5 band gains (-12..+12 dB).
    if (doc.containsKey("eq") && doc["eq"].is<JsonArray>()) {
        JsonArray eqArr = doc["eq"].as<JsonArray>();
        uint8_t bi = 0;
        for (JsonVariant v : eqArr) {
            if (bi >= kEqBands) break;
            int8_t g = (int8_t)v.as<int>();
            if (g < -12) g = -12;
            if (g > 12) g = 12;
            s_eqGains[bi] = g;
            bi++;
        }
        s_eqState.dirty = true;
    }

    // Auto-resume with 60s delay to let system fully boot before HTTP
    // connections.  The 3s delay caused crash-loops (async_tcp null-pointer
    // in the old HTTP audio source before network stack was ready).
    s_resumeMode = doc["last_mode"].as<String>();
    s_resumeSource = doc["last_source"].as<String>();
    s_resumeWasActive = doc["last_active"] | false;
    s_autoResume = doc["auto_resume"] | true;
    s_resumeFirst = doc["resume_first"] | false;
    if (s_autoResume && s_resumeWasActive && s_resumeSource.length()) {
        s_resumePending = true;
        s_resumeAtMs = millis() + 60000;
    }

    // Sleep timer survives refresh and reboot (epoch-based; millis fallback for
    // the pre-NTP-sync window of this session only).
    s_sleepEndEpoch = (time_t)((long)doc["sleep_end"] | 0);
    s_sleepDurationMin = (uint32_t)((int)doc["sleep_min"] | 0);
    s_sleepSetUptimeMs = 0; // reboot: rely on epoch after NTP sync
    if (s_sleepEndEpoch <= 0) {
        s_sleepDurationMin = 0;
    }
}

static bool radioAudioSaveQueue() {
    if (!littlefsBegin()) return false;

    SpiRamJsonDocument doc(8192);
    doc["index"] = s_queueIndex;
    JsonArray arr = doc.createNestedArray("items");
    for (size_t i = 0; i < s_queue.size(); i++) {
        arr.add(s_queue[i]);
    }

    String tmpPath = String(kRadioQueuePath) + ".tmp";
    File f = LittleFS.open(tmpPath, "w");
    if (!f) return false;
    size_t bytes = serializeJson(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes == 0) {
        LittleFS.remove(tmpPath);
        return false;
    }

    LittleFS.remove(kRadioQueuePath);
    if (!LittleFS.rename(tmpPath, kRadioQueuePath)) {
        LittleFS.remove(tmpPath);
        return false;
    }
    return true;
}

static bool radioAudioSaveFavorites() {
    if (!littlefsBegin()) return false;

    SpiRamJsonDocument doc(8192);
    JsonArray arr = doc.createNestedArray("items");
    for (size_t i = 0; i < s_favorites.size(); i++) {
        JsonObject o = arr.createNestedObject();
        o["name"] = s_favorites[i].name;
        o["url"] = s_favorites[i].url;
        o["favicon"] = s_favorites[i].favicon;
    }

    String tmpPath = String(kRadioFavoritesPath) + ".tmp";
    File f = LittleFS.open(tmpPath, "w");
    if (!f) return false;
    size_t bytes = serializeJson(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes == 0) {
        LittleFS.remove(tmpPath);
        return false;
    }

    LittleFS.remove(kRadioFavoritesPath);
    if (!LittleFS.rename(tmpPath, kRadioFavoritesPath)) {
        LittleFS.remove(tmpPath);
        return false;
    }
    return true;
}

static bool radioAudioSaveState() {
    if (!littlefsBegin()) return false;

    SpiRamJsonDocument doc(2048);
    doc["volume"] = s_volume;
    doc["stream_profile"] = s_streamProfile;
    doc["last_active"] = s_active;
    doc["last_mode"] = s_stream ? "stream" : (s_current.startsWith("sd:") ? "sd" : "");
    doc["last_source"] = s_stream ? s_lastStreamUrl : s_lastSdPath;
    doc["auto_resume"] = s_autoResume;
    doc["resume_first"] = s_resumeFirst;
    doc["last_stream_url"] = s_lastStreamUrl;
    doc["station_favicon"] = s_stationFavicon;
    doc["station_name"] = s_stationName;
    JsonArray eqArr = doc.createNestedArray("eq");
    for (uint8_t i = 0; i < kEqBands; i++) eqArr.add(s_eqGains[i]);
    doc["last_sd_path"] = s_lastSdPath;
    doc["sleep_end"] = (long)s_sleepEndEpoch;
    doc["sleep_min"] = (int)s_sleepDurationMin;

    String tmpPath = String(kRadioStatePath) + ".tmp";
    File f = LittleFS.open(tmpPath, "w");
    if (!f) return false;
    size_t bytes = serializeJson(doc, f);
    f.close();
    storNoteLfsWrite(bytes);
    if (bytes == 0) {
        LittleFS.remove(tmpPath);
        return false;
    }

    LittleFS.remove(kRadioStatePath);
    if (!LittleFS.rename(tmpPath, kRadioStatePath)) {
        LittleFS.remove(tmpPath);
        return false;
    }
    return true;
}

static void radioAudioTouchFavoriteByUrl(const String& url, const String& preferredName) {
    if (!url.length()) return;

    for (size_t i = 0; i < s_favorites.size(); i++) {
        if (s_favorites[i].url == url) {
            RadioFavorite hit = s_favorites[i];
            if (preferredName.length()) hit.name = preferredName;
            s_favorites.erase(s_favorites.begin() + (int)i);
            s_favorites.insert(s_favorites.begin(), hit);
            radioAudioSaveFavorites();
            return;
        }
    }
}

static void radioAudioCleanupSource() {
    if (s_decoder) {
        if (s_decoderStarted) {
            s_decoder->stop();
            delete s_decoder;
        }
        s_decoder = nullptr;
        s_decoderStarted = false;
    }
    if (s_buffered && s_buffered != s_source) {
        delete s_buffered;
    }
    s_buffered = nullptr;
    if (s_psramBuffer) {
        heap_caps_free(s_psramBuffer);
        s_psramBuffer = nullptr;
    }
    if (s_source) {
        s_source->close();
        delete s_source;
        s_source = nullptr;
    }
    if (s_stream) {
        // Fold the completed segment into the accumulated stream playing time
        // so a reconnect does not reset the "minutes playing" counter to zero.
        if (s_streamStartMs) {
            s_streamPlayedMs += (millis() - s_streamStartMs);
            s_streamStartMs = 0;
        }
    }
    s_active = false;
    s_cacheBytesInUse = 0;
    s_pendingUnmute = false;
    s_unmuteAtMs = 0;
    s_stream = false;
    if (s_output) s_output->setIdle(true);  // stop I2S clock -> no hum when idle
    radioAudioClearArtwork();
}

static bool radioAudioEnsureOutput() {
    // The I2S driver must be installed before setIdle(false) re-routes the pins
    // via i2s_set_pin(). udaDac.begin() normally does this at boot, but if it
    // was skipped (config wipe / earlier failure) install it here so the first
    // play never hits i2s_set_pin() on a NULL port driver.
    if (!udaDac.isInitialized()) {
        AudioFormat fmt(uda1334aConfig.getConfig().sampleRate,
                        uda1334aConfig.getConfig().bitDepth, 2);
        if (!udaDac.begin(fmt)) {
            s_lastError = F("I2S driver not available (UDA1334A init failed)");
            return false;
        }
    }
    if (s_output) return true;

    s_output = new EqAudioOutput();
    if (!s_output) {
        s_lastError = F("Out of memory creating EQ output");
        return false;
    }

    s_output->SetPinout(UDA1334A_I2S_BCLK_PIN, UDA1334A_I2S_LRCK_PIN, UDA1334A_I2S_DOUT_PIN);
    s_output->SetGain((float)s_volume / 100.0f);
    return true;
}

enum RadioDecoderKind : uint8_t {
    DEC_MP3 = 0,
    DEC_AAC = 1,
    DEC_FLAC = 2,
    DEC_WAV = 3,
    DEC_OGG = 4,   // T16: OGG Vorbis (stb_vorbis)
    DEC_OPUS = 5,  // T16: OGG Opus (libopus core via esp_audio_codec)
};

// Pick a decoder from the source label (URL or path extension). Streams use
// their URL; SD/PSRAM files use their filename. Falls back to MP3.
static RadioDecoderKind radioAudioPickDecoder(const String& display,
                                              const String& mimeHint = String()) {
    String m = mimeHint;
    m.toLowerCase();
    if (m.length()) {
        // A declared MIME wins over the URL extension (transcoded URLs often
        // carry no suffix).
        if (m.indexOf("mpeg") >= 0 || m.indexOf("mp3") >= 0) return DEC_MP3;
        if (m.indexOf("flac") >= 0) return DEC_FLAC;
        if (m.indexOf("aac") >= 0) return DEC_AAC;
        if (m.indexOf("wav") >= 0) return DEC_WAV;
        if (m.indexOf("opus") >= 0) return DEC_OPUS;
        if (m.indexOf("ogg") >= 0 || m.indexOf("vorbis") >= 0) return DEC_OGG;
    }
    String d = display;
    d.toLowerCase();
    if (d.endsWith(".flac")) return DEC_FLAC;
    if (d.endsWith(".aac") || d.endsWith(".m4a") || d.endsWith(".m4b") ||
        d.indexOf("aac") >= 0) {
        return DEC_AAC;
    }
    if (d.endsWith(".wav")) return DEC_WAV;
    if (d.endsWith(".opus")) return DEC_OPUS;
    if (d.endsWith(".ogg")) return DEC_OGG;
    return DEC_MP3;
}

// Reject clearly-unsupported audio formats (no bundled decoder). Unknown/no
// extension is allowed - the decoder auto-detects at playback. A declared MIME
// hint (mimeHint) overrides the URL-extension checks.
static bool radioAudioIsSupportedStream(const String& url, const String& mimeHint = String()) {
    String m = mimeHint;
    m.toLowerCase();
    if (m.length()) {
        if (m.indexOf("audio/mpeg") >= 0 || m.indexOf("audio/mp3") >= 0 ||
            m.indexOf("audio/flac") >= 0 || m.indexOf("audio/aac") >= 0 ||
            m.indexOf("audio/x-aac") >= 0 || m.indexOf("audio/mp4") >= 0 ||
            m.indexOf("audio/wav") >= 0 || m.indexOf("audio/x-wav") >= 0 ||
            m.indexOf("audio/ogg") >= 0 || m.indexOf("audio/opus") >= 0) {
            return true;
        }
        if (m.indexOf("audio/") >= 0) return false; // declared but not decodable
    }
    String l = url;
    int q = l.indexOf('?');
    if (q >= 0) l = l.substring(0, q);
    int h = l.indexOf('#');
    if (h >= 0) l = l.substring(0, h);
    l.toLowerCase();
    if (l.endsWith(".mp3") || l.endsWith(".flac") ||
        l.endsWith(".aac") || l.endsWith(".m4a") || l.endsWith(".m4b") ||
        l.endsWith(".wav") || l.endsWith(".ogg") || l.endsWith(".opus")) {
        return true;
    }
    if (l.endsWith(".m3u8") || l.endsWith(".m3u") || l.endsWith(".pls") ||
        l.endsWith(".asx") || l.endsWith(".mpd") || l.endsWith(".hls") ||
        l.endsWith(".ts") || l.endsWith(".flv") ||
        l.endsWith(".mp4") ||
        l.endsWith(".wma") || l.endsWith(".wmv") || l.endsWith(".aiff") ||
        l.endsWith(".aif") || l.endsWith(".ape") || l.endsWith(".mka") ||
        l.endsWith(".ra") || l.endsWith(".rm") || l.endsWith(".mid") ||
        l.endsWith(".midi") || l.endsWith(".mod") || l.endsWith(".s3m")) {
        return false;
    }
    return true; // unknown -> auto-detect
}

// A station is only shown in the UI when its direct URL is a format the
// bundled decoders can actually start (MP3, FLAC, AAC, WAV, OGG, Opus).
// HLS/playlists (m3u8/m3u/pls/mpd...) and clearly-unsupported codecs are
// skipped so broken stations never appear.
static bool radioAudioIsPlayableStream(const String& url, const String& codec) {
    String l = url;
    int q = l.indexOf('?');
    if (q >= 0) l = l.substring(0, q);
    l.toLowerCase();
    if (l.endsWith(".m3u8") || l.endsWith(".m3u") || l.endsWith(".pls") ||
        l.endsWith(".asx") || l.endsWith(".mpd") || l.endsWith(".hls") ||
        l.endsWith(".ts") || l.endsWith(".flv")) {
        return false;
    }
    String c = codec;
    c.toLowerCase();
    if (c.length()) {
        if (c.indexOf("hls") >= 0 || c.indexOf("wma") >= 0 ||
            c.indexOf("mpd") >= 0 ||
            c.indexOf("dash") >= 0 || c.indexOf("flv") >= 0) {
            return false;
        }
    }
    return true;
}

// FLAC safety: only 44.1/48 kHz 16-bit decodes reliably on this ESP32.
static bool radioAudioIsSafeFlacBuffer(const uint8_t* buf, size_t len) {
    if (!buf || len < 42) return true;
    if (memcmp(buf, "fLaC", 4) != 0) return true;              // not FLAC -> let decoder decide
    if ((buf[4] & 0x7F) != 0) return true;                     // STREAMINFO not first
    uint32_t sampleRate = ((uint32_t)buf[18] << 12) | ((uint32_t)buf[19] << 4) | ((uint32_t)buf[20] >> 4);
    uint8_t bits = ((buf[20] & 0x01) << 4) | (buf[21] >> 4);
    bits += 1;
    return sampleRate <= 48000 && bits <= 16;
}

static bool radioAudioIsSafeFlacFile(const char* path) {
    if (!sdMutexLockFast(pdMS_TO_TICKS(8000))) return true;
    bool safe = true;
    File f = SD_MMC.open(path, FILE_READ);
    if (f) {
        uint8_t buf[42];
        size_t got = f.read(buf, sizeof(buf));
        f.close();
        safe = radioAudioIsSafeFlacBuffer(buf, got);
    }
    sdMutexUnlockFast();
    return safe;
}

// Best-effort duration probe for local SD files (ms, 0 = unknown).
// FLAC uses the STREAMINFO block (exact); MP3 uses the Xing/Info/VBRI frame
// count (exact) and falls back to a CBR estimate from the first frame header.
// AAC and unknown formats return 0 so the page shows only the elapsed time
// (AAC is disabled in this build anyway).
static uint32_t radioAudioProbeFlacDuration(const uint8_t* buf, size_t len) {
    if (len < 30 || memcmp(buf, "fLaC", 4) != 0) return 0;
    if ((buf[4] & 0x7F) != 0) return 0;  // STREAMINFO must be the first block
    uint32_t sampleRate = ((uint32_t)buf[18] << 12) | ((uint32_t)buf[19] << 4) | ((uint32_t)buf[20] >> 4);
    uint64_t total = ((uint64_t)(buf[21] & 0x0F) << 32) |
                     ((uint64_t)buf[22] << 24) |
                     ((uint64_t)buf[23] << 16) |
                     ((uint64_t)buf[24] << 8) |
                     ((uint64_t)buf[25]);
    if (sampleRate == 0 || total == 0) return 0;
    uint64_t ms = total * 1000ULL / sampleRate;
    return (ms > 0x7FFFFFFFULL) ? 0 : (uint32_t)ms;
}

static uint32_t radioAudioProbeMp3Duration(const uint8_t* buf, size_t len, uint32_t fileSize) {
    size_t dataStart = 0;
    if (len >= 10 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
        dataStart = 10 + ((size_t)(buf[6] & 0x7F) << 21) + ((size_t)(buf[7] & 0x7F) << 14) +
                    ((size_t)(buf[8] & 0x7F) << 7) + (size_t)(buf[9] & 0x7F);
    }
    size_t i = dataStart;
    while (i + 4 <= len) {
        if (buf[i] == 0xFF && (buf[i + 1] & 0xE0) == 0xE0) break;
        i++;
    }
    if (i + 4 > len) return 0;
    const uint8_t* h = buf + i;
    int ver = (h[1] >> 3) & 0x03;   // 3=MPEG1, 2=MPEG2, 0=MPEG2.5
    int layer = (h[1] >> 1) & 0x03; // 3=Layer I, 2=Layer II, 1=Layer III
    if (ver == 1 || layer != 1) return 0;  // reserved version, or not Layer III
    int bIdx = (h[2] >> 4) & 0x0F;
    int sIdx = (h[2] >> 2) & 0x03;
    if (bIdx == 0 || bIdx == 15 || sIdx == 3) return 0;
    uint32_t sampleRate;
    if (ver == 3)      { static const uint32_t t[3] = {44100, 48000, 32000}; sampleRate = t[sIdx]; }
    else if (ver == 2) { static const uint32_t t[3] = {22050, 24000, 16000}; sampleRate = t[sIdx]; }
    else               { static const uint32_t t[3] = {11025, 12000, 8000};  sampleRate = t[sIdx]; }
    uint32_t bitrate;
    uint16_t spf;
    if (ver == 3) {
        static const uint16_t t[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
        bitrate = t[bIdx]; spf = 1152;
    } else {
        static const uint16_t t[16] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
        bitrate = t[bIdx]; spf = 576;
    }
    if (bitrate == 0) return 0;

    uint32_t frames = 0;
    bool haveFrames = false;
    size_t xoff = i + 4 + ((ver == 3) ? (((h[3] >> 6) & 0x03) == 3 ? 17 : 32) : (((h[3] >> 6) & 0x03) == 3 ? 9 : 17));
    if (xoff + 12 <= len) {
        if (memcmp(buf + xoff, "Xing", 4) == 0 || memcmp(buf + xoff, "Info", 4) == 0) {
            uint32_t flags = ((uint32_t)buf[xoff + 4] << 24) | ((uint32_t)buf[xoff + 5] << 16) |
                             ((uint32_t)buf[xoff + 6] << 8) | buf[xoff + 7];
            if (flags & 0x01) {
                frames = ((uint32_t)buf[xoff + 8] << 24) | ((uint32_t)buf[xoff + 9] << 16) |
                         ((uint32_t)buf[xoff + 10] << 8) | buf[xoff + 11];
                haveFrames = true;
            }
        } else {
            size_t voff = i + 4 + 32;
            if (voff + 18 <= len && memcmp(buf + voff, "VBRI", 4) == 0) {
                frames = ((uint32_t)buf[voff + 14] << 24) | ((uint32_t)buf[voff + 15] << 16) |
                         ((uint32_t)buf[voff + 16] << 8) | buf[voff + 17];
                haveFrames = true;
            }
        }
    }
    if (haveFrames && frames > 0) {
        uint64_t ms = (uint64_t)frames * (uint64_t)spf * 1000ULL / sampleRate;
        return (ms > 0x7FFFFFFFULL) ? 0 : (uint32_t)ms;
    }
    if (fileSize > dataStart) {
        uint64_t ms = (uint64_t)(fileSize - dataStart) * 8ULL / bitrate;  // CBR estimate
        return (ms > 0x7FFFFFFFULL) ? 0 : (uint32_t)ms;
    }
    return 0;
}

static String s_durProbePath;
static uint32_t s_durProbeSize = 0;
static uint32_t s_durProbeMs = 0;

static uint32_t radioAudioProbeDurationCached(const char* path, uint32_t fileSize) {
    if (s_durProbePath != path || s_durProbeSize != fileSize) {
        String lower = path;
        lower.toLowerCase();
        bool isFlac = lower.endsWith(".flac");
        bool isMp3 = lower.endsWith(".mp3");
        s_durProbeMs = 0;
        if ((isFlac || isMp3) && fileSize >= 256) {
            const uint32_t kHead = 16384;
            uint8_t* buf = (uint8_t*)heap_caps_malloc(kHead, MALLOC_CAP_8BIT);
            if (buf) {
                if (sdMutexLockFast(pdMS_TO_TICKS(2000))) {
                    File f = SD_MMC.open(path, FILE_READ);
                    if (f) {
                        size_t got = f.read(buf, kHead);
                        f.close();
                        if (got > 0) {
                            s_durProbeMs = isFlac ? radioAudioProbeFlacDuration(buf, got)
                                                  : radioAudioProbeMp3Duration(buf, got, fileSize);
                        }
                    }
                    sdMutexUnlockFast();
                }
                heap_caps_free(buf);
            }
        }
        s_durProbePath = path;
        s_durProbeSize = fileSize;
    }
    return s_durProbeMs;
}

static bool radioAudioStartWithSource(AudioSource* src,
                                      uint32_t preferredCacheBytes,
                                      uint32_t minCacheBytes,
                                      uint16_t prebufferMs,
                                      bool isStream,
                                      const String& display,
                                      String& messageOut,
                                      bool sourceSelfBuffered = false) {
    radioAudioCleanupSource();
    s_metaIcyActive = false;   // new source: no ICY metadata until it arrives


    if (!radioAudioEnsureOutput()) {
        messageOut = s_lastError;
        return false;
    }

    s_source = src;
    if (!s_source) {
        s_lastError = F("Invalid audio source");
        messageOut = s_lastError;
        return false;
    }

    uint32_t cacheTry = preferredCacheBytes;
    if (sourceSelfBuffered) {
        // The ring source IS the buffer (deep PSRAM, filled by a background
        // task) — no extra wrapper on top. The decoder reads it directly.
        s_cacheBytesInUse = cacheTry;
        s_psramBuffer = nullptr;
        s_buffered = s_source;
    } else {
        while (cacheTry >= minCacheBytes) {
            s_psramBuffer = nullptr;
            if (psramFound()) {
                s_psramBuffer = heap_caps_malloc(cacheTry, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
            if (s_psramBuffer) {
                memset(s_psramBuffer, 0, cacheTry);
                s_buffered = new AudioSourceBuffer(s_source, s_psramBuffer, cacheTry);
                if (s_buffered) {
                    s_cacheBytesInUse = cacheTry;
                    break;
                }
                heap_caps_free(s_psramBuffer);
                s_psramBuffer = nullptr;
            }
            if (cacheTry <= (64UL * 1024UL)) break;
            cacheTry /= 2;
        }
    }

    if (!s_buffered) {
        s_lastError = F("Out of memory creating cache buffer (PSRAM required)");
        messageOut = s_lastError;
        radioAudioCleanupSource();
        return false;
    }

    {
        char buf[96];
        snprintf(buf, sizeof(buf), "[RADIO] Cache: %lu KB in PSRAM (%s)",
                 (unsigned long)(s_cacheBytesInUse / 1024), isStream ? "stream" : "sd");
        sendVirtualMessage(buf, INFO);
    }

    RadioDecoderKind kind = (s_decoderOverride >= 0)
                                ? (RadioDecoderKind)s_decoderOverride
                                : radioAudioPickDecoder(display);
    s_decoderOverride = -1;
    switch (kind) {
        case DEC_AAC:
            s_decoder = new AacDecoder();
            if (!s_decoder) s_lastError = F("Out of memory creating AAC decoder");
            break;
        case DEC_FLAC:
            s_decoder = new FlacDecoder();
            if (!s_decoder) s_lastError = F("Out of memory creating FLAC decoder");
            break;
        case DEC_WAV:
            s_decoder = new WavDecoder();
            if (!s_decoder) s_lastError = F("Out of memory creating WAV decoder");
            break;
        case DEC_OGG:
            s_decoder = new OggVorbisDecoder();
            if (!s_decoder) s_lastError = F("Out of memory creating OGG decoder");
            break;
        case DEC_OPUS:
            s_decoder = new OggOpusDecoder();
            if (!s_decoder) s_lastError = F("Out of memory creating Opus decoder");
            break;
        default:
            s_decoder = new Minimp3Decoder();
            if (!s_decoder) s_lastError = F("Out of memory creating MP3 decoder");
            break;
    }
    if (!s_decoder) {
        messageOut = s_lastError;
        radioAudioCleanupSource();
        return false;
    }

    if (s_output) s_output->setIdle(false);  // resume I2S clock for playback
    if (!s_decoder->begin(s_buffered, s_output)) {
        s_lastError = F("Failed to start decoder");
        messageOut = s_lastError;
        radioAudioCleanupSource();
        return false;
    }

    s_decoderStarted = true;
    s_stream = isStream;
    s_current = display;
    s_lastError = "";
    s_active = true;
    s_output->SetGain(0.0f);
    s_pendingUnmute = true;
    s_unmuteAtMs = millis() + prebufferMs;
    s_streamStartMs = millis();
    radioAudioSaveState();
    messageOut = F("Playback started");
    return true;
}

static void radioAudioRememberQueueItem(const String& path) {
    int found = -1;
    for (size_t i = 0; i < s_queue.size(); i++) {
        if (s_queue[i] == path) {
            found = (int)i;
            break;
        }
    }
    if (found >= 0) {
        s_queueIndex = found;
    } else {
        s_queue.push_back(path);
        if (s_queue.size() > 256) {
            s_queue.erase(s_queue.begin());
        }
        s_queueIndex = (int)s_queue.size() - 1;
    }
    radioAudioSaveQueue();
}

// SD playback now goes through AudioSourceSdRing (audio_source_sd_ring.h): a
// PSRAM ring fed by a background task, mirroring the HTTP ring source. The
// decode loop never blocks on the SD card, and the first chunk is preloaded
// before the decoder begins so the wall/exp ratio stays ~1.0 from the start.

static bool radioAudioPlaySdInternal(const String& path, String& messageOut, bool updateQueue) {
    if (!sdMutexLockFast(pdMS_TO_TICKS(8000))) {
        messageOut = F("SD busy");
        return false;
    }
    if (!SD_MMC.exists(path.c_str())) {
        sdMutexUnlockFast();
        messageOut = F("File not found on SD");
        return false;
    }

    String lower = path;
    lower.toLowerCase();
    if (lower.endsWith(".flac") && !radioAudioIsSafeFlacFile(path.c_str())) {
        sdMutexUnlockFast();
        messageOut = F("Unsupported high-res FLAC (only 44.1/48 kHz 16-bit)");
        return false;
    }
    sdMutexUnlockFast();

    AudioSourceSdRing* src = new AudioSourceSdRing(path.c_str(), kSdRingBytes);
    if (!src || !src->isOpen()) {
        if (src) delete src;
        messageOut = F("Failed to open SD file for playback");
        return false;
    }

    s_manualStop = false;
    s_pendingReconnect = false;
    s_reconnectAttempt = 0;
    s_stationFavicon = "";
    s_lastSdPath = path;

    String display = F("sd:");
    display += path;
    s_decoderOverride = -1; // auto-detect from the SD file extension
    s_streamSession = false; // local file: per-track elapsed, not stream accumulation
    bool ok = radioAudioStartWithSource(src,
                                        kSdRingBytes,
                                        kSdCacheMinBytes,
                                        kSdPrebufferMs,
                                        false,
                                        display,
                                        messageOut,
                                        true);   // sourceSelfBuffered: ring IS the buffer
    if (!ok) return false;

    if (updateQueue) {
        radioAudioRememberQueueItem(path);
    }

    radioAudioSaveState();

    return true;
}

// The HTTP source does not follow 3xx redirects (setFollowRedirects is
// guarded with #ifndef ESP32). Many radio stations answer with a 302 to a
// signed CDN URL, so we walk the redirect chain here and hand the final URL
// to the source.
static bool radioAudioResolveStreamUrl(const String& inUrl, String& outUrl, String& errorOut) {
    String url = inUrl;
    for (int hops = 0; hops < 6; hops++) {
        HTTPClient http;
        http.setTimeout(8000);
        http.setReuse(false);
        http.begin(url);
        int code = http.GET();
        if (code < 0) {
            errorOut = http.errorToString(code);
            http.end();
            return false;
        }
        if (code >= 300 && code < 400) {
            String loc = http.getLocation();
            http.end();
            if (!loc.length() || !(loc.startsWith("http://") || loc.startsWith("https://"))) {
                errorOut = F("Bad redirect target");
                return false;
            }
            url = loc;
            continue;
        }
        http.end();
        if (code != HTTP_CODE_OK) {
            errorOut = F("HTTP ");
            errorOut += String(code);
            return false;
        }
        break;
    }
    outUrl = url;
    return true;
}

// Percent-encode a query string fragment for use in an HTTP request URL.
static void radioAudioUrlEncode(const String& in, String& out) {
    out = String();
    out.reserve(in.length() * 3);
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            out += buf;
        }
    }
}

// ============================================================
// Tags / Countries lists - fetched from radio-browser, PSRAM cache (24 h TTL)
// ============================================================
static const uint32_t kMetaCacheTtlMs = 24UL * 60UL * 60UL * 1000UL;
static const uint16_t kMetaCacheBytes = 8192;
static uint8_t* s_metaTagsData = nullptr;
static uint16_t s_metaTagsLen = 0;
static uint32_t s_metaTagsTs = 0;
static uint8_t* s_metaCountriesData = nullptr;
static uint16_t s_metaCountriesLen = 0;
static uint32_t s_metaCountriesTs = 0;

static bool radioAudioMetaCacheHit(uint8_t* data, uint16_t len, uint32_t ts, String& jsonOut) {
    if (!data || len == 0) return false;
    if ((uint32_t)(millis() - ts) > kMetaCacheTtlMs) return false;
    jsonOut = String((const char*)data, len);
    return true;
}

static void radioAudioMetaCachePut(uint8_t*& data, uint16_t& len, uint32_t& ts, const String& json) {
    if (json.length() == 0 || json.length() >= kMetaCacheBytes) return;
    if (!data) {
        data = psramFound() ? (uint8_t*)heap_caps_malloc(kMetaCacheBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
        if (!data) data = (uint8_t*)malloc(kMetaCacheBytes);
        if (data) memset(data, 0, kMetaCacheBytes);
    }
    if (!data) return;
    memcpy(data, json.c_str(), json.length());
    data[json.length()] = '\0';
    len = (uint16_t)json.length();
    ts = millis();
}

// mode: "tags" -> [{name,count}], "countries" -> [{name,code}]
static bool radioAudioFetchMeta(const char* endpoint, const char* mode,
                                uint8_t*& cacheData, uint16_t& cacheLen, uint32_t& cacheTs,
                                String& jsonOut, String& errorOut) {
    if (radioAudioMetaCacheHit(cacheData, cacheLen, cacheTs, jsonOut)) return true;

    static const char* kSearchBases[] = {
        "http://de1.api.radio-browser.info",
        "http://all.api.radio-browser.info",
    };
    String payload;
    errorOut = String();
    for (size_t b = 0; b < sizeof(kSearchBases) / sizeof(kSearchBases[0]); b++) {
        String url = String(kSearchBases[b]) + endpoint;
        HTTPClient http;
        http.setTimeout(5000);
        http.setReuse(false);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.begin(url);
        http.setUserAgent(F("Node32-HUB/1.0"));
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            payload = http.getString();
            http.end();
            break;
        }
        if (code < 0) errorOut = http.errorToString(code);
        http.end();
        payload = String();
    }
    if (!payload.length()) {
        if (!errorOut.length()) errorOut = F("No data from radio-browser.info");
        return false;
    }

    DynamicJsonDocument in(16384);
    if (deserializeJson(in, payload)) {
        errorOut = F("Invalid response from radio-browser.info");
        return false;
    }
    DynamicJsonDocument out(8192);
    JsonArray outArr = out.to<JsonArray>();
    JsonArray inArr = in.as<JsonArray>();
    int n = 0;
    if (strcmp(mode, "tags") == 0) {
        for (JsonObject t : inArr) {
            String name = t["name"].as<String>();
            if (!name.length()) name = t["tag"].as<String>();
            if (!name.length()) continue;
            JsonObject o = outArr.createNestedObject();
            o["name"] = name;
            o["count"] = t["stationcount"].as<int>();
            if (++n >= 50) break;
        }
    } else {
        for (JsonObject c : inArr) {
            String code = c["iso_3166_1"].as<String>();
            if (!code.length()) continue;
            JsonObject o = outArr.createNestedObject();
            o["name"] = c["name"].as<String>();
            o["code"] = code;
            if (++n >= 50) break;
        }
    }
    jsonOut = String();
    serializeJson(outArr, jsonOut);
    radioAudioMetaCachePut(cacheData, cacheLen, cacheTs, jsonOut);
    return true;
}

bool radioAudioGetTags(String& jsonOut, String& errorOut) {
    return radioAudioFetchMeta("/json/tags?limit=100&order=stationcount", "tags",
                               s_metaTagsData, s_metaTagsLen, s_metaTagsTs, jsonOut, errorOut);
}

bool radioAudioGetCountries(String& jsonOut, String& errorOut) {
    return radioAudioFetchMeta("/json/countries?order=stationcount", "countries",
                               s_metaCountriesData, s_metaCountriesLen, s_metaCountriesTs, jsonOut, errorOut);
}

// Default radio stations from the built-in queue (80s80s/90s90s), deduplicated
// to the 128 kbps variant with a friendly name. Single source of truth so the
// web UI never drifts from kDefaultRadioQueue.
String radioAudioDefaultStationsJson() {
    SpiRamJsonDocument doc(16384);
    doc["success"] = true;
    JsonArray arr = doc.createNestedArray("stations");
    for (size_t i = 0; i < sizeof(kDefaultRadioQueue) / sizeof(kDefaultRadioQueue[0]); i++) {
        String u = String(kDefaultRadioQueue[i]);
        if (u.indexOf("mp3-192") >= 0) continue; // keep only the 128 kbps variant

        String brand = "80s80s";
        if (u.indexOf("streams.90s90s.de") >= 0) brand = "90s90s";

        String rest = u;
        rest.replace(String("https://streams.") + brand + ".de/", "");
        int slash = rest.indexOf('/');
        String genre = (slash >= 0) ? rest.substring(0, slash) : rest;
        if (genre.startsWith("80s80s")) genre = genre.substring(6);
        else if (genre.startsWith("90s90s")) genre = genre.substring(6);
        genre.replace("-", " ");
        genre.trim();
        if (genre.length()) {
            String cap = genre;
            cap.toLowerCase();
            cap[0] = (char)toupper((unsigned char)cap[0]);
            genre = cap;
        }

        JsonObject o = arr.createNestedObject();
        o["name"] = brand + " " + genre;
        o["url"] = u;
    }
    String out;
    serializeJson(doc, out);
    return out;
}
 
// ============================================================
// radio-browser.info search cache - PSRAM, tiny entries (~KB)
// ============================================================
static const uint8_t kSearchCacheSlots = 4;
static const uint16_t kSearchCacheSlotBytes = 8192;
static const uint32_t kSearchCacheTtlMs = 30UL * 60UL * 1000UL;

struct SearchCacheSlot {
    char key[128];
    uint8_t* data;   // PSRAM
    uint16_t len;
    uint32_t lastUsedMs;
};
static SearchCacheSlot s_searchCache[kSearchCacheSlots];
static bool s_searchCacheReady = false;

static void radioAudioSearchCacheInit() {
    if (s_searchCacheReady) return;
    for (uint8_t i = 0; i < kSearchCacheSlots; i++) {
        s_searchCache[i].key[0] = '\0';
        s_searchCache[i].len = 0;
        s_searchCache[i].lastUsedMs = 0;
        s_searchCache[i].data = nullptr;
        if (psramFound()) {
            s_searchCache[i].data = (uint8_t*)heap_caps_malloc(kSearchCacheSlotBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!s_searchCache[i].data) {
            s_searchCache[i].data = (uint8_t*)malloc(kSearchCacheSlotBytes);
        }
        if (s_searchCache[i].data) memset(s_searchCache[i].data, 0, kSearchCacheSlotBytes);
    }
    s_searchCacheReady = true;
}

static bool radioAudioSearchCacheGet(const String& key, String& jsonOut) {
    radioAudioSearchCacheInit();
    if (!key.length()) return false;
    uint32_t now = millis();
    for (uint8_t i = 0; i < kSearchCacheSlots; i++) {
        if (s_searchCache[i].len == 0 || s_searchCache[i].data == nullptr) continue;
        if (strncmp(s_searchCache[i].key, key.c_str(), sizeof(s_searchCache[i].key) - 1) != 0) continue;
        if ((uint32_t)(now - s_searchCache[i].lastUsedMs) > kSearchCacheTtlMs) {
            s_searchCache[i].len = 0;
            continue;
        }
        s_searchCache[i].lastUsedMs = now;
        jsonOut = String((const char*)s_searchCache[i].data, s_searchCache[i].len);
        return true;
    }
    return false;
}

static void radioAudioSearchCachePut(const String& key, const String& json) {
    radioAudioSearchCacheInit();
    if (!key.length() || json.length() == 0 || json.length() > kSearchCacheSlotBytes - 1) return;
    uint32_t now = millis();
    int oldest = -1;
    uint32_t oldestTs = (uint32_t)-1;
    for (uint8_t i = 0; i < kSearchCacheSlots; i++) {
        if (s_searchCache[i].data == nullptr) continue;
        if (strncmp(s_searchCache[i].key, key.c_str(), sizeof(s_searchCache[i].key) - 1) == 0) {
            s_searchCache[i].len = 0;  // evict same-key (different offset) if needed
        }
        if (s_searchCache[i].len == 0) { oldest = i; break; }
        if ((uint32_t)(now - s_searchCache[i].lastUsedMs) > oldestTs) {
            oldestTs = now - s_searchCache[i].lastUsedMs;
            oldest = i;
        }
    }
    if (oldest < 0 || s_searchCache[oldest].data == nullptr) return;
    strncpy(s_searchCache[oldest].key, key.c_str(), sizeof(s_searchCache[oldest].key) - 1);
    s_searchCache[oldest].key[sizeof(s_searchCache[oldest].key) - 1] = '\0';
    memcpy(s_searchCache[oldest].data, json.c_str(), json.length());
    s_searchCache[oldest].data[json.length()] = '\0';
    s_searchCache[oldest].len = (uint16_t)json.length();
    s_searchCache[oldest].lastUsedMs = now;
}

bool radioAudioSearchStations(const String& name, const String& genre,
                              const String& countrycode, int offset, int limit,
                              String& jsonOut, String& errorOut,
                              bool* hasMoreOut) {
    if (limit <= 0) limit = 50;
    if (limit > 50) limit = 50;
    if (offset < 0) offset = 0;
    if (hasMoreOut) *hasMoreOut = false;

    String encName, encGenre, encCountry;
    radioAudioUrlEncode(name, encName);
    radioAudioUrlEncode(genre, encGenre);
    radioAudioUrlEncode(countrycode, encCountry);

    String cacheKey = String(offset) + "|" + String(limit) + "|" + encName + "|" + encGenre + "|" + encCountry;
    if (radioAudioSearchCacheGet(cacheKey, jsonOut)) {
        return true;
    }

    static const char* kSearchBases[] = {
        "http://de1.api.radio-browser.info",
        "http://all.api.radio-browser.info",
    };

    // fields= keeps the raw response small (memory-frugal parse).
    static const char kFields[] = "stationuuid,name,url,url_resolved,favicon,country,countrycode,tags,bitrate,codec,homepage";

    String payload;
    errorOut = String();
    for (size_t b = 0; b < sizeof(kSearchBases) / sizeof(kSearchBases[0]); b++) {
        String url = String(kSearchBases[b]);
        url += "/json/stations/search?hidebroken=true&hls=0&limit=";
        url += String(limit);
        url += "&offset=";
        url += String(offset);
        url += "&fields=";
        url += kFields;
        if (encName.length()) {
            url += "&name=";
            url += encName;
        }
        if (encGenre.length()) {
            url += "&tag=";
            url += encGenre;
        }
        if (encCountry.length()) {
            url += "&countrycode=";
            url += encCountry;
        }
        // Browsing by country/genre (no name search): show the most clicked
        // stations first, so picking a country or genre lists its top radios.
        if (!encName.length()) {
            url += "&order=clickcount&reverse=true";
        }

        HTTPClient http;
        http.setTimeout(5000);
        http.setReuse(false);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.begin(url);
        http.setUserAgent(F("Node32-HUB/1.0"));
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            payload = http.getString();
            http.end();
            break;
        }
        if (code < 0) errorOut = http.errorToString(code);
        http.end();
        payload = String();
    }

    if (!payload.length()) {
        if (!errorOut.length()) errorOut = F("No result from radio-browser.info");
        jsonOut = F("[]");
        return false;
    }

    // radio-browser.info ignores `fields=` and returns every column (~1.1 KB per
    // station, ~30 KB for a full page), which overflowed the old 16 KB internal
    // document and made country/genre browsing fail with "Error" in the UI.
    // Filter the parse down to the columns the page uses and keep both documents
    // in PSRAM so the internal heap stays free for audio.
    StaticJsonDocument<512> flt;
    JsonObject fo = flt.createNestedObject();  // root is an array: applies to each element
    fo["name"] = true;
    fo["url"] = true;
    fo["url_resolved"] = true;
    fo["favicon"] = true;
    fo["countrycode"] = true;
    fo["tags"] = true;
    fo["bitrate"] = true;
    fo["codec"] = true;
    SpiRamJsonDocument in(16384);
    DeserializationError derr = deserializeJson(in, payload, DeserializationOption::Filter(flt));
    if (derr) {
        errorOut = F("Invalid response from radio-browser.info");
        jsonOut = F("[]");
        return false;
    }

    SpiRamJsonDocument out(16384);
    JsonArray outArr = out.to<JsonArray>();
    JsonArray inArr = in.as<JsonArray>();
    size_t rawCount = 0;
    for (JsonObject st : inArr) {
        rawCount++;
        String resolved = st["url_resolved"].as<String>();
        String raw = st["url"].as<String>();
        String playUrl = resolved.length() ? resolved : raw;
        if (!radioAudioIsPlayableStream(playUrl, st["codec"].as<String>())) {
            continue;  // HLS/playlist/unsupported codec — never show in the UI
        }
        int br = st["bitrate"].as<int>();
        if (br > 320) {
            continue;  // bitrate too high for smooth ESP32 decoding (stutter/fail)
        }
        JsonObject o = outArr.createNestedObject();
        o["name"] = st["name"].as<String>();
        o["url"] = playUrl;
        o["favicon"] = st["favicon"].as<String>();
        o["countrycode"] = st["countrycode"].as<String>();
        o["genre"] = st["tags"].as<String>();
        o["bitrate"] = br;
        if (outArr.size() >= (size_t)limit) break;
    }
    // A full raw page means more results probably exist (filtering above only
    // removes stations, it never adds them). This keeps Next/Prev working even
    // when a page ends up with fewer playable stations than the limit.
    if (hasMoreOut) *hasMoreOut = rawCount >= (size_t)limit;
    jsonOut = String();
    serializeJson(outArr, jsonOut);
    radioAudioSearchCachePut(cacheKey, jsonOut);
    return true;
}

static bool radioAudioPlayUrlInternal(const String& url, String& messageOut, bool fromReconnect) {
    // One-shot MIME hint (e.g. from a DIDL protocolInfo) for this play request.
    // Consumed at the top — even on early failure — so it never leaks into a
    // later play that did not declare a MIME.
    String mimeHint = s_playMimeHint;
    s_playMimeHint = String();

    if (url.length() < 8) {
        messageOut = F("Invalid URL");
        return false;
    }

    if (!(url.startsWith("http://") || url.startsWith("https://"))) {
        messageOut = F("URL must start with http:// or https://");
        return false;
    }

    if (!radioAudioIsSupportedStream(url, mimeHint)) {
        messageOut = F("Unsupported audio format - cannot be played");
        return false;
    }

    String selectedUrl = radioAudioSelectBitrateUrl(url);

    String streamUrl;
    String resolveErr;
    bool resolved = radioAudioResolveStreamUrl(selectedUrl, streamUrl, resolveErr);
    if (!resolved && selectedUrl.startsWith("https://")) {
        // TLS to the stream host is unreliable on this build — retry the same
        // stream over plain HTTP (most servers serve both).
        String httpUrl = String("http://") + selectedUrl.substring(8);
        sendVirtualMessage(F("[RADIO] HTTPS unreachable, retrying over HTTP"), INFO);
        selectedUrl = httpUrl;
        resolved = radioAudioResolveStreamUrl(httpUrl, streamUrl, resolveErr);
    }
    if (!resolved) {
        messageOut = F("Stream unreachable (");
        messageOut += resolveErr;
        messageOut += F(")");
        sendVirtualMessage(messageOut, WARN);
        return false;
    }
    if (streamUrl != selectedUrl) {
        char buf[192];
        snprintf(buf, sizeof(buf), "[RADIO] Stream redirect -> %s",
                 streamUrl.c_str());
        sendVirtualMessage(buf, INFO);
        selectedUrl = streamUrl;
    }

    s_manualStop = false;
    s_lastStreamUrl = selectedUrl;
    s_decoderOverride = (int8_t)radioAudioPickDecoder(url, mimeHint); // detect from the original station URL

    uint8_t p = clampStreamProfile(s_streamProfile);
    // The HTTP ring source IS the PSRAM buffer (background fill task), so we
    // skip the AudioSourceBuffer wrapper entirely (sourceSelfBuffered).
    AudioSourceHttpRing* src = new AudioSourceHttpRing(selectedUrl.c_str(),
                                                              kStreamCachePreferred[p]);
    if (!src) {
        messageOut = F("Out of memory creating HTTP source");
        return false;
    }
    bool ok = radioAudioStartWithSource(src,
                                        kStreamCachePreferred[p],
                                        kStreamCacheMin[p],
                                        kStreamPrebufferMs[p],
                                        true,
                                        selectedUrl,
                                        messageOut,
                                        true);
    if (!ok && selectedUrl.startsWith("https://")) {
        // Resolve worked over TLS but the decoder got no data — retry plain HTTP.
        String httpUrl = String("http://") + selectedUrl.substring(8);
        sendVirtualMessage(F("[RADIO] HTTPS decode failed, retrying over HTTP"), INFO);
        s_lastError = "";
        AudioSourceHttpRing* src2 = new AudioSourceHttpRing(httpUrl.c_str(),
                                                                   kStreamCachePreferred[p]);
        if (src2) {
            s_manualStop = false;
            s_lastStreamUrl = httpUrl;
            s_decoderOverride = (int8_t)radioAudioPickDecoder(url, mimeHint);
            ok = radioAudioStartWithSource(src2,
                                           kStreamCachePreferred[p],
                                           kStreamCacheMin[p],
                                           kStreamPrebufferMs[p],
                                           true,
                                           httpUrl,
                                           messageOut,
                                           true);
        }
    }
    if (!ok) {
        sendVirtualMessage(String(F("[RADIO] Play failed: ")) + s_lastError, WARN);
        return false;
    }

    s_pendingReconnect = false;
    if (!fromReconnect) {
        s_reconnectAttempt = 0;
        // A fresh user-initiated stream opens a new session: reset the
        // accumulated "minutes playing" (the source was just started).
        s_streamPlayedMs = 0;
        s_streamSession = true;
    }

    radioAudioTouchFavoriteByUrl(url, String());
    radioAudioSaveState();

    return true;
}

void radioAudioRuntimeBegin() {
    if (s_started) return;
    s_deferMtx = xSemaphoreCreateMutex();
    s_metaMtx = xSemaphoreCreateMutex();
    s_queue.reserve(256);
    s_favorites.reserve(64);
    randomSeed((uint32_t)micros());
    radioAudioLoadQueue();
    radioAudioLoadFavorites();
    radioAudioLoadState();
    // radioAudioEnsureLibraryDir(); // disabled: runs in loop when SD is ready
    radioScheduleBegin();
    upnpClientBegin();             // UPnP client — discovery starts in loop
    s_started = true;
}

// Deferred actions: called from async_tcp task, executed in loop task.
static void radioAudioDeferAction(RadioDeferredAction action, const String& param, int index = -1) {
    if (s_deferMtx) xSemaphoreTake(s_deferMtx, portMAX_DELAY);
    s_deferParam = param;
    s_deferIndex = index;
    s_deferAction = action;
    if (s_deferMtx) xSemaphoreGive(s_deferMtx);
}

void radioAudioStopDeferred() { radioAudioDeferAction(DEFER_STOP, ""); }
void radioAudioPlayUrlDeferred(const String& url) { s_playMimeHint = String(); radioAudioDeferAction(DEFER_PLAY_URL, url); }
// MIME-aware variant: lets a URL with no .mp3/.flac extension (e.g. a UPnP
// transcoded resource) pick the right decoder instead of guessing from the URL.
void radioAudioPlayUrlMimeDeferred(const String& url, const String& mime) { s_playMimeHint = mime; radioAudioDeferAction(DEFER_PLAY_URL, url); }
void radioAudioPlaySdDeferred(const String& path) { radioAudioDeferAction(DEFER_PLAY_SD, path); }
void radioAudioPlayNextDeferred() { radioAudioDeferAction(DEFER_NEXT, ""); }
void radioAudioPlayRandomDeferred() { radioAudioDeferAction(DEFER_RANDOM, ""); }
void radioAudioPlayFavoriteDeferred(int index) { radioAudioDeferAction(DEFER_PLAY_FAV, "", index); }
void radioAudioTogglePlayDeferred() { radioAudioDeferAction(DEFER_TOGGLE, ""); }
void radioAudioPlayEntryDeferred(const String& entry) { radioAudioDeferAction(DEFER_PLAY_ENTRY, entry); }

static void radioAudioProcessDeferred() {
    if (s_deferAction == DEFER_NONE) return;
    RadioDeferredAction act;
    String param;
    int index = -1;
    if (s_deferMtx) xSemaphoreTake(s_deferMtx, portMAX_DELAY);
    act = s_deferAction;
    param = s_deferParam;
    index = s_deferIndex;
    s_deferAction = DEFER_NONE;
    s_deferParam = "";
    s_deferIndex = -1;
    if (s_deferMtx) xSemaphoreGive(s_deferMtx);

    // Any deferred action is a manual (user) control command — release the
    // schedule ownership so it never kills or hijacks a manual session.
    radioScheduleClearOwner();

    String msg;
    switch (act) {
        case DEFER_STOP:     radioAudioStop(); break;
        case DEFER_PLAY_URL: if (!radioAudioPlayUrl(param, msg)) sendVirtualMessage(msg.length() ? msg : String(F("Play URL failed")), WARN); break;
        case DEFER_PLAY_SD:  if (!radioAudioPlaySdPath(param, msg)) sendVirtualMessage(msg.length() ? msg : String(F("Play SD failed")), WARN); break;
        case DEFER_NEXT:     if (!radioAudioPlayNext(msg)) sendVirtualMessage(msg.length() ? msg : String(F("Play next failed")), WARN); break;
        case DEFER_RANDOM:   if (!radioAudioPlayRandom(msg)) sendVirtualMessage(msg.length() ? msg : String(F("Play random failed")), WARN); break;
        case DEFER_PLAY_FAV: if (!radioAudioPlayFavorite(index, msg)) sendVirtualMessage(msg.length() ? msg : String(F("Play favorite failed")), WARN); break;
        case DEFER_TOGGLE:   if (!radioAudioTogglePlay(msg)) sendVirtualMessage(msg.length() ? msg : String(F("Toggle failed")), WARN); break;
        case DEFER_PLAY_ENTRY: if (!radioAudioPlayQueueEntry(param, msg, false)) sendVirtualMessage(msg.length() ? msg : String(F("Play entry failed")), WARN); break;
        default: break;
    }
}

void radioAudioRuntimeLoop() {
    // The library has no background scan: each listing walks the SD live.

    radioAudioProcessDeferred();
    radioScheduleLoop();
    upnpClientLoop();              // UPnP: SSDP → describe → ready (cooperative)
    radioAudioDrainIcyFromRing();  // publish ring-parsed StreamTitle to the UI

    // Persist any async-requested radio state, queue and favorites serially in
    // the loop task — never from the web handler task — so LittleFS writes do
    // not collide with ConfigManager saves. Debounced and per-file: a burst of
    // async requests (e.g. volume slider drag) coalesces into a single small
    // write, and only the files that actually changed are rewritten.
    if (s_persistDirty && (int32_t)(millis() - s_persistAtMs) >= 0) {
        uint8_t dirty = s_persistDirty;
        s_persistDirty = 0;
        if (dirty & 0x01) radioAudioSaveState();
        if (dirty & 0x02) radioAudioSaveQueue();
        if (dirty & 0x04) radioAudioSaveFavorites();
    }

    // Create the SD folders as soon as the card is available (SD may mount after
    // radioAudioRuntimeBegin). Retry for the first ~2 minutes after boot.
    static uint8_t s_dirRetries = 0;
    if (s_dirRetries < 8 && radioAudioSdMounted()) {
        bool dirsReady = false;
        if (sdMutexLock(pdMS_TO_TICKS(2000))) {
            dirsReady = SD_MMC.exists(kLibraryDir) && SD_MMC.exists(kPlaylistDir);
            sdMutexUnlock();
        }
        if (dirsReady) {
            s_dirRetries = 8; // done
        } else {
            radioAudioEnsureLibraryDir();
            radioAudioEnsurePlaylistDir();
            s_dirRetries++;
        }
    }

    if (s_resumePending && !s_active && (int32_t)(millis() - s_resumeAtMs) >= 0) {
        String msg;
        bool ok = false;
        if (s_resumeFirst && !s_queue.empty()) {
            // Resume the FIRST item of the playlist/queue (not what was playing).
            ok = radioAudioPlayQueueEntry(s_queue[0], msg, false);
            if (ok) s_queueIndex = 0;
        } else if (s_resumeMode == "stream") {
            ok = radioAudioPlayUrlInternal(s_resumeSource, msg, false);
        } else if (s_resumeMode == "sd") {
            ok = radioAudioPlaySdInternal(s_resumeSource, msg, false);
        }

        if (ok) {
            s_resumePending = false;
            s_lastError = F("Playback restored after reboot");
        } else {
            s_resumeTries++;
            if (s_resumeTries >= 8) {
                s_resumePending = false;
                s_lastError = msg.length() ? msg : String(F("Auto-resume failed"));
            } else {
                s_resumeAtMs = millis() + 5000;
            }
        }
    }

    if (s_pendingReconnect && !s_active && s_lastStreamUrl.length()) {
        if ((int32_t)(millis() - s_reconnectAtMs) >= 0) {
            String msg;
            if (!radioAudioPlayUrlInternal(s_lastStreamUrl, msg, true)) {
                radioAudioScheduleReconnect(msg.length() ? msg : String(F("Reconnect failed")));
            }
        }
    }

    if (s_pendingUnmute && s_output && (int32_t)(millis() - s_unmuteAtMs) >= 0) {
        s_output->SetGain((float)s_volume / 100.0f);
        s_pendingUnmute = false;
    }

    // Sleep timer: when it expires, stop the audio and clear the timer.
    if (s_sleepEndEpoch > 0) {
        int rem = radioAudioSleepRemainingSec();
        if (rem == 0) {
            s_sleepEndEpoch = 0;
            s_sleepDurationMin = 0;
            s_sleepSetUptimeMs = 0;
            radioAudioSaveState();
            radioAudioStop();
            sendVirtualMessage(F("Sleep timer ended - radio off"), INFO);
        }
        // rem < 0: clock not synced after reboot yet — wait before evaluating.
    }

    if (!s_active || !s_decoder) return;

    if (s_stream && s_streamProfile > 1 && (millis() - s_profileStepMs) > 300000UL) {
        s_streamProfile--;
        s_profileStepMs = millis();
    }

    if (!s_decoder->isRunning()) {
        bool shouldReconnect = s_stream && !s_manualStop && s_lastStreamUrl.length();
        bool shouldQueueNext = !s_stream && !s_manualStop && !s_queue.empty();
        radioAudioCleanupSource();
        if (shouldReconnect) {
            radioAudioScheduleReconnect(F("Stream stopped, reconnecting"));
        } else if (shouldQueueNext) {
            String nextMsg;
            if (!radioAudioPlayNext(nextMsg)) s_lastError = nextMsg;
        }
        return;
    }

    if (!s_decoder->loop()) {
        bool shouldReconnect = s_stream && !s_manualStop && s_lastStreamUrl.length();
        bool shouldQueueNext = !s_stream && !s_manualStop && !s_queue.empty();
        s_lastError = F("Playback loop ended");
        radioAudioCleanupSource();
        if (shouldReconnect) {
            radioAudioScheduleReconnect(s_lastError);
        } else if (shouldQueueNext) {
            String nextMsg;
            if (!radioAudioPlayNext(nextMsg)) s_lastError = nextMsg;
        }
    }
}

bool radioAudioPlayUrl(const String& url, String& messageOut) {
    return radioAudioPlayUrlInternal(url, messageOut, false);
}

bool radioAudioPlaySdPath(const String& path, String& messageOut) {
    return radioAudioPlaySdInternal(path, messageOut, true);
}

// Play a single queue entry, routing HTTP(S) entries to the stream player and
// sd:/ entries to the SD player so mixed playlists keep working.
static bool radioAudioPlayQueueEntry(const String& entry, String& messageOut, bool updateQueue) {
    if (radioAudioIsStreamEntry(entry)) {
        return radioAudioPlayUrlInternal(entry, messageOut, false);
    }
    return radioAudioPlaySdInternal(entry, messageOut, updateQueue);
}

bool radioAudioPlayNext(String& messageOut) {
    if (s_queue.empty()) {
        messageOut = F("Queue is empty");
        return false;
    }

    if (s_repeatMode == 1) {
        // Repeat one - replay current track
        if (s_queueIndex >= 0 && (size_t)s_queueIndex < s_queue.size()) {
            return radioAudioPlayQueueEntry(s_queue[(size_t)s_queueIndex], messageOut, false);
        }
        s_queueIndex = 0;
        radioAudioSaveQueue();
        return radioAudioPlayQueueEntry(s_queue[0], messageOut, false);
    }

    if (s_shuffle) {
        s_queueIndex = random((int)s_queue.size());
    } else {
        if (s_queueIndex < 0 || s_queueIndex >= (int)s_queue.size()) {
            s_queueIndex = 0;
        } else {
            s_queueIndex++;
            if (s_queueIndex >= (int)s_queue.size()) {
                if (s_repeatMode == 2) {
                    s_queueIndex = 0;
                } else {
                    messageOut = F("End of queue");
                    return false;
                }
            }
        }
    }

    radioAudioSaveQueue();
    return radioAudioPlayQueueEntry(s_queue[(size_t)s_queueIndex], messageOut, false);
}

bool radioAudioPlayRandom(String& messageOut) {
    if (s_queue.empty()) {
        messageOut = F("Queue is empty");
        return false;
    }

    s_queueIndex = random((int)s_queue.size());
    if (s_queueIndex < 0 || s_queueIndex >= (int)s_queue.size()) {
        s_queueIndex = 0;
    }

    radioAudioSaveQueue();
    return radioAudioPlayQueueEntry(s_queue[(size_t)s_queueIndex], messageOut, false);
}

void radioAudioStop() {
    s_manualStop = true;
    s_pendingReconnect = false;
    s_reconnectAttempt = 0;
    s_stationFavicon = "";
    radioAudioCleanupSource();
    radioAudioSaveState();
}

bool radioAudioSeekPosition(float fraction) {
    if (s_stream || !s_active || s_lastSdPath.isEmpty()) return false;
    if (fraction < 0.0f || fraction > 1.0f) return false;

    // Save path before stopping (stop clears it indirectly via cleanup).
    String path = s_lastSdPath;
    if (!sdMutexLockFast(pdMS_TO_TICKS(8000))) return false;
    if (!SD_MMC.exists(path.c_str())) { sdMutexUnlockFast(); return false; }

    // Compute target byte
    File f = SD_MMC.open(path.c_str(), FILE_READ);
    if (!f) { sdMutexUnlockFast(); return false; }
    uint32_t size = (uint32_t)f.size();
    f.close();
    sdMutexUnlockFast();
    if (size == 0) return false;
    uint32_t target = (uint32_t)(fraction * (float)size);
    if (target >= size) target = size - 1;

    // Stop the current playback
    s_manualStop = true;
    radioAudioStop();

    // Restart at the target byte – pre-seek the source before wrapping
    // in the buffer so the decoder sees audio from the new position.
    AudioSourceSdRing* src = new AudioSourceSdRing(path.c_str(), kSdRingBytes);
    if (!src || !src->isOpen()) {
        if (src) delete src;
        return false;
    }
    src->seek((int32_t)target, SEEK_SET);

    s_manualStop = false;
    s_pendingReconnect = false;
    s_reconnectAttempt = 0;
    s_stationFavicon = "";
    s_lastSdPath = path;

    String display = String(F("sd:")) + path;
    String msg;
    s_decoderOverride = -1;
    s_streamSession = false; // local file seek: per-track elapsed
    bool ok = radioAudioStartWithSource(src,
                                        kSdRingBytes,
                                        kSdCacheMinBytes,
                                        kSdPrebufferMs,
                                        false,
                                        display,
                                        msg,
                                        true);   // sourceSelfBuffered: ring IS the buffer
    if (!ok) return false;
    radioAudioSaveState();
    return true;
}

void radioAudioCancelResume() {
    s_resumePending = false;
    s_resumeTries = 0;
    s_resumeMode = "";
    s_resumeSource = "";
    s_resumeWasActive = false;
    radioAudioSaveState();
}

// Toggle play/pause of what is selected: stop if playing, otherwise resume the
// last stream/SD source, falling back to the first queue entry.
bool radioAudioTogglePlay(String& messageOut) {
    if (s_active) {
        radioAudioStop();
        messageOut = F("Stopped");
        return true;
    }
    if (s_lastStreamUrl.length()) return radioAudioPlayUrl(s_lastStreamUrl, messageOut);
    if (s_lastSdPath.length()) return radioAudioPlaySdPath(s_lastSdPath, messageOut);
    if (!s_queue.empty()) return radioAudioPlayQueueEntry(s_queue[0], messageOut, true);
    messageOut = F("Nothing to play");
    return false;
}

void radioAudioSetVolume(uint8_t value0to100) {
    if (value0to100 > 100) value0to100 = 100;
    s_volume = value0to100;
    if (s_output) {
        s_output->SetGain((float)s_volume / 100.0f);
    }
    radioAudioRequestPersistState();
}

bool radioAudioSetQueue(const std::vector<String>& items, int startIndex, String& messageOut) {
    s_queue.clear();

    for (size_t i = 0; i < items.size(); i++) {
        String p = items[i];
        if (!p.length()) continue;
        s_queue.push_back(p);
        if (s_queue.size() >= 256) break;
    }

    if (s_queue.empty()) {
        s_queueIndex = -1;
        radioAudioRequestPersist();
        messageOut = F("Queue cleared");
        return true;
    }

    if (startIndex < 0 || startIndex >= (int)s_queue.size()) startIndex = 0;
    s_queueIndex = startIndex;
    radioAudioRequestPersist();

    messageOut = F("Queue updated");
    return true;
}

String radioAudioQueueJson() {
    // Keep payload bounded to avoid watchdog resets on large default queues.
    const size_t maxItemsForApi = 120;
    const size_t count = s_queue.size() < maxItemsForApi ? s_queue.size() : maxItemsForApi;

    String out;
    out.reserve(256 + (count * 96));
    out = F("{\"success\":true,\"message\":\"");
    if (s_queue.size() > maxItemsForApi) {
        out += F("Queue status (partial)");
    } else {
        out += F("Queue status");
    }
    out += F("\",\"index\":");
    out += String(s_queueIndex);
    out += F(",\"items\":[");

    for (size_t i = 0; i < count; i++) {
        if (i > 0) out += ',';
        out += '"';
        const String& item = s_queue[i];
        for (size_t j = 0; j < item.length(); j++) {
            char c = item[j];
            if (c == '\\' || c == '"') out += '\\';
            out += c;
        }
        out += '"';
    }

    out += F("]}");
    return out;
}

bool radioAudioAddFavorite(const String& name, const String& url, String& messageOut, const String& favicon) {
    bool isSdPath = url.startsWith("/") || url.startsWith("sd:");
    if (!(url.startsWith("http://") || url.startsWith("https://") || isSdPath)) {
        messageOut = F("Favorite must be an http(s) URL or SD path");
        return false;
    }
    if (!radioAudioIsSupportedStream(url)) {
        messageOut = F("Unsupported audio format - cannot be saved or played");
        return false;
    }
    if (url.endsWith(".flac") && isSdPath) {
        String path = url;
        if (path.startsWith("sd:")) path = path.substring(3);
        if (!radioAudioIsSafeFlacFile(path.c_str())) {
            messageOut = F("Unsupported high-res FLAC (only 44.1/48 kHz 16-bit)");
            return false;
        }
    }

    for (size_t i = 0; i < s_favorites.size(); i++) {
        if (s_favorites[i].url == url) {
            RadioFavorite fav = s_favorites[i];
            fav.name = name.length() ? name : fav.name;
            if (favicon.length()) fav.favicon = favicon;
            s_favorites.erase(s_favorites.begin() + (int)i);
            s_favorites.insert(s_favorites.begin(), fav);
            radioAudioRequestPersist();
            messageOut = F("Favorite updated");
            return true;
        }
    }

    RadioFavorite fav;
    fav.name = name.length() ? name : url;
    fav.url = url;
    fav.favicon = favicon;
    s_favorites.insert(s_favorites.begin(), fav);
    if (s_favorites.size() > 64) {
        s_favorites.pop_back();
    }
    radioAudioRequestPersist();
    messageOut = F("Favorite added");
    return true;
}

bool radioAudioRemoveFavorite(int index, String& messageOut) {
    if (index < 0 || index >= (int)s_favorites.size()) {
        messageOut = F("Invalid favorite index");
        return false;
    }
    s_favorites.erase(s_favorites.begin() + index);
    radioAudioRequestPersist();
    messageOut = F("Favorite removed");
    return true;
}

bool radioAudioPlayFavorite(int index, String& messageOut) {
    if (index < 0 || index >= (int)s_favorites.size()) {
        messageOut = F("Invalid favorite index");
        return false;
    }
    RadioFavorite fav = s_favorites[(size_t)index];
    s_stationFavicon = fav.favicon;
    s_stationName = fav.name;
    bool isSdPath = fav.url.startsWith("/") || fav.url.startsWith("sd:");
    bool ok;
    if (isSdPath) {
        String sdPath = fav.url;
        if (sdPath.startsWith("sd:")) sdPath = sdPath.substring(3);
        ok = radioAudioPlaySdInternal(sdPath, messageOut, false);
    } else {
        ok = radioAudioPlayUrlInternal(fav.url, messageOut, false);
    }
    if (ok) {
        radioAudioTouchFavoriteByUrl(fav.url, fav.name);
    }
    return ok;
}

String radioAudioFavoritesJson() {
    SpiRamJsonDocument doc(8192);
    doc["success"] = true;
    doc["message"] = F("Favorites status");
    JsonArray arr = doc.createNestedArray("items");
    for (size_t i = 0; i < s_favorites.size(); i++) {
        JsonObject o = arr.createNestedObject();
        o["name"] = s_favorites[i].name;
        o["url"] = s_favorites[i].url;
        o["favicon"] = s_favorites[i].favicon;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

bool radioAudioIsActive() {
    return s_active;
}

const char* radioAudioGetCurrentTitle() {
    radioAudioExtractMetadata();
    return s_metaTitle;
}

const char* radioAudioGetCurrentArtist() {
    radioAudioExtractMetadata();
    return s_metaArtist;
}

const char* radioAudioGetCurrentArtwork() {
    radioAudioExtractMetadata();
    return s_metaArtwork;
}

// Derive a best-effort station favicon when the player was started without
// one (queue entry, manual URL, old favorite, resume): https://<host>/favicon.ico.
// Served to the UI as station_favicon only while a stream is active, so a
// local SD file never picks up a bogus icon.
static String radioAudioDeriveFavicon() {
    if (!s_stream || !s_lastStreamUrl.length()) return String();
    char buf[192];
    if (radio_favicon::deriveForStreamUrl(s_lastStreamUrl.c_str(), buf, sizeof(buf)) == 0) {
        return String();
    }
    return String(buf);
}

String radioAudioStatusJson() {
    SpiRamJsonDocument doc(768);
    doc["success"] = true;
    doc["message"] = s_active ? "Playback active" : "Playback idle";
    doc["active"] = s_active;
    doc["stream"] = s_stream;
    doc["volume"] = s_volume;
    doc["current"] = s_current;
    doc["cache_bytes"] = s_cacheBytesInUse;
    doc["error"] = s_lastError;
    doc["reconnect_pending"] = s_pendingReconnect;
    doc["reconnect_attempt"] = s_reconnectAttempt;
    doc["stream_profile"] = s_streamProfile;
    doc["queue_count"] = (uint32_t)s_queue.size();
    doc["queue_index"] = s_queueIndex;
    doc["favorites_count"] = (uint32_t)s_favorites.size();
    doc["resume_pending"] = s_resumePending;
    doc["shuffle"] = s_shuffle;
    doc["repeat"] = s_repeatMode;
    doc["sleep_active"] = (s_sleepEndEpoch > 0);
    doc["sleep_min"] = (int)s_sleepDurationMin;
    doc["sleep_remaining"] = radioAudioSleepRemainingSec();
    doc["eq_bands"] = kEqBands;
    JsonArray eqArr = doc.createNestedArray("eq_gains");
    for (uint8_t i = 0; i < kEqBands; i++) eqArr.add(s_eqGains[i]);
#if ENABLE_WIFI==1
    doc["wifi_rssi"] = (WiFi.status() == WL_CONNECTED) ? (int32_t)WiFi.RSSI() : 0;
#else
    doc["wifi_rssi"] = 0;
#endif
    radioAudioExtractMetadata();
    doc["title"] = s_metaTitle;
    doc["artist"] = s_metaArtist;
    doc["artwork"] = s_metaArtwork;
    doc["station_favicon"] = s_stationFavicon.length() ? s_stationFavicon
                                                        : radioAudioDeriveFavicon();
    doc["station"] = s_stationName;
    {
        uint32_t segMs = (s_active && s_streamStartMs) ? (millis() - s_streamStartMs) : 0;
        // Streaming: report accumulated played time so the "minutes playing"
        // survives reconnects. Local SD files keep the per-track elapsed.
        doc["active_ms"] = s_active ? (s_streamSession ? (s_streamPlayedMs + segMs) : segMs) : 0;
    }
    // Progress bar for SD playback: byte position / total size. Streams
    // have no total size, so the bar only fills for local files.
    if (!s_stream && s_buffered) {
        doc["pos_byte"]  = s_buffered->getPos();
        uint32_t sz = s_buffered->getSize();
        if (sz > 0) doc["size_byte"] = sz;
        if (s_active && s_lastSdPath.length()) {
            uint32_t dur = radioAudioProbeDurationCached(s_lastSdPath.c_str(), sz);
            if (dur > 0) doc["duration_ms"] = dur;
        }
    }
    doc["library_available"] = radioAudioSdMounted() && psramFound();
    doc["sd_mounted"]        = sdcardIsMounted();
    doc["sd_fallback"]       = sdcardInFallbackMode();
    if (s_pendingReconnect) {
        uint32_t now = millis();
        doc["next_reconnect_ms"] = (s_reconnectAtMs > now) ? (s_reconnectAtMs - now) : 0;
    } else {
        doc["next_reconnect_ms"] = 0;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

// ============================================================
// Cover art
// ============================================================
void radioAudioClearArtwork() {
    if (s_artworkData) {
        heap_caps_free(s_artworkData);
        s_artworkData = nullptr;
    }
    s_artworkSize = 0;
    s_artworkMime[0] = '\0';
}

void radioAudioSetStationFavicon(const char* url) {
    s_stationFavicon = url ? url : "";
}

void radioAudioSetStationName(const char* name) {
    s_stationName = (name && name[0]) ? name : "";
}

const char* radioAudioGetStationFavicon() {
    return s_stationFavicon.c_str();
}

const char* radioAudioGetArtworkMime() {
    return s_artworkMime;
}

static void radioAudioParseId3Apic(const uint8_t* data, uint32_t dataLen) {
    radioAudioClearArtwork();
    // Scan for APIC frames in ID3v2 tags
    // data starts at the first ID3v2 frame
    uint32_t pos = 0;
    while (pos + 10 <= dataLen) {
        // Frame header: 4 bytes ID, 4 bytes size (big-endian), 2 bytes flags
        char frameId[5] = {0};
        memcpy(frameId, data + pos, 4);
        uint32_t frameSize = ((uint32_t)data[pos+4] << 24)
                           | ((uint32_t)data[pos+5] << 16)
                           | ((uint32_t)data[pos+6] << 8)
                           | ((uint32_t)data[pos+7]);
        if (frameSize == 0) break;
        pos += 10; // skip frame header
        if (pos + frameSize > dataLen) break;

        if (strcmp(frameId, "APIC") == 0) {
            // APIC: encoding(1) + MIME(null-term) + picType(1) + desc(null-term) + imageData
            uint32_t off = 0;
            uint8_t enc = data[pos];
            off++;
            // Read MIME type (null-terminated)
            const char* mimeStart = (const char*)(data + pos + off);
            uint32_t mimeLen = 0;
            while (off + mimeLen < frameSize && data[pos + off + mimeLen] != 0) mimeLen++;
            if (mimeLen == 0 || mimeLen > 30) break;
            memcpy(s_artworkMime, mimeStart, mimeLen);
            s_artworkMime[mimeLen] = '\0';
            off += mimeLen + 1; // skip null term
            off++; // skip picture type byte
            // Skip description (null-terminated, encoding dependent)
            if (enc == 0 || enc == 3) {
                // ISO-8859-1 or UTF-8 - 1 byte null term
                while (off < frameSize && data[pos + off] != 0) off++;
                off++;
            } else {
                // UTF-16 with BOM - 2 byte null term
                while (off + 1 < frameSize && !(data[pos + off] == 0 && data[pos + off + 1] == 0)) off++;
                off += 2;
            }
            // Remaining data is the image
            uint32_t imgSize = frameSize - off;
            if (imgSize > 0 && imgSize < (512 * 1024)) {
                s_artworkData = (uint8_t*)heap_caps_calloc(1, imgSize, MALLOC_CAP_SPIRAM);
                if (!s_artworkData) s_artworkData = (uint8_t*)heap_caps_calloc(1, imgSize, MALLOC_CAP_8BIT);
                if (s_artworkData) {
                    memcpy(s_artworkData, data + pos + off, imgSize);
                    s_artworkSize = imgSize;
                }
            }
            return; // found APIC, done
        }
        pos += frameSize;
    }
}

static void radioAudioExtractCoverArt() {
    if (!s_active || !s_decoder || s_stream) return;
    if (s_artworkData) return; // already have it

    // The real file path lives in s_lastSdPath; s_current is only the display
    // string ("sd:/...") and must never be handed to SD_MMC.open().
    const String& path = s_lastSdPath;
    if (!path.length()) return;

    // Serialised on the SD mutex (web-handler task, polled during playback):
    // must never run concurrently with the audio loop reading the same file.
    if (!sdMutexLock(pdMS_TO_TICKS(8000))) return;

    // 1) Embedded APIC in the playing file (best quality, no extra IO).
    File file = SD_MMC.open(path.c_str(), FILE_READ);
    if (file) {
        uint8_t header[10];
        if (file.read(header, 10) == 10) {
            if (header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
                uint32_t tagSize = ((uint32_t)(header[6] & 0x7F) << 21)
                                 | ((uint32_t)(header[7] & 0x7F) << 14)
                                 | ((uint32_t)(header[8] & 0x7F) << 7)
                                 | ((uint32_t)(header[9] & 0x7F));
                if (tagSize > 0 && tagSize <= (512 * 1024)) {
                    uint8_t* tagBuf = (uint8_t*)heap_caps_calloc(1, tagSize, MALLOC_CAP_SPIRAM);
                    if (!tagBuf) tagBuf = (uint8_t*)heap_caps_calloc(1, tagSize, MALLOC_CAP_8BIT);
                    if (tagBuf) {
                        if (file.read(tagBuf, tagSize) == tagSize) {
                            uint32_t tagOff = 0;
                            if (header[5] & 0x40) { // extended header present
                                uint32_t ext = ((uint32_t)(tagBuf[0] & 0x7F) << 21)
                                             | ((uint32_t)(tagBuf[1] & 0x7F) << 14)
                                             | ((uint32_t)(tagBuf[2] & 0x7F) << 7)
                                             | (uint32_t)(tagBuf[3] & 0x7F);
                                tagOff = 4 + ext;
                                if (tagOff > tagSize) tagOff = tagSize;
                            }
                            radioAudioParseId3Apic(tagBuf + tagOff, tagSize - tagOff);
                        }
                        heap_caps_free(tagBuf);
                    }
                }
            }
        }
        file.close();
    }
    sdMutexUnlock();
    // Only embedded art is used — no cover.jpg/folder.jpg scraping.
}

const uint8_t* radioAudioGetArtworkData(uint32_t& outSize) {
    // Try to extract if not yet loaded (for SD files). Attempt only once per
    // source so tracks without any art don't re-scan the SD on every 5s poll.
    if (!s_artworkData && !s_stream && s_active) {
        static String s_lastArtAttempt;
        if (s_lastArtAttempt != s_current) {
            s_lastArtAttempt = s_current;
            radioAudioExtractCoverArt();
        }
    }
    outSize = s_artworkSize;
    return s_artworkData;
}

// ============================================================
// Search (recursive SD scan)
// ============================================================
static bool s_searchOpen = false;
static String s_searchQuery;
static File s_searchDir;

static bool radioAudioSearchOpen(const char* query) {
    s_searchOpen = false;
    s_searchQuery = String(query);
    s_searchDir = SD_MMC.open("/");
    if (!s_searchDir) return false;
    s_searchOpen = true;
    return true;
}

// Recursive SD scan - finds matching names in root + subfolders (library etc.)
static void radioAudioSearchWalk(File& dir, const String& q, JsonArray& arr, uint32_t& count, uint32_t max) {
    File entry = dir.openNextFile();
    while (entry && count < max) {
        if (entry.isDirectory()) {
            File sub = entry;
            radioAudioSearchWalk(sub, q, arr, count, max);
        } else {
            String name = String(entry.name());
            String lower = name;
            lower.toLowerCase();
            if (lower.indexOf(q) >= 0) {
                JsonObject o = arr.createNestedObject();
                o["path"] = String(entry.path());
                o["name"] = name;
                count++;
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
}

String radioAudioSearch(const char* query) {
    if (!query || strlen(query) == 0) {
        return String(F("{\"success\":false,\"message\":\"Empty query\"}"));
    }

    String q = String(query);
    q.toLowerCase();

    SpiRamJsonDocument doc(16384);
    doc["success"] = true;
    JsonArray arr = doc.createNestedArray("results");
    uint32_t count = 0;

    // Serialised on the shared SD mutex (web handler vs audio loop)
    extern bool sdMutexLock(TickType_t timeoutTicks);
    extern void sdMutexUnlock();
    if (sdMutexLock(pdMS_TO_TICKS(8000))) {
        File root = SD_MMC.open("/");
        if (root && root.isDirectory()) {
            radioAudioSearchWalk(root, q, arr, count, 50);
            root.close();
        }
        sdMutexUnlock();
    }

    String out;
    serializeJson(doc, out);
    return out;
}

// ============================================================
// Library - music folder (auto-created on SD)
// ============================================================
static bool radioAudioIsMusicFile(const String& name) {
    String l = name;
    l.toLowerCase();
    // Only formats the bundled decoders can actually play:
    //   .mp3  -> Minimp3Decoder (minimp3, CC0)
    //   .flac -> FlacDecoder (dr_flac; 44.1/48 kHz 16-bit; high-res is hidden)
    //   .aac/.m4a/.m4b -> AacDecoder (esp_audio_codec, Espressif Modified MIT)
    //   .wav  -> WavDecoder (first-party, 16-bit PCM)
    //   .ogg  -> OggVorbisDecoder (stb_vorbis, Unlicense; T16)
    //   .opus -> OggOpusDecoder (libopus core via esp_audio_codec; T16)
    return l.endsWith(".mp3") || l.endsWith(".flac") ||
           l.endsWith(".aac") || l.endsWith(".m4a") || l.endsWith(".m4b") ||
           l.endsWith(".wav") || l.endsWith(".ogg") || l.endsWith(".opus");
}

// ===== ICY metadata reader (song title + artist from Shoutcast/Icecast streams) =====
// The ring source (AudioSourceHttpRing) requests Icy-MetaData:1 on the MAIN
// stream, strips the interleaved metadata blocks inline and publishes each
// StreamTitle via readIcyMetadata(). radioAudioDrainIcyFromRing() (radio loop
// task) moves those into the shared s_metaTitle/s_metaArtist for the web and
// screen handlers. A single connection — no extra sockets, immune to
// per-IP client limits and https-only stations that reject a second fetch.
static void radioAudioParseIcyMetadata(const String& meta, int metaLen) {
    if (metaLen <= 0) return;
    String title = meta;
    int st = meta.indexOf("StreamTitle='");
    if (st >= 0) {
        st += 13;
        int end = meta.indexOf("';", st);
        if (end <= st) return;
        title = meta.substring(st, end);
    }
    title.trim();
    if (!title.length()) return;
    if (s_metaMtx) xSemaphoreTake(s_metaMtx, portMAX_DELAY);
    int dash = title.indexOf(" - ");
    if (dash > 0) {
        safeCopy(s_metaArtist, sizeof(s_metaArtist), title.substring(0, dash).c_str());
        safeCopy(s_metaTitle, sizeof(s_metaTitle), title.substring(dash + 3).c_str());
    } else {
        safeCopy(s_metaTitle, sizeof(s_metaTitle), title.c_str());
    }
    if (s_metaMtx) xSemaphoreGive(s_metaMtx);
    s_metaIcyActive = true;
}

static void radioAudioDrainIcyFromRing() {
    if (!s_stream || !s_active || !s_source) return;
    char raw[256];
    if (s_source->readIcyMetadata(raw, sizeof(raw)) && raw[0]) {
        radioAudioParseIcyMetadata(String(raw), (int)strlen(raw));
    }
}

static bool radioAudioEnsureLibraryDir() {
    if (!radioAudioSdMounted()) return false;
    // Serialised on the shared SD mutex.
    extern bool sdMutexLock(TickType_t timeoutTicks);
    extern void sdMutexUnlock();
    if (!sdMutexLock(pdMS_TO_TICKS(1000))) return false;
    bool ok = SD_MMC.exists(kLibraryDir) || SD_MMC.mkdir(kLibraryDir);
    sdMutexUnlock();
    return ok;
}

static bool radioAudioSdMounted() { return sdcardIsMounted(); }

// ---- ID3v2 tag reader (title/artist/album/cover flag) ----
static uint32_t radioAudioId3Syncsafe(const uint8_t* b) {
    return ((uint32_t)(b[0] & 0x7F) << 21) | ((uint32_t)(b[1] & 0x7F) << 14) |
           ((uint32_t)(b[2] & 0x7F) << 7) | (uint32_t)(b[3] & 0x7F);
}

// Extract a text frame payload. enc: 0 ISO-8859-1, 1 UTF-16, 2 UTF-16BE, 3 UTF-8.
static String radioAudioId3Text(const uint8_t* data, uint32_t len, uint8_t enc) {
    if (enc == 0 || enc == 3) {
        String s;
        for (uint32_t i = 0; i < len && data[i]; i++) s += (char)data[i];
        return s;
    }
    uint32_t i = 0;
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xFE) i = 2;       // UTF-16 LE BOM
    else if (len >= 2 && data[0] == 0xFE && data[1] == 0xFF) i = 2;   // UTF-16 BE BOM
    String s;
    for (; i + 1 < len; i += 2) {
        char c = (enc == 1) ? (char)data[i] : (char)data[i + 1];
        if (c == 0) break;
        s += c;
    }
    return s;
}

// Reads ID3v2 tags of an SD file into title/artist/album and sets hasArt when an
// APIC (cover) frame is present. Metadata ALWAYS wins over the filename.
static void radioAudioReadId3Tags(const char* path, String& title, String& artist,
                                  String& album, bool& hasArt) {
    title = ""; artist = ""; album = ""; hasArt = false;
    if (!radioAudioSdMounted() || !path || !path[0]) return;
    // Web handler task: serialised with playback reads so a concurrent
    // metadata poll can never interleave with the audio loop's file access.
    if (!sdMutexLockFast(pdMS_TO_TICKS(8000))) return;
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) { sdMutexUnlockFast(); return; }
    uint8_t hdr[10];
    if (f.read(hdr, 10) != 10) { f.close(); sdMutexUnlockFast(); return; }
    if (hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') { f.close(); sdMutexUnlockFast(); return; }
    uint32_t tagSize = radioAudioId3Syncsafe(hdr + 6);
    if (tagSize == 0 || tagSize > 128 * 1024) { f.close(); sdMutexUnlockFast(); return; }
    uint8_t* tag = (uint8_t*)heap_caps_malloc(tagSize, MALLOC_CAP_SPIRAM);
    if (!tag) { f.close(); sdMutexUnlockFast(); return; }
    if (f.read(tag, tagSize) != tagSize) { heap_caps_free(tag); f.close(); sdMutexUnlockFast(); return; }
    f.close();
    sdMutexUnlockFast();

    uint32_t pos = 0;
    if (hdr[5] & 0x40) { // extended header present
        uint32_t ext = radioAudioId3Syncsafe(tag);
        pos = 4 + ext;
        if (pos > tagSize) pos = tagSize;
    }
    while (pos + 10 <= tagSize) {
        char fid[5] = {0};
        memcpy(fid, tag + pos, 4);
        uint32_t fsz = ((uint32_t)tag[pos + 4] << 24) | ((uint32_t)tag[pos + 5] << 16) |
                       ((uint32_t)tag[pos + 6] << 8) | (uint32_t)tag[pos + 7];
        pos += 10;
        if (pos + fsz > tagSize) break;
        if (fid[0] == 'T' && fsz > 1) {
            String val = radioAudioId3Text(tag + pos + 1, fsz - 1, tag[pos]);
            if (val.length()) {
                if (strcmp(fid, "TIT2") == 0 && !title.length()) title = val;
                else if (strcmp(fid, "TPE1") == 0 && !artist.length()) artist = val;
                else if (strcmp(fid, "TALB") == 0 && !album.length()) album = val;
            }
        } else if (strcmp(fid, "APIC") == 0) {
            hasArt = true;
        }
        pos += fsz;
    }
    heap_caps_free(tag);
}

// ============================================================

// Recursive walk of /library (and sub-folders) adding playable music files.
static void radioAudioScanLibraryDir(File& dir, JsonArray arr, bool& skippedFlac, int depth) {
    if (depth > 5) return; // guard against very deep trees
    File entry = dir.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            radioAudioScanLibraryDir(entry, arr, skippedFlac, depth + 1);
        } else {
            String name = String(entry.name());
            String lower = name;
            lower.toLowerCase();
            if (radioAudioIsMusicFile(name)) {
                if (lower.endsWith(".flac") && !radioAudioIsSafeFlacFile(entry.path())) {
                    skippedFlac = true;   // high-res FLAC -> not shown
                } else {
                    // Cheap listing: directory walk only, no file opens. Tags are
                    // read once per track on PLAYBACK (radioAudioExtractMetadata),
                    // so queue building never costs ESP32 SD/CPU time.
                    JsonObject o = arr.createNestedObject();
                    o["path"] = String(entry.path());
                    o["name"] = name;
                }
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
}

// ============================================================
// Library folder covers (cover.jpg / folder.jpg / ... per folder)
// ============================================================

// Recognized cover-art filenames (lowercase). The library listing only opens
// each sub-folder's first entries to find one of these, so browsing stays
// cheap even on large trees — no per-track tag reads in list views.
static const char* const kCoverFileNames[] = {
    "cover.jpg",  "folder.jpg",  "front.jpg",  "album.jpg",  "albumart.jpg",
    "cover.jpeg", "folder.jpeg", "front.jpeg", "album.jpeg", "albumart.jpeg",
    "cover.png",  "folder.png",  "front.png",  "album.png",  "albumart.png",
    "albumartsmall.jpg", "albumartsmall.png"
};
static const size_t kCoverFileCount = sizeof(kCoverFileNames) / sizeof(kCoverFileNames[0]);

// Look for a known cover file among the first entries of an OPEN sub-folder
// (the parent walk already holds the SD mutex). Returns true and sets
// coverNameOut to the found filename.
static bool radioAudioFindFolderCoverInside(File& dir, String& coverNameOut) {
    if (!dir || !dir.isDirectory()) return false;
    int scanned = 0;
    File fe = dir.openNextFile();
    while (fe && scanned < 24) {
        if (!fe.isDirectory()) {
            String lower = String(fe.name());
            lower.toLowerCase();
            for (size_t i = 0; i < kCoverFileCount; i++) {
                if (lower == kCoverFileNames[i]) {
                    coverNameOut = String(fe.name());
                    fe.close();
                    return true;
                }
            }
        }
        scanned++;
        fe.close();
        fe = dir.openNextFile();
    }
    return false;
}

// Upper bound for a served folder-cover file (JPEG/PNG from SD).
static const uint32_t kMaxLibraryArtBytes = 1024 * 1024;

// Loads a cover image file (must live under /library) into a PSRAM-backed
// buffer that the caller owns and must release with heap_caps_free(). MIME is
// written into mimeOut (mimeCap bytes) and must stay in scope while the
// response is sent (the HttpResponse copies it). Only allowlisted image
// extensions, no "..", and a size cap keep this from becoming a raw file
// reader. Serialized on the SD mutex like the rest of the library API.
bool radioAudioLoadLibraryArt(const String& absPath, const uint8_t*& dataOut,
                              uint32_t& sizeOut, char* mimeOut, size_t mimeCap) {
    dataOut = nullptr;
    sizeOut = 0;
    if (mimeCap) mimeOut[0] = '\0';
    if (absPath.length() == 0) return false;

    if (absPath != kLibraryDir && absPath.indexOf(String(kLibraryDir) + "/") != 0) return false;
    if (absPath.indexOf("..") >= 0) return false;

    String lower = absPath;
    lower.toLowerCase();
    const char* mime = nullptr;
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg"))       mime = "image/jpeg";
    else if (lower.endsWith(".png"))                              mime = "image/png";
    else if (lower.endsWith(".webp"))                             mime = "image/webp";
    else if (lower.endsWith(".gif"))                              mime = "image/gif";
    else if (lower.endsWith(".bmp"))                              mime = "image/bmp";
    if (!mime) return false;
    if (mimeCap) safeCopy(mimeOut, mimeCap, mime);

    if (!radioAudioSdMounted()) return false;
    extern bool sdMutexLock(TickType_t timeoutTicks);
    extern void sdMutexUnlock();
    if (!sdMutexLock(pdMS_TO_TICKS(8000))) return false;

    File f = SD_MMC.open(absPath.c_str());
    if (!f || f.isDirectory()) { sdMutexUnlock(); return false; }
    size_t sz = (size_t)f.size();
    if (sz == 0 || sz > kMaxLibraryArtBytes) { f.close(); sdMutexUnlock(); return false; }
    uint8_t* buf = (uint8_t*)heap_caps_calloc(1, sz, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (uint8_t*)heap_caps_calloc(1, sz, MALLOC_CAP_8BIT);
    if (!buf) { f.close(); sdMutexUnlock(); return false; }
    if (f.read(buf, sz) != sz) {
        heap_caps_free(buf);
        f.close();
        sdMutexUnlock();
        return false;
    }
    f.close();
    sdMutexUnlock();
    dataOut = buf;
    sizeOut = sz;
    return true;
}

// Lists the DIRECT children of a /library folder (sub-folders + music files)
// with a live SD walk — no cache, no scan, no index file. absFolder is the
// absolute path inside /library ("" or "/library" = root). Files are never
// opened here (zero tag reads); embedded metadata/art is read once per track
// on PLAYBACK.
String radioAudioListLibrary(const String& absFolder) {
    String key = (absFolder.length() == 0) ? String(kLibraryDir) : absFolder;
    SpiRamJsonDocument doc(32768);
    doc["success"] = false;
    if (!radioAudioSdMounted()) {
        doc["library_available"] = false;
        doc["message"] = "SD card not available";
        String out;
        serializeJson(doc, out);
        return out;
    }
    doc["success"] = true;
    doc["is_root"] = (key == kLibraryDir);
    if (!doc["is_root"].as<bool>()) {
        int slash = key.lastIndexOf('/');
        doc["parent"] = (slash > 0) ? key.substring(0, slash) : String(kLibraryDir);
    }
    JsonArray folders = doc.createNestedArray("folders");
    JsonArray items   = doc.createNestedArray("items");

    extern bool sdMutexLock(TickType_t timeoutTicks);
    extern void sdMutexUnlock();
    if (sdMutexLock(pdMS_TO_TICKS(8000))) {
        File dir = SD_MMC.open(key.c_str());
        if (dir && dir.isDirectory()) {
            File e = dir.openNextFile();
            while (e) {
                String name = String(e.name());
                if (e.isDirectory()) {
                    if (!name.startsWith(".")) {
                        JsonObject fo = folders.createNestedObject();
                        fo["name"] = name;
                        fo["path"] = key + "/" + name;
                        String coverName;
                        if (radioAudioFindFolderCoverInside(e, coverName)) {
                            fo["cover"] = key + "/" + name + "/" + coverName;
                        }
                    }
                } else {
                    String lower = name;
                    lower.toLowerCase();
                    if (radioAudioIsMusicFile(name) &&
                        !(lower.endsWith(".flac") && !radioAudioIsSafeFlacFile(e.path()))) {
                        JsonObject o = items.createNestedObject();
                        o["name"] = name;
                        o["path"] = String(e.path());
                    }
                }
                e.close();
                e = dir.openNextFile();
            }
            dir.close();
        }
        sdMutexUnlock();
    }
    doc["count"] = items.size();
    String out;
    serializeJson(doc, out);
    return out;
}

// All music files (recursively) under an absolute /library folder - used to
// build the queue for "play ordered" / "play random" from the folder context menu.
// Serialized on the SD mutex: this walks the SD from a web handler while the
// audio loop may also touch the SD on the other core.
String radioAudioListFolderMusic(const String& absFolder) {
    SpiRamJsonDocument doc(131072);
    doc["success"] = true;
    JsonArray arr = doc.createNestedArray("items");
    bool skippedFlac = false;
    if (radioAudioSdMounted()) {
        extern bool sdMutexLock(TickType_t timeoutTicks);
        extern void sdMutexUnlock();
        if (sdMutexLock(pdMS_TO_TICKS(8000))) {
            String base = absFolder.length() ? absFolder : String(kLibraryDir);
            File dir = SD_MMC.open(base);
            if (dir && dir.isDirectory()) {
                radioAudioScanLibraryDir(dir, arr, skippedFlac, 0);
                dir.close();
            }
            sdMutexUnlock();
        }
    }
    doc["count"] = arr.size();
    if (skippedFlac) doc["skipped_flac"] = true;
    String out;
    serializeJson(doc, out);
    return out;
}

// Recursive collect of playable music paths under a folder into a vector.
static void radioAudioCollectMusicDir(File& dir, std::vector<String, PsramAllocatorStd<String>>& out, int depth) {
    if (depth > 5) return; // guard against very deep trees
    File entry = dir.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            radioAudioCollectMusicDir(entry, out, depth + 1);
        } else {
            String name = String(entry.name());
            String lower = name;
            lower.toLowerCase();
            if (radioAudioIsMusicFile(name)) {
                if (lower.endsWith(".flac") && !radioAudioIsSafeFlacFile(entry.path())) {
                    // high-res FLAC -> skipped (matches the library listing)
                } else {
                    out.push_back(String(entry.path()));
                }
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
}

// Play every music file under an absolute folder as a queue, starting with the
// first track. Used by the weekly schedule "folder" source (and mirrors the
// folder context menu's play-ordered behaviour).
bool radioAudioPlayFolder(const String& folder, String& messageOut) {
    if (!radioAudioSdMounted()) {
        messageOut = F("SD unavailable");
        return false;
    }
    // Serialised with the audio loop + web handlers on the shared SD mutex.
    if (!sdMutexLock(pdMS_TO_TICKS(8000))) {
        messageOut = F("SD busy");
        return false;
    }
    String base = folder.length() ? folder : String(kLibraryDir);
    File dir = SD_MMC.open(base);
    if (!dir || !dir.isDirectory()) {
        messageOut = F("Folder not found on SD");
        sdMutexUnlock();
        return false;
    }
    std::vector<String, PsramAllocatorStd<String>> entries;
    radioAudioCollectMusicDir(dir, entries, 0);
    dir.close();
    sdMutexUnlock();
    if (entries.empty()) {
        messageOut = F("No music in folder");
        return false;
    }
    s_queue = entries;
    s_queueIndex = 0;
    radioAudioRequestPersist();  // never LittleFS from an async context
    radioAudioPlayEntryDeferred(entries[0]);
    messageOut = F("Folder queued");
    return true;
}

// ============================================================
// Playlists (M3U on SD)
// ============================================================
static bool radioAudioEnsurePlaylistDir() {
    if (!radioAudioSdMounted()) return false;
    if (!sdMutexLock(pdMS_TO_TICKS(4000))) return false;
    bool ok = true;
    if (!SD_MMC.exists(kPlaylistDir)) {
        ok = SD_MMC.mkdir(kPlaylistDir);
    }
    sdMutexUnlock();
    return ok;
}

static bool radioAudioParseM3u(const String& path, std::vector<String, PsramAllocatorStd<String>>& entries) {
    if (!radioAudioSdMounted()) return false;
    if (!sdMutexLock(pdMS_TO_TICKS(8000))) return false;
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) {
        sdMutexUnlock();
        return false;
    }

    entries.clear();
    String line;
    while (f.available()) {
        line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        if (line.startsWith("#")) continue; // skip comments/extinf
        if (line.startsWith("sd:/") || line.startsWith("/sd/") ||
            line.startsWith("http://") || line.startsWith("https://")) {
            entries.push_back(line);
        }
    }
    f.close();
    sdMutexUnlock();
    return entries.size() > 0;
}

bool radioAudioSavePlaylist(const char* name, String& msg) {
    if (!name || !name[0]) {
        msg = F("Missing playlist name");
        return false;
    }
    if (!radioAudioEnsurePlaylistDir()) {
        msg = F("SD unavailable or failed to create playlist directory");
        return false;
    }
    String path = String(kPlaylistDir) + "/" + String(name) + ".m3u";
    if (!sdMutexLock(pdMS_TO_TICKS(8000))) {
        msg = F("SD busy");
        return false;
    }
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) {
        sdMutexUnlock();
        msg = F("Failed to create playlist file");
        return false;
    }
    f.println("#EXTM3U");
    for (size_t i = 0; i < s_queue.size(); i++) {
        f.println(s_queue[i]);
    }
    size_t plSize = f.size();
    f.close();
    storNoteSdWrite(plSize);
    sdMutexUnlock();
    msg = F("Playlist saved");
    return true;
}

bool radioAudioLoadPlaylist(const char* name, String& msg) {
    String path = String(kPlaylistDir) + "/" + String(name) + ".m3u";
    std::vector<String, PsramAllocatorStd<String>> entries;
    if (!radioAudioParseM3u(path, entries)) {
        msg = F("Failed to load playlist");
        return false;
    }
    if (entries.size() == 0) {
        msg = F("Empty playlist");
        return false;
    }
    s_queue = entries;
    s_queueIndex = 0;
    radioAudioRequestPersist();  // never LittleFS from the async playlist handler
    // Start the first entry in the loop task (playback also persists state).
    radioAudioPlayEntryDeferred(s_queue[0]);
    msg = F("Playlist loaded");
    return true;
}

String radioAudioListPlaylists() {
    radioAudioEnsurePlaylistDir();
    SpiRamJsonDocument doc(4096);
    doc["success"] = true;
    JsonArray arr = doc.createNestedArray("playlists");

    if (sdMutexLock(pdMS_TO_TICKS(8000))) {
        File dir = SD_MMC.open(kPlaylistDir);
        if (dir && dir.isDirectory()) {
            File f = dir.openNextFile();
            while (f) {
                String name = String(f.name());
                if (name.endsWith(".m3u")) {
                    String display = name.substring(0, name.length() - 4);
                    arr.add(display);
                }
                f.close();
                f = dir.openNextFile();
            }
            dir.close();
        }
        sdMutexUnlock();
    }
    String out;
    serializeJson(doc, out);
    return out;
}

bool radioAudioDeletePlaylist(const char* name, String& msg) {
    String path = String(kPlaylistDir) + "/" + String(name) + ".m3u";
    if (!radioAudioSdMounted()) {
        msg = F("SD unavailable");
        return false;
    }
    if (!sdMutexLock(pdMS_TO_TICKS(8000))) {
        msg = F("SD busy");
        return false;
    }
    bool ok = false;
    if (!SD_MMC.exists(path)) {
        msg = F("Playlist not found");
    } else if (SD_MMC.remove(path)) {
        msg = F("Playlist deleted");
        ok = true;
    } else {
        msg = F("Failed to delete playlist");
    }
    sdMutexUnlock();
    return ok;
}

// ============================================================
// Shuffle / Repeat
// ============================================================
bool radioAudioGetShuffle() { return s_shuffle; }

void radioAudioSetShuffle(bool on) { s_shuffle = on; }

uint8_t radioAudioGetRepeatMode() { return s_repeatMode; }

// Auto-resume after boot: master switch + resume target (first of playlist
// vs the source that was playing). Persisted in the radio state file.
bool radioAudioGetResume(bool& autoResume, bool& resumeFirst) {
    autoResume = s_autoResume;
    resumeFirst = s_resumeFirst;
    return true;
}

void radioAudioSetResume(bool autoResume, bool resumeFirst) {
    if (s_autoResume == autoResume && s_resumeFirst == resumeFirst) return;
    s_autoResume = autoResume;
    s_resumeFirst = resumeFirst;
    radioAudioRequestPersist(); // deferred — the loop task writes LittleFS
}

void radioAudioSetRepeatMode(uint8_t mode) {
    if (mode > 2) mode = 0;
    s_repeatMode = mode;
}

// ============================================================
// Sleep timer
// ============================================================

// Remaining seconds until the sleep timer stops audio.
// Returns: 0 if off/expired, -1 if not computable yet (clock not synced after
// a reboot and no in-session fallback), or the number of seconds remaining.
int radioAudioSleepRemainingSec() {
    if (s_sleepEndEpoch <= 0) return 0;
    time_t now = time(nullptr);
    if (now >= 1600000000) {  // NTP/RTC synced — epoch is authoritative
        // Rebase a sleep that was set before NTP sync once the clock is valid.
        if (s_sleepEndEpoch < 1600000000 && s_sleepDurationMin > 0) {
            uint32_t elapsed = s_sleepSetUptimeMs ? (millis() - s_sleepSetUptimeMs) / 1000UL : 0;
            uint32_t total = s_sleepDurationMin * 60UL;
            s_sleepEndEpoch = (elapsed >= total) ? now : now + (time_t)(total - elapsed);
            radioAudioRequestPersistState();  // never LittleFS from the async status handler
        }
        long sec = (long)(s_sleepEndEpoch - now);
        if (sec <= 0) return 0;
        return (int)sec;
    }
    // Clock not synced yet: use the in-session millis fallback when available.
    if (s_sleepDurationMin > 0 && s_sleepSetUptimeMs > 0) {
        uint32_t elapsed = (millis() - s_sleepSetUptimeMs) / 1000UL;
        uint32_t total = s_sleepDurationMin * 60UL;
        if (elapsed >= total) return 0;
        return (int)(total - elapsed);
    }
    return -1;  // rebooted, still waiting for time sync
}

void radioAudioSetSleep(int minutes) {
    if (minutes <= 0) {
        s_sleepEndEpoch = 0;
        s_sleepDurationMin = 0;
        s_sleepSetUptimeMs = 0;
        radioAudioRequestPersistState();
        return;
    }
    time_t now = time(nullptr);
    s_sleepSetUptimeMs = millis();
    s_sleepDurationMin = (uint32_t)minutes;
    if (now >= 1600000000) {
        s_sleepEndEpoch = now + (time_t)minutes * 60;
    } else {
        // Unsynced clock: keep a relative epoch so the delta still counts down
        // this session; after a reboot the synced clock corrects the end time.
        s_sleepEndEpoch = (now >= 0 ? now : 0) + (time_t)minutes * 60;
    }
    radioAudioRequestPersistState();
}

int radioAudioSleepMinutes() { return (int)s_sleepDurationMin; }

// Async web handlers call this instead of writing LittleFS directly, so all
// radio persistence happens serially in the loop task (no contention with
// ConfigManager saves). All variants debounce: consecutive requests coalesce
// into a single flush in the loop, and each dirty flag persists only the file
// that actually changed (volume/EQ/sleep touch the small state file only, not
// the up-to-8KB queue/favorites files).
void radioAudioRequestPersist() {
    s_persistDirty |= 0x07;
    s_persistAtMs = millis() + kPersistDebounceMs;
}

void radioAudioRequestPersistState() {
    s_persistDirty |= 0x01;
    s_persistAtMs = millis() + kPersistDebounceMs;
}

// ============================================================
// Equalizer
// ============================================================
uint8_t radioAudioGetEqBands() { return kEqBands; }

int8_t radioAudioGetEqBandGain(uint8_t band) {
    if (band >= kEqBands) return 0;
    return s_eqGains[band];
}

bool radioAudioSetEqBandGain(uint8_t band, int8_t gainDB) {
    if (band >= kEqBands) return false;
    if (gainDB < -12) gainDB = -12;
    if (gainDB > 12) gainDB = 12;
    s_eqGains[band] = gainDB;
    s_eqState.dirty = true;
    radioAudioRequestPersistState(); // never LittleFS from the async EQ handler
    return true;
}

// ============================================================
// Stubs for non-PACK_RADIO builds
// ============================================================
#else

void radioAudioRuntimeBegin() {}
void radioAudioRuntimeLoop() {}
void radioAudioCancelResume() {}
void radioAudioTogglePlayDeferred() {}
void radioAudioPlayEntryDeferred(const String&) {}
bool radioAudioTogglePlay(String& messageOut) { messageOut = F("PACK_RADIO only"); return false; }
bool radioAudioPlayUrl(const String&, String& messageOut) { messageOut = F("PACK_RADIO only"); return false; }
bool radioAudioPlaySdPath(const String&, String& messageOut) { messageOut = F("PACK_RADIO only"); return false; }
bool radioAudioPlayFolder(const String&, String& messageOut) { messageOut = F("PACK_RADIO only"); return false; }
bool radioAudioPlayNext(String& messageOut) { messageOut = F("PACK_RADIO only"); return false; }
bool radioAudioPlayRandom(String& messageOut) { messageOut = F("PACK_RADIO only"); return false; }
void radioAudioStop() {}
void radioAudioSetVolume(uint8_t) {}
bool radioAudioSetQueue(const std::vector<String>&, int, String& messageOut) { messageOut = F("PACK_RADIO only"); return false; }
String radioAudioQueueJson() { return String(F("{\"success\":false,\"message\":\"PACK_RADIO only\"}")); }
bool radioAudioAddFavorite(const String&, const String&, String& messageOut, const String&) { messageOut = F("PACK_RADIO only"); return false; }
bool radioAudioRemoveFavorite(int, String& messageOut) { messageOut = F("PACK_RADIO only"); return false; }
bool radioAudioPlayFavorite(int, String& messageOut) { messageOut = F("PACK_RADIO only"); return false; }
String radioAudioFavoritesJson() { return String(F("{\"success\":false,\"message\":\"PACK_RADIO only\"}")); }
String radioAudioStatusJson() { return String(F("{\"success\":true,\"active\":false,\"message\":\"PACK_RADIO only\"}")); }
bool radioAudioIsActive() { return false; }
// Cover art stubs
void radioAudioClearArtwork() {}
const uint8_t* radioAudioGetArtworkData(uint32_t& outSize) { outSize=0; return nullptr; }
const char* radioAudioGetArtworkMime() { return ""; }
// Search stub
String radioAudioSearch(const char*) { return String(F("{\"success\":false,\"message\":\"PACK_RADIO only\"}")); }
String radioAudioListLibrary(const String&) { return String(F("{\"success\":false,\"message\":\"PACK_RADIO only\"}")); }
String radioAudioListFolderMusic(const String&) { return String(F("{\"success\":false,\"message\":\"PACK_RADIO only\"}")); }
bool radioAudioLoadLibraryArt(const String&, const uint8_t*& dataOut, uint32_t& sizeOut, char*, size_t) { dataOut = nullptr; sizeOut = 0; return false; }
bool radioAudioSearchStations(const String&, const String&, const String&, int, int, String& jsonOut, String&, bool*) { jsonOut = F("[]"); return false; }
bool radioAudioGetTags(String& jsonOut, String&) { jsonOut = F("[]"); return false; }
bool radioAudioGetCountries(String& jsonOut, String&) { jsonOut = F("[]"); return false; }
void radioAudioSetStationFavicon(const char*) {}
void radioAudioSetStationName(const char*) {}
const char* radioAudioGetStationFavicon() { return ""; }
// Playlist stubs
bool radioAudioSavePlaylist(const char*, String& msg) { msg=F("PACK_RADIO only"); return false; }
bool radioAudioLoadPlaylist(const char*, String& msg) { msg=F("PACK_RADIO only"); return false; }
String radioAudioListPlaylists() { return String(F("{\"success\":false,\"message\":\"PACK_RADIO only\"}")); }
bool radioAudioDeletePlaylist(const char*, String& msg) { msg=F("PACK_RADIO only"); return false; }
// Shuffle/Repeat stubs
bool radioAudioGetShuffle() { return false; }
void radioAudioSetShuffle(bool) {}
uint8_t radioAudioGetRepeatMode() { return 0; }
void radioAudioSetRepeatMode(uint8_t) {}
// Resume stubs
bool radioAudioGetResume(bool& autoResume, bool& resumeFirst) {
    autoResume = true; resumeFirst = false; return true;
}
void radioAudioSetResume(bool, bool) {}
// EQ stubs
uint8_t radioAudioGetEqBands() { return 0; }
int8_t radioAudioGetEqBandGain(uint8_t) { return 0; }
bool radioAudioSetEqBandGain(uint8_t, int8_t) { return false; }
// Sleep stubs
void radioAudioSetSleep(int) {}
int radioAudioSleepRemainingSec() { return 0; }
int radioAudioSleepMinutes() { return 0; }
void radioAudioRequestPersist() {}
void radioAudioRequestPersistState() {}

#endif

#endif
