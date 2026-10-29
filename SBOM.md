# Node32-HUB — SBOM

Software bill of materials for the firmware (all packs). Versions read from the
installed packages, not from registry metadata.

- **Firmware:** v1.120
- **Generated:** 2026-10-18

Envs: `n16r8` (S3: Rambox/Cam/RS232/HomeE32/Radio/S3-CNC) · `waveshare_8di8do`
(Node32-OEE) · `s3_touch7` (TouchMeteo-7) · `esp32p4` (P4-CNC / P4-USB-Server) ·
`esp32p4cam` (Cam-P4).

## Toolchain / platform

| Component | Version | License | Envs |
|---|---|---|---|
| pioarduino platform-espressif32 | 55.03.35 | Apache-2.0 | n16r8, waveshare_8di8do, esp32p4 |
| pioarduino platform-espressif32 | 53.03.13-1 | Apache-2.0 | s3_touch7 (Arduino 3.1.x + IDF 5.3.x — legacy I2C driver requirement, see platformio.ini) |
| pioarduino platform-espressif32 (esp32p4cam) | 55.03.312 | Apache-2.0 | esp32p4cam |
| arduino-esp32 core | 3.3.5 | LGPL-2.1-only | n16r8, waveshare_8di8do, esp32p4 |
| arduino-esp32 core (s3_touch7) | 3.1.x | LGPL-2.1-only | s3_touch7 |
| arduino-esp32 core (esp32p4cam) | 3.3.12 | LGPL-2.1-only | esp32p4cam |
| ESP-IDF (bundled with core) | 5.5.1 with core 3.3.5; 5.3.x with core 3.1.x | Apache-2.0 | all |
| xtensa-esp32s3 / riscv32-esp-elf GCC | — | GPL-3.0 + GCC Runtime Library Exception (build-time only) | all |

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
| adafruit/Adafruit SSD1306 | 2.5.17 | BSD-3-Clause | all |
| adafruit/Adafruit SH110X | 2.1.15 | BSD-3-Clause | all |
| adafruit/Adafruit GFX Library | 1.13.0 | BSD-3-Clause | all |
| adafruit/Adafruit AHTX0 | 2.0.6 | BSD-3-Clause | all |
| adafruit/Adafruit BMP280 Library | 3.0.0 | MIT | all |
| adafruit/Adafruit BusIO | 1.17.4 | MIT | all |
| adafruit/Adafruit Unified Sensor | 1.1.15 (transitive, unpinned — pulled as a registry dependency of AHT20/BMP280, resolved at install) | Apache-2.0 | all |
| lvgl/lvgl | 9.6.0 | MIT | s3_touch7 |
| ESP32_Display_Panel | 1.0.4 | Apache-2.0 | s3_touch7 |
| ESP32_IO_Expander | 1.1.1 | Apache-2.0 | s3_touch7 |
| esp-lib-utils | 0.2.0 | Apache-2.0 | s3_touch7 |
| Bodmer/JPEGDecoder | 2.0.0 | ChaN permissive | s3_touch7 |
| ESP_Video | bundled with arduino-esp32 3.3.12 (framework `libraries/ESP_Video`; no separate registry version) | Apache-2.0 | esp32p4cam |
| espressif/esp_h264 (HW encoder + tinyh264/openh264, Fase 1 T5) | managed with core 3.3.12 (version resolved at build, not separately pinned) | Apache-2.0 | esp32p4cam |
| espressif/esp-dl (Fase 5 T5 vision hook only, no model run yet) | managed with core 3.3.12 (version resolved at build, not separately pinned) | Apache-2.0 | esp32p4cam |

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
- **Transitive deps are unpinned by nature.** `Adafruit Unified Sensor` is not
  in `platformio.ini:lib_deps` — it is pulled as a registry dependency of the
  Adafruit sensor libraries and resolved at install time (1.1.15 observed).
  The `s3_touch7` git-URL deps ARE pinned by tag (`#v1.0.4`, `#v1.1.1`,
  `#v0.2.0`, `#v9.6.0`, `#2.0.0`).
- **Vendored versions verified** against `chatgpt/lib/*/library.json`
  (minimp3 commit `ea9934`, dr_flac 0.13.4, stb_vorbis 1.22, esp_audio_codec 2.6.2).
- **T16 (Radio OGG/Opus):** Vorbis decodes via vendored stb_vorbis 1.22
  (Unlicense); Opus decodes via the libopus core inside the already-vendored
  esp_audio_codec 2.6.2 prebuilt (Espressif Modified MIT) — no new binary
  dependency, no raw libopus/libogg vendor tree. Both licenses are
  MIT-compatible, matching the existing radio decoder set.
