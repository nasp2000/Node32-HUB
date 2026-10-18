# Node32-HUB — SBOM

Software bill of materials for the firmware (all packs). Versions read from the
installed packages, not from registry metadata.

- **Firmware:** v1.114
- **Generated:** 2026-09-20

Envs: `n16r8` (S3: Rambox/Cam/PSRAM/HomeE32/Radio/S3-CNC) · `waveshare_8di8do`
(Node32-OEE) · `s3_touch7` (TouchMeteo-7) · `esp32p4` (P4-CNC / P4-USB-Server) ·
`esp32p4cam` (Cam-P4).

## Toolchain / platform

| Component | Version | License |
|---|---|---|
| pioarduino platform-espressif32 | 55.03.35 | Apache-2.0 |
| pioarduino platform-espressif32 (esp32p4cam) | 55.03.312 | Apache-2.0 |
| arduino-esp32 core | 3.3.5 | LGPL-2.1-only |
| arduino-esp32 core (esp32p4cam) | 3.3.12 | LGPL-2.1-only |
| ESP-IDF (bundled with core) | 5.5.1 | Apache-2.0 |
| xtensa-esp32s3 / riscv32-esp-elf GCC | — | GPL-3.0 + GCC Runtime Library Exception (build-time only) |

## Runtime libraries

| Component | Version | License | Envs |
|---|---|---|---|
| bblanchon/ArduinoJson | 6.21.6 | MIT | all |
| ESP32Async/AsyncTCP | 3.5.0 | LGPL-3.0-only | all |
| ESP32Async/ESPAsyncWebServer | 3.12.1 | LGPL-3.0-only | all |
| rlogiacco/CircularBuffer | 1.4.0 | LGPL-3.0-only | all |
| greiman/SdFat | 2.3.1 | MIT | all |
| arduino-libraries/Ethernet | 2.0.2 | MIT | all |
| mobizt/ReadyMail | 0.4.0 | MIT | all |
| mobizt/ESP_SSLClient | 3.1.3 | MIT | all |
| adafruit/Adafruit SSD1306 | 2.5.17 | BSD-3-Clause | esp32p4, esp32p4cam, n16r8, s3_touch7, waveshare_8di8do |
| adafruit/Adafruit SH110X | 2.1.15 | BSD-3-Clause | n16r8, s3_touch7, waveshare_8di8do |
| adafruit/Adafruit GFX Library | 1.12.6 | BSD-3-Clause | all |
| adafruit/Adafruit AHTX0 | 2.0.6 | BSD-3-Clause | n16r8, s3_touch7, waveshare_8di8do |
| adafruit/Adafruit BMP280 Library | 3.0.0 | MIT | n16r8, s3_touch7, waveshare_8di8do |
| adafruit/Adafruit BusIO | 1.17.4 | MIT | all |
| adafruit/Adafruit Unified Sensor | 1.1.15 | Apache-2.0 | n16r8, s3_touch7, waveshare_8di8do |
| lvgl/lvgl | 9.6.0 | MIT | s3_touch7 |
| ESP32_Display_Panel | 1.0.4 | Apache-2.0 | s3_touch7 |
| ESP32_IO_Expander | 1.1.1 | Apache-2.0 | s3_touch7 |
| esp-lib-utils | 0.2.0 | Apache-2.0 | s3_touch7 |
| Bodmer/JPEGDecoder | 2.0.0 | ChaN permissive | s3_touch7 |
| ESP_Video | 3.3.11 | Apache-2.0 | esp32p4cam |
| espressif/esp_h264 (HW encoder + tinyh264/openh264, Fase 1 T5) | 3.3.12-core | Apache-2.0 | esp32p4cam |
| espressif/esp-dl (Fase 5 T5 vision hook only, no model run yet) | 3.3.12-core | Apache-2.0 | esp32p4cam |

## Vendored (`chatgpt/lib/`)

| Component | Version | License | Envs |
|---|---|---|---|
| lieff/minimp3 | commit `ea99364f61c14656440e8d77e9c233ccf3124633` | CC0-1.0 | n16r8 (Radio) |
| mackron/dr_flac | 0.13.4 | Unlicense / MIT-0 | n16r8 (Radio) |
| nothings/stb_vorbis | 1.22 | Unlicense | n16r8 (Radio) |
| espressif/esp_audio_codec | 2.6.2 | Espressif Modified MIT | n16r8 (Radio) |

## First-party

First-party code is **MIT** (`LICENSE`). Drivers/wrappers with no external
license: HX711, INA226, MAX31865, MQ135, LPT/DB25, Modbus RTU/TCP/slave, CAN,
GRBL (web), proximity, buzzer, UDA1334A DAC, and the audio decoders/wrappers
(`AacDecoder`, `WavDecoder`, `Minimp3Decoder`, `FlacDecoder`,
`OggVorbisDecoder`, `OggOpusDecoder`, `adts_parser.h`, `wav_parser.h`,
`ogg_parser.h`).

## Notes

- **LGPL static linking:** AsyncTCP, ESPAsyncWebServer and CircularBuffer
  (LGPL-3.0) plus the arduino-esp32 core (LGPL-2.1) are linked statically. The
  distribution ships their sources + relinkable objects
  (`<build>/third-party.zip`) + license texts so the recipient can relink them.
- **ReadyMail is pinned to 0.4.0 (MIT).** 0.4.1 ("ReadyMail Custom License") and
  0.4.2 (CC-BY-NC-4.0) are non-commercial. Do not relax the constraint.
- **T16 (Radio OGG/Opus):** Vorbis decodes via vendored stb_vorbis 1.22
  (Unlicense); Opus decodes via the libopus core inside the already-vendored
  esp_audio_codec 2.6.2 prebuilt (Espressif Modified MIT) — no new binary
  dependency, no raw libopus/libogg vendor tree. Both licenses are
  MIT-compatible, matching the existing radio decoder set.
