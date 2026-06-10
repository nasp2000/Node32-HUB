# Node32-HUB

**Node32-HUB** is a modular ESP32 firmware framework. By enabling or disabling feature flags, the same codebase can act as an RS232 data hub, a printer emulator, a Modbus gateway, a GRBL CNC sender, a sensor aggregator, a camera station, an email alert system — or any combination of these.

## Releases

Pre-built firmware binaries for supported boards are available in dedicated repositories, each with setup and usage instructions.

| Repository | Description |
|---|---|
| *Coming soon* | |

## Supported hardware

ESP32-S3 and ESP32-P4 based boards with PSRAM.

## Feature modules (selectable)

- **Serial**: RS232, parallel port emulation, dual-RX sniffer
- **Networking**: WiFi, Ethernet, mDNS, NTP
- **Industrial**: Modbus RTU/TCP/Slave, CAN bus, proximity inputs, digital outputs
- **Storage**: SD card, LittleFS, PSRAM virtual filesystem
- **Web UI**: Configuration, monitoring, file manager, scheduler
- **Email**: SMTP and webhook-based alerts with storage attachments
- **Audio**: I2S DAC, MP3 decoding, internet radio
- **Camera**: DVP and MIPI CSI-2 camera support
- **CNC**: USB Host G-code sender for GRBL controllers
- **Sensors**: Temperature, humidity, air quality, load cell, current/voltage, PT100/PT1000, sound level
- **Display**: SSD1306/SSD1309 OLED
