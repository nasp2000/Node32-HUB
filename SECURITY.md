# Security — Node32-HUB

## 1. Threat Model

- Device operates on **local LAN only** (WiFi + Ethernet). No WAN-facing services.
- **No HTTPS** — TLS is not feasible on ESP32-S3/P4 given memory constraints (~320 KB RAM usable).
- HTTP **Basic Auth** only — credentials are Base64-encoded, trivially reversible.
- **Physical access = full compromise**: serial console, flash read via esptool, direct GPIO manipulation.
- Attack vectors:
  - **LAN sniffing** — credentials captured in plaintext over HTTP.
  - **Unauthorized web access** — weak or default passwords.
  - **Malicious OTA** — unsigned firmware if signature check is bypassed.
  - **Physical flash dump** — extract NVS passwords, LittleFS configs, Wi-Fi credentials.

## 2. Current Security Measures

| Measure | Description |
|---|---|
| **HTTP Basic Auth** | All endpoints protected via `ensureBasicAuth()` in handler layer. |
| **OTA signing** | RSA-2048 + SHA-256 signature verification. Chip magic embedded (`S3__`/`P4__`) to prevent cross-flash. Automatic rollback on bad signature. |
| **NVS passwords** | Stored in NVS (plaintext — known limitation, see below). |
| **Telnet** | Disabled by default on production packs (`ENABLE_TELNET=0`). |
| **Feature flags** | Compile out unused modules at `#define` level — zero RAM cost, minimal attack surface. |
| **Memory guard** | `memoryPressurePanic()` before critical allocations prevents heap-exhaustion exploits. |

## 3. Known Limitations

- **NVS passwords are NOT encrypted** — plaintext storage. AES encryption derived from chip unique ID is planned.
- **Weak default passwords**: Telnet `root`/`root`, AP mode password `12345678`. (Web admin ships with empty credentials — set them before exposing the device.)
- **No forced password change** on first boot.
- **No HTTPS/TLS** — all traffic (including credentials) travels in cleartext.
- **No rate limiting** on login attempts — brute-force is feasible on LAN.
- **No session management** — Basic Auth is sent on every request; no token expiry or logout.

## 4. Recommended Deployment Practices

1. **Change ALL default passwords** on first boot — web admin, telnet, AP mode.
2. **Isolate on a dedicated VLAN** — do not expose to general network traffic.
3. **Disable Telnet** — set `#define ENABLE_TELNET 0` in `user_config.h`.
4. **Disable unused modules** via feature flags — fewer attack vectors.
5. **Keep firmware updated** — use signed OTA only; never upload unsigned binaries.
6. **Use WPA2/WPA3** WiFi — never deploy on open or WEP networks.
7. **Monitor uptime** via `/api/health` — unexpected reboots may indicate tampering.

## 5. OTA Security

- Firmware is **RSA-2048 + SHA-256 signed**.
- Sign a binary:
  ```bash
  python scripts/sign_firmware.py --chip S3 firmware.bin
  ```
- Generate a signing key pair:
  ```bash
  python scripts/ota_keygen.py
  ```
- Chip ID check prevents cross-flashing between S3 and P4 variants.
- On signature verification failure the bootloader automatically **rolls back** to the previous slot.

## 6. Reporting Vulnerabilities

Please open an issue at the repository:
[https://github.com/nasp2000/Node32-HUB/issues](https://github.com/nasp2000/Node32-HUB/issues)
