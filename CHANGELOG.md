# Changelog

All notable changes to this project are documented here, in English.

## [Unreleased]

- Releases now carry two assets: the build folder exactly as `./build_firmware.sh` produces it (firmware, bootloader/partitions, flash instructions, licence notices) and, separately, the LGPL compliance bundle (sources + relinkable objects + licences). The bundle is no longer packed inside the firmware zip, and the workflow refuses to publish if it is missing.
- Release automation: manual `Release firmware` workflow (`.github/workflows/release.yml`) builds one pack variant, packages the resulting build folder and publishes the zip to the variant's target repo; pack→repo mapping in `.github/release_targets.json` (pilot: `CAM_S3` → Cam-M). The workflow verifies the OTA signature of the packaged zip and refuses to publish an image the device would reject.
- `THIRD_PARTY_NOTICES`: ESP_Video 3.3.11 → 3.3.12, matching the framework actually linked by the `esp32p4cam` platform (`SBOM.md` already said 3.3.12). Caught by the new strict compliance check on the first run.
- Release notes are English-only: the generator lists only conventional commits (`type(scope): text`) and reports how many legacy/free-form subjects it omitted; an invalid `--since` now fails instead of producing empty notes.
- `./release.sh <variant>` now preflights before spending CI time: clean tree, `HEAD` pushed, and no existing release tag in the target repo (`--bump X.YYY` bumps both version sources and pushes in one command).
- CI flags SBOM/notices drift on every push (`validate_third_party.py --strict` after each matrix build), instead of only on release day.
- Native tests now report their real result: `run_native_tests.py` propagates the exit code, so a failing suite fails the build/release gate instead of printing `FAIL` under a green tick.
- Fresh clones can build the native suite again: the three suites that compile the real `ConfigManager` skip cleanly when the git-ignored `.pio` libdeps are absent (they run once dependencies are installed).
- Dependency pin: `Adafruit GFX Library` 1.12.6 → **1.13.0**. The old pin was silently ignored (a transitive `>= 1.11.11` requirement made PlatformIO install 1.13.0 and keep 1.12.6 as an unused copy), so fresh builds already linked 1.13.0; the pin, `SBOM.md` and `THIRD_PARTY_NOTICES` now match reality.
- Public release cleanup: hardcoded LAN IPs and default test credentials removed from HW scripts and test docs; agent instructions untracked; build artifacts untracked; GitHub source archives carry the full tree.
