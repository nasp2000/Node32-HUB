# Releases — source anchors

Every published firmware maps to an exact commit of this repository: that commit is the
*corresponding source* for the binaries in the release, and the anchor used to bound the
notes of the next release of the same pack.

Releases live **in this repository**, one per pack and version. The tag carries the pack:
`vX.YYY-<pack>`.

| Version | Pack | Tag | Commit | Date |
|---|---|---|---|---|
| v1.120 | Cam-M (ESP32-S3) | `v1.120-cam-s3` | `76f8c05` | 2026-11-28 |
| v1.120 | Rambox32 | `v1.120-rambox` | `f1a5b4a` | 2026-11-28 |
| v1.120 | Harmonica Box | `v1.120-radio` | `f1a5b4a` | 2026-11-28 |
| v1.120 | GcodeSender (ESP32-P4) | `v1.120-p4cnc` | `f1a5b4a` | 2026-11-28 |
| v1.120 | Node32-OEE (ESP32-S3 8DI8DO) | `v1.120-oee` | `5c3fb39` | 2026-11-28 |

Each release has two assets:

- `<pack>_<env>_<version>_<PACK>_<timestamp>.zip` — the build folder (`firmware.bin`,
  bootloader, partitions, `flash_command.txt`, licence notices).
- `…_third-party.zip` — LGPL sources, relinkable objects and licence texts.

## Getting the source for a release

```bash
# every release body ends with "built from <commit>"
curl -L -o source.zip https://github.com/nasp2000/Node32-HUB/archive/<commit>.zip
```

## Rebuilding a firmware from it

```bash
unzip source.zip && cd Node32-HUB-<commit>
./build_firmware.sh -e <env> -p <pack>
```

| Pack | Command |
|---|---|
| Cam-M (S3) | `./build_firmware.sh -e n16r8 -p CAM` |
| Rambox32 | `./build_firmware.sh -e n16r8 -p RAMBOX` |
| Harmonica Box | `./build_firmware.sh -e n16r8 -p RADIO` |
| GcodeSender (P4) | `./build_firmware.sh -e esp32p4 -p P4_CNC` |

Two caveats: the private OTA key (`chatgpt/ota_private.pem`) is never published, so a
rebuild produces an **unsigned** image — flashable over USB, rejected by OTA; and the build
embeds its own date/time, so the bytes differ from the published binary even though the
source is identical (platform and library versions are pinned).

Add a row here when a release is published.
