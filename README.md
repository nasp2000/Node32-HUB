# Node32-HUB

**Node32-HUB** is a modular, multi-protocol firmware framework for ESP32-S3 and ESP32-P4 microcontrollers. It transforms a single board into a versatile industrial communication hub, data logger, automation controller, or IoT gateway — configured entirely through compile-time feature flags.

## Architecture

The firmware is organized around a pack-based module system. Each pack defines a personality (e.g., serial hub, CNC sender, Modbus gateway) by enabling the relevant feature modules. Because modules are independent, the same codebase can produce firmware for completely different use cases without bloat.

```
user_config.h
  └─ pack_selector.h ──► pack_*.h ──► feature flags
                                        ├─ Serial (RS232, parallel, sniffing)
                                        ├─ Networking (WiFi, Ethernet, mDNS, NTP)
                                        ├─ Industrial (Modbus, CAN, I/O expander)
                                        ├─ Storage (SD card, LittleFS, PSRAM FS)
                                        ├─ Web UI (monitoring, config, scheduler)
                                        ├─ Email (SMTP, webhook alerts)
                                        ├─ Audio (I2S DAC, MP3, radio)
                                        ├─ Camera (DVP, MIPI CSI-2)
                                        ├─ CNC (USB Host G-code sender)
                                        ├─ Sensors (environmental, load, power, RTD)
                                        └─ Display (OLED)
```

## Why Node32-HUB?

- **One codebase, many devices** — Build tailored firmware for each board without maintaining separate forks.
- **Optimized footprint** — Only compile the features you actually need.
- **Production-ready** — WDT, crash recovery, PSRAM management, and brownout detection included.
- **Dual-network** — WiFi and wired Ethernet (W5500 or RMII) coexist for redundant connectivity.
- **Web-based management** — Full configuration, live monitoring, file uploads, and event scheduling from any browser.

## Use cases

| Scenario | Enabled modules |
|---|---|
| RS232-to-WiFi bridge / serial data hub | Serial + Networking + Web UI |
| Modbus RTU/TCP gateway with monitoring | Serial + Networking + Industrial + Web UI |
| CNC G-code sender via USB Host | CNC + Display |
| Multi-sensor environmental logger + cloud alerts | Sensors + Storage + Email + Networking |
| Camera station with audio playback | Camera + Audio + Storage + Web UI |
| Industrial automation (8DI / 8DO) | Industrial + Networking + Modbus |

## Feature modules

| Module | Capabilities |
|---|---|
| **Serial** | RS232 communication, parallel port emulation (DB25/Centronics), dual-RX line sniffer |
| **Networking** | WiFi station/AP, W5500 SPI Ethernet, RMII Ethernet (P4), mDNS service discovery, NTP sync |
| **Industrial** | Modbus RTU, Modbus TCP, Modbus slave, CAN bus (TWAI), proximity sensor inputs, digital output expander |
| **Storage** | SD card (SDMMC), LittleFS internal filesystem, PSRAM-backed virtual filesystem |
| **Web UI** | Real-time dashboards, full configuration editor, file manager, event scheduler, OTA updates |
| **Email** | SMTP client with TLS, Google Apps Script webhook integration, SD card photo attachments |
| **Audio** | UDA1334A I2S DAC, MP3 decoding (libhelix), internet radio streaming |
| **Camera** | OV2640/OV3660/OV5640 via DVP (S3) or MIPI CSI-2 (P4), snapshot and live view |
| **CNC** | USB Host CDC driver, G-code buffered sender for GRBL-based controllers |
| **Sensors** | AHT20+BMP280 (T/RH/P), MQ135 (air quality), HX711 (load cell), INA226 (current/voltage), MAX31865 (PT100/PT1000), analog sound level |
| **Display** | SSD1306/SSD1309 OLED via I2C, data visualization and status screens |

## Releases

Pre-built firmware binaries are published in dedicated per-board repositories, each with setup guides and usage instructions.

| Repository | Description |
|---|---|
| *Coming soon* | First binary release with serial hub and Modbus gateway profiles |

## Repository structure

| Path | Purpose |
|---|---|
| `Node32-HUB` (this repo) | Public documentation and binary release links |
| `Node32-HUB-MAIN` | Private source code repository |
