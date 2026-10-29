# Changelog

All notable changes to this project are documented here, in English.

## [Unreleased]

- Release automation: manual `Release firmware` workflow (`.github/workflows/release.yml`) builds one pack variant, packages the compliance bundle and publishes the zip to the variant's target repo; pack→repo mapping in `.github/release_targets.json` (pilot: `CAM_S3` → Cam-M).
- Public release cleanup: hardcoded LAN IPs and default test credentials removed from HW scripts and test docs; agent instructions untracked; build artifacts untracked; GitHub source archives carry the full tree.
