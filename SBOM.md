# Node32-HUB — SBOM / Licensing Manifest

Software Bill of Materials + license audit for the **complete** Node32-HUB project
(all packs: N16R8, Waveshare 8DI8DO, P4 CNC).

- **Firmware:** v1.100
- **Toolchain:** pioarduino platform-espressif32 55.03.35 / arduino-esp32 3.3.5 / ESP-IDF 5.5.1
- **Generated:** 2026-08-16 — licenses verified from the **installed packages** (`.pio/libdeps`,
  `~/.platformio/packages`), not from registry metadata alone.
- **Scope:** everything linked into the firmware binaries + tooling used to build/test.

---

## 1. License summary tree

```
Node32-HUB
│
├── Proprietary
│   └── Node32-HUB firmware (src/, web assets, packs)   ← no LICENSE file (gap)
│
├── LGPL-3.0
│   ├── AsyncTCP
│   ├── ESPAsyncWebServer
│   └── CircularBuffer
│
├── LGPL-2.1
│   ├── arduino-esp32 core (framework)                  ← distribution obligation
│   └── (Ethernet — upstream metadata only; installed code is MIT)
│
├── MIT
│   ├── ArduinoJson
│   ├── ReadyMail                                       ← NOT a blocker (was assumed)
│   ├── Adafruit BMP280
│   ├── Adafruit BusIO
│   ├── SdFat
│   └── Ethernet (installed code, Stoffregen)
│
├── BSD-3-Clause
│   ├── Adafruit SSD1306
│   ├── Adafruit SH110X
│   ├── Adafruit GFX
│   ├── Adafruit AHTX0
│   └── SdFat (BSD exception: DM partitions etc.)
│
├── CC0-1.0 / Unlicense (public domain)
│   ├── minimp3 (MP3 decoder, vendored)
│   └── dr_flac (FLAC decoder, vendored)
│
├── Espressif Modified MIT (ESP32-only)
│   └── espressif/esp_audio_codec (AAC decoder; IDF component via component manager)
│
├── Apache-2.0
│   ├── pioarduino platform
│   ├── ESP-IDF (bundled)
│   ├── Adafruit Unified Sensor
│   ├── esp32-camera (optional, ENABLE_CAMERA=1)
│   └── espressif IDF components (mdns, esp-dsp, esp-modbus, wifi_remote…)
│
└── Restricted / source-available (framework transitive)
    ├── chmorgan/esp-libhelix-mp3 (RealNetworks license) ← flagged
    └── espressif/esp-sr, esp-zboss-lib, esp-zigbee-lib (precompiled, Espressif terms)
```

---

## 2. Direct runtime dependencies (PlatformIO `lib_deps`)

| Package | Version | SPDX | Verified from | Used by pack |
|---|---|---|---|---|
| bblanchon/ArduinoJson | 6.21.6 | MIT | `LICENSE.txt` | all |
| ESP32Async/AsyncTCP | 3.4.10 | LGPL-3.0-only | `LICENSE` | all |
| ESP32Async/ESPAsyncWebServer | 3.11.1 | LGPL-3.0-only | `LICENSE` | all |
| rlogiacco/CircularBuffer | 1.4.0 | LGPL-3.0-only | `LICENSE` | all |
| adafruit/Adafruit SSD1306 | 2.5.16 | BSD-3-Clause | `license.txt` | N16R8, Waveshare (OLED) |
| adafruit/Adafruit SH110X | 2.1.14 | BSD-3-Clause | `license.txt` | N16R8, Waveshare (transitive) |
| adafruit/Adafruit GFX Library | 1.12.6 | BSD-3-Clause | `license.txt` | N16R8, Waveshare (transitive) |
| adafruit/Adafruit AHTX0 | 2.0.6 | BSD-3-Clause | `license.txt` | N16R8, Waveshare (sensor) |
| adafruit/Adafruit BMP280 Library | 3.0.0 | MIT | `LICENSE.txt` | N16R8, Waveshare (sensor) |
| adafruit/Adafruit BusIO | 1.17.4 | MIT | `LICENSE` | N16R8, Waveshare (transitive) |
| adafruit/Adafruit Unified Sensor | 1.1.15 | Apache-2.0 | `LICENSE.txt` | N16R8, Waveshare (transitive) |
| arduino-libraries/Ethernet | 2.0.2 | MIT ⚠ | `src/Ethernet.h` | all (P4 keeps for headers) |
| greiman/SdFat | 2.3.1 | MIT | `LICENSE.md` | all (SD) |
| mobizt/ReadyMail | 0.4.0 | MIT | `LICENSE` + README | all (SMTP) |
| mobizt/ESP_SSLClient | 3.1.3 | MIT | `LICENSE` | all (email TLS over Ethernet) |

> ⚠ **Ethernet:** the *installed* code headers are Paul Stoffregen's **MIT** version
> (that is what is compiled). Upstream `arduino-libraries/Ethernet` metadata says
> LGPL-2.1. Treat as MIT but keep the copyright header.

### Local/vendored runtime dependencies (project `lib/`)

| Package | Version | SPDX | Verified from | Used by pack |
|---|---|---|---|---|
| lieff/minimp3 | commit `ea99364f61c14656440e8d77e9c233ccf3124633` | CC0-1.0 | `lib/minimp3/src/minimp3.h` header (public-domain dedication) | N16R8 (Radio, MP3) |
| mackron/dr_flac | 0.13.4 | Unlicense / MIT-0 | `lib/dr_flac/src/dr_flac.h` header (license statements at EOF) | N16R8 (Radio, FLAC) |
| espressif/esp_audio_codec | 2.6.2 | Espressif Modified MIT (`LicenseRef-Espressif-Modified-MIT`) | vendored in `lib/esp_audio_codec/` (headers + `libesp_audio_codec.a` esp32s3) | N16R8 (Radio, AAC) |

> `minimp3` is a single-header MP3 decoder (MIT-adjacent **CC0-1.0** public
> domain). Vendored at the pinned commit (no tagged releases upstream).

> `dr_flac` is a single-header FLAC decoder by David Reid, released under
> **Unlicense** (public domain) or MIT-0 (no attribution). Used for FLAC SD
> playback. FLAC is restricted to 44.1/48 kHz 16-bit by the runtime safety
> check. Decoder-internal allocations are routed to PSRAM via
> `drflac_allocation_callbacks`.

> `esp_audio_codec` (AAC) is the official Espressif audio codec module, under
> **Espressif Modified MIT** — permitted for use exclusively with Espressif
> products (this project is 100% ESP32-S3/P4). Only the AAC decoder is linked
> (all other codecs are dropped by `--gc-sections`). The decoder's internal
> state (~51 KB) lives in internal RAM while playing; the ADTS input buffer and
> PCM output are allocated in PSRAM.

### Decoder stack — license audit

| Codec | Backend | License | Memory |
|---|---|---|---|
| MP3 | minimp3 (first-party wrapper) | CC0-1.0 | PSRAM buffers |
| FLAC | dr_flac (first-party wrapper) | Unlicense / MIT-0 | PSRAM (state via callbacks) |
| AAC / HE-AAC | esp_audio_codec (AacDecoder) | Espressif Modified MIT | PSRAM buffers + ~51 KB internal state |
| WAV (PCM 16-bit) | first-party `WavDecoder` | N/A (first-party) | PSRAM buffer |
| OGG (Vorbis) / OPUS | not linked | — | — (planned: Tremor/libopus, PSRAM) |

---

## 3. Framework, platform, toolchain (build-time / runtime)

| Component | Version | License | Notes |
|---|---|---|---|
| pioarduino platform-espressif32 | 55.03.35 | Apache-2.0 | fork of Espressif platform |
| arduino-esp32 core | 3.3.5 | LGPL-2.1-only | `idf_component.yml` + core headers |
| ESP-IDF (bundled with core) | 5.5.1 | Apache-2.0 | notable bundled parts: FreeRTOS kernel (MIT), lwIP (BSD-3-Clause), mbedTLS (Apache-2.0 OR GPL-2.0), libsodium (ISC), Newlib (BSD) |
| Toolchain xtensa-esp32s3 / riscv32 | — | GPL-3.0 + GCC Runtime Library Exception | build-time only, not shipped |

### Framework IDF component dependencies (component manager, per `idf_component.yml`)

Pulled for the P4 env (see `build_src_filter` in `platformio.ini`); most are
Apache-2.0, but watch these:

| Component | Version | License | Flag |
|---|---|---|---|
| chmorgan/esp-libhelix-mp3 | 1.0.3 | RealNetworks | 🔴 restricted |
| espressif/esp-sr | ^2.1.5 | Espressif (precompiled) | 🟠 |
| espressif/esp-zboss-lib / esp-zigbee-lib | 1.6.x | Espressif (precompiled) | 🟠 |
| espressif/libsodium | ^1.0.20 | ISC | 🟢 |
| joltwallet/littlefs | ^1.10.2 | MIT | 🟢 |
| espressif/mdns, esp-dsp, esp-modbus, esp_hosted, esp_wifi_remote, lan867x, cbor, esp_diag_data_store, esp_diagnostics, esp_insights, esp_rainmaker, esp_modem, network_provisioning, qrcode, rmaker_common | — | Apache-2.0 | 🟢 |

---

## 4. Optional / pack-specific

| Dependency | When | License |
|---|---|---|
| espressif/esp32-camera (esp_camera.h) | `ENABLE_CAMERA=1` (off by default; not in lib_deps — install manually) | Apache-2.0 |
| ESP_I2S (framework library) | Radio audio stack (native I2S output) | LGPL-2.1 (framework) |
| espressif/esp_audio_codec | Radio AAC (PACK_RADIO) — vendored in `lib/esp_audio_codec/`, AAC only | Espressif Modified MIT |

First-party drivers (no external license): HX711, INA226, MAX31865, MQ135,
AHT20 via Adafruit AHTX0, LPT/DB25, Modbus RTU/TCP/slave, CAN, GRBL (web),
proximity, buzzer, UDA1334A DAC. First-party audio decoders/wrappers:
`AacDecoder`, `WavDecoder`, `Minimp3Decoder`, `FlacDecoder`, `adts_parser.h`,
`wav_parser.h`.

---

## 5. Development / test tooling (not shipped)

| Tool | License |
|---|---|
| doctest.h (native tests) | MIT |
| jsdom (web tests) | MIT |
| pytest / requests (`tests/requirements.txt`) | MIT / Apache-2.0 |
| ccache | GPL-3.0 + exception (build tool) |
| PlatformIO core | Apache-2.0 |

---

## 6. Risk register — problems found

### 🟠 Distribution obligations (LGPL — manageable, but must act)
1. **AsyncTCP, ESPAsyncWebServer, CircularBuffer (LGPL-3.0)** and
   **arduino-esp32 core (LGPL-2.1)** are statically linked. LGPL requires
   providing the library source + the ability to relink. Practical compliance:
   publish the unchanged library sources and relinkable objects alongside the
   release. Standard for all Arduino firmware.

### 🟠 Watch-list
2. **chmorgan/esp-libhelix-mp3 (RealNetworks)** — pulled as a framework IDF
   dependency (P4 env only). Restricted license; verify it is not linked into
   shipped builds (Radio is locked to N16R8 in this build flow; P4
   build_src_filter excludes it).
3. **esp-sr / esp-zboss-lib / esp-zigbee-lib** — Espressif precompiled libs;
   verify they are excluded (they are, via `build_src_filter`, when unused).

### ✅ Good news (corrections to previous assumptions)
4. **ReadyMail is MIT** (Mobizt, 2025) — **not** a commercial blocker.
5. **Ethernet installed code is MIT** (Stoffregen), not LGPL.
6. All sensor/OLED libs are permissive (MIT / BSD / Apache).

### ⚠ Gaps
7. ~~**No `LICENSE` file in the repo root**~~ — **done** (proprietary + free-binary grant, see `LICENSE`).
8. ~~No `THIRD_PARTY_NOTICES`~~ — **done** (repo root; generated materials via `scripts/package_third_party.py` → `third-party/`).

---

## 7. Compliance checklist (before any release)

- [x] Add repo `LICENSE` for first-party code.
- [x] Add `THIRD_PARTY_NOTICES` with all MIT/BSD/Apache copyright headers.
- [x] Publish LGPL-2.1/LGPL-3.0 library sources + relinkable objects (script: `python scripts/package_third_party.py --env <env>`).
- [ ] Verify esp-libhelix-mp3 / esp-sr not linked in shipped n16r8/waveshare builds.
- [x] Add a `THIRD_PARTY_NOTICES` entry for minimp3 (CC0-1.0) and dr_flac (Unlicense) copyright lines.
