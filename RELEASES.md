# Releases — source anchors

Every published firmware maps to an exact commit of this repository. That commit is
the *corresponding source* for the binaries shipped in the per-board release repos,
and the anchor used to bound the release notes of the next version.

| Version | Variant | Commit | Date | Published in |
|---|---|---|---|---|
| v1.120 | CAM_S3 | `0428e0d` | 2026-10-30 | [Cam-M](https://github.com/nasp2000/Cam-M/releases/tag/v1.120) |
| v1.120 | RAMBOX | `14576f6` | 2026-10-30 | [Rambox32](https://github.com/nasp2000/Rambox32/releases/tag/v1.120) |
| v1.120 | RADIO | `14576f6` | 2026-10-30 | [Harmonica-BOX](https://github.com/nasp2000/Harmonica-BOX/releases/tag/v1.120) |
| v1.120 | P4_CNC | `14576f6` | 2026-10-30 | [GcodeSender](https://github.com/nasp2000/GcodeSender/releases/tag/v1.120) |

## Getting the source for a release

```bash
# any commit above, no tag needed
curl -L -o source.zip https://github.com/nasp2000/Node32-HUB/archive/<commit>.zip
```

Every release body also ends with `built from <commit>`, so the mapping is never lost
if this table goes stale.

## Rebuilding a firmware from it

```bash
unzip source.zip && cd Node32-HUB-<commit>
./build_firmware.sh -e <env> -p <pack>
```

| Variant | Command |
|---|---|
| CAM_S3 | `./build_firmware.sh -e n16r8 -p CAM` |
| RAMBOX | `./build_firmware.sh -e n16r8 -p RAMBOX` |
| RADIO | `./build_firmware.sh -e n16r8 -p RADIO` |
| P4_CNC | `./build_firmware.sh -e esp32p4 -p P4_CNC` |

Two caveats: the private OTA key (`chatgpt/ota_private.pem`) is never published, so a
rebuild produces an **unsigned** image — flashable over USB, rejected by OTA; and the
build embeds its own date/time, so the bytes differ from the published binary even
though the source is identical (platform and library versions are pinned).

Add a row here when a release is published.
