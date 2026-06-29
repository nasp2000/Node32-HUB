# Node32-HUB

**Node32-HUB** is a modular, multi-protocol firmware framework for ESP32-S3 and ESP32-P4 microcontrollers. It turns a single board into a versatile industrial communication hub, data logger, automation controller, or IoT gateway. Thanks to compile-time feature flags, it can be tailored to fit absolutely any use case.

## Architecture

The firmware is organized around a **pack-based module system**, where each "pack" defines a specific device personality by enabling relevant features. Because these modules are independent, a single codebase can generate firmware for completely different use cases, allowing you to mix and match features in endless configurations. 

## Why Node32-HUB?

* **One codebase, many devices** — Build tailored firmware for each board without maintaining separate forks.
* **Optimized footprint** — Only compile the specific features and modules your hardware actually needs.
* **Production-ready & Fail-safe** — Built-in WDT, crash recovery, PSRAM management, and brownout detection.
* **Dual-network redundancy** — Seamless coexistence of Wi-Fi and wired Ethernet for redundant connectivity.
* **Web-based management** — Full configuration, live monitoring, file uploads, and event scheduling directly from any browser.

## Feature Modules

* **Serial:** RS232, parallel port emulation, and dual-RX line sniffing.
* **Networking:** Wi-Fi, Ethernet, mDNS, and NTP.
* **Industrial:** Modbus RTU/TCP/Slave, CAN bus, proximity inputs, and digital outputs.
* **Storage:** SD card, LittleFS, and PSRAM-backed virtual filesystem.
* **Web UI:** Real-time dashboards, configuration portal, file manager, task scheduler, and OTA updates.
* **Alerts & Notifications:** SMTP email and webhook-based alerts with storage attachments.
* **Audio:** I2S DAC, MP3 hardware decoding, and internet radio streaming.
* **Camera:** DVP and MIPI CSI-2 camera interface support.
* **CNC & Automation:** USB Host G-code sender for GRBL controllers.
* **Sensors:** Temperature, humidity, pressure, air quality, load cells, current/voltage, RTD, and sound level monitoring.
* **Display:** OLED data visualization and status screens.

## Releases

Pre-built firmware binaries are published in dedicated per-board repositories, each with setup guides and usage instructions.

| Repository | Description |
|---|---|
| [**GcodeSender**](https://github.com/nasp2000/GcodeSender) | Standalone G-code sender para ESP32-P4. USB Host, web UI com processamento local, streaming SD + PSRAM. |
| *Coming soon* |  |
