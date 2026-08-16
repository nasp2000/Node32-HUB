# Node32-HUB — SBOM / Licensing Manifest

Software Bill of Materials + license audit for the **complete** Node32-HUB project
(all packs: N16R8, Waveshare 8DI8DO, P4 CNC).

- **Firmware:** v1.100
- **Toolchain:** pioarduino platform-espressif32 55.03.35 / arduino-esp32 3.3.5 / ESP-IDF 5.5.1
- **Generated:** 2026-08-16 — licenses verified from the **installed packages** (`.pio/libdeps`, `~/.platformio/packages`), not from registry metadata alone.

---

## Status

### ✅ Resolved

- [x] `LICENSE` in repo root (proprietary + free-binary grant).
- [x] `THIRD_PARTY_NOTICES` with all MIT/BSD/Apache copyright headers.
- [x] LGPL compliance materials publishable via `scripts/package_third_party.py` (sources + relinkable objects).
- [x] `THIRD_PARTY_NOTICES` entries for minimp3 (CC0-1.0) and dr_flac (Unlicense).
- [x] ReadyMail confirmed **MIT** (not a commercial blocker).
- [x] Ethernet installed code confirmed **MIT** (Stoffregen), not LGPL.

### ❌ Not resolved (open items)

- [ ] Verify `esp-libhelix-mp3` / `esp-sr` not linked into shipped n16r8/waveshare builds (P4-env IDF deps; currently excluded via `build_src_filter`).

---

## Libraries used (direct runtime dependencies)

| Package | Version | SPDX | Used by |
|---|---|---|---|
| bblanchon/ArduinoJson | 6.21.6 | MIT | all |
| ESP32Async/AsyncTCP | 3.4.10 | LGPL-3.0-only | all |
| ESP32Async/ESPAsyncWebServer | 3.11.1 | LGPL-3.0-only | all |
| rlogiacco/CircularBuffer | 1.4.0 | LGPL-3.0-only | all |
| adafruit/Adafruit SSD1306 | 2.5.16 | BSD-3-Clause | N16R8, Waveshare (OLED) |
| adafruit/Adafruit SH110X | 2.1.14 | BSD-3-Clause | N16R8, Waveshare (transitive) |
| adafruit/Adafruit GFX Library | 1.12.6 | BSD-3-Clause | N16R8, Waveshare (transitive) |
| adafruit/Adafruit AHTX0 | 2.0.6 | BSD-3-Clause | N16R8, Waveshare (sensor) |
| adafruit/Adafruit BMP280 Library | 3.0.0 | MIT | N16R8, Waveshare (sensor) |
| adafruit/Adafruit BusIO | 1.17.4 | MIT | N16R8, Waveshare (transitive) |
| adafruit/Adafruit Unified Sensor | 1.1.15 | Apache-2.0 | N16R8, Waveshare (transitive) |
| arduino-libraries/Ethernet | 2.0.2 | MIT | all (P4 keeps for headers) |
| greiman/SdFat | 2.3.1 | MIT | all (SD) |
| mobizt/ReadyMail | 0.4.0 | MIT | all (SMTP) |
| mobizt/ESP_SSLClient | 3.1.3 | MIT | all (email TLS over Ethernet) |

### Local / vendored (project `lib/`)

| Package | Version | SPDX | Used by |
|---|---|---|---|
| lieff/minimp3 | commit `ea99364f61c14656440e8d77e9c233ccf3124633` | CC0-1.0 | N16R8 (Radio, MP3) |
| mackron/dr_flac | 0.13.4 | Unlicense / MIT-0 | N16R8 (Radio, FLAC) |
| espressif/esp_audio_codec | 2.6.2 | Espressif Modified MIT | N16R8 (Radio, AAC) |

### Framework / platform / toolchain (build-time / runtime)

| Component | Version | License |
|---|---|---|
| pioarduino platform-espressif32 | 55.03.35 | Apache-2.0 |
| arduino-esp32 core | 3.3.5 | LGPL-2.1-only |
| ESP-IDF (bundled with core) | 5.5.1 | Apache-2.0 |
| Toolchain xtensa-esp32s3 / riscv32 | — | GPL-3.0 + GCC Runtime Library Exception (build-time only) |

---

First-party drivers (no external license): HX711, INA226, MAX31865, MQ135,
AHT20 via Adafruit AHTX0, LPT/DB25, Modbus RTU/TCP/slave, CAN, GRBL (web),
proximity, buzzer, UDA1334A DAC. First-party audio decoders/wrappers:
`AacDecoder`, `WavDecoder`, `Minimp3Decoder`, `FlacDecoder`, `adts_parser.h`, `wav_parser.h`.
