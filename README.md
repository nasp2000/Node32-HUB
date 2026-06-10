# Node32-HUB

**Node32-HUB** is a modular, multi-protocol firmware framework for ESP32-S3 and ESP32-P4 microcontrollers. It transforms a single board into a versatile industrial communication hub, data logger, automation controller, or IoT gateway — configured entirely through compile-time feature flags.

## Architecture

The firmware is organized around a pack-based module system. Each pack defines a personality by enabling the relevant feature modules. Because modules are independent, the same codebase can produce firmware for completely different use cases without bloat.

## Why Node32-HUB?

- **One codebase, many devices** — Build tailored firmware for each board without maintaining separate forks
- **Optimized footprint** — Only compile the features you actually need
- **Production-ready** — WDT, crash recovery, PSRAM management, and brownout detection included
- **Dual-network** — WiFi and wired Ethernet coexist for redundant connectivity
- **Web-based management** — Full configuration, live monitoring, file uploads, and event scheduling from any browser

## Feature modules

- **Serial**: RS232, parallel port emulation, dual-RX line sniffer
- **Networking**: WiFi, Ethernet, mDNS, NTP
- **Industrial**: Modbus RTU/TCP/Slave, CAN bus, proximity inputs, digital outputs
- **Storage**: SD card, LittleFS, PSRAM-backed virtual filesystem
- **Web UI**: Real-time dashboards, configuration, file manager, scheduler, OTA updates
- **Email**: SMTP and webhook-based alerts with storage attachments
- **Audio**: I2S DAC, MP3 decoding, internet radio
- **Camera**: DVP and MIPI CSI-2 camera support
- **CNC**: USB Host G-code sender for GRBL controllers
- **Sensors**: Temperature, humidity, pressure, air quality, load cell, current, voltage, RTD, sound level
- **Display**: OLED data visualization and status screens

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
