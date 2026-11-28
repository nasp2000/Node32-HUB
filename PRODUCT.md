# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

Static HTML + CSS in `site/`, published to GitHub Pages by a GitHub Actions workflow.
No build step, no framework, no client-side dependency beyond inline CSS/SVG.

## Users

Two layers of the same visitor:

1. **Maker / installer (primary).** Bought or is considering one board, wants to know
   in ten seconds whether this firmware does what they need, then download the zip and
   flash it. Works from a laptop, often on a bench, sometimes on a phone.
2. **Integrator (secondary).** Needs the facts the maker skips: exact board, pinout,
   buses, protocols, storage, limits, licensing and how to rebuild from source.

## Product Purpose

One firmware family for ESP32-S3 and ESP32-P4 boards — **Node32-HUB** — with each
*pack* turning the same codebase into one dedicated product (camera, pigeon-clock
terminal, internet radio, G-code sender, OEE terminal…). The site is the single index:
what each pack does, which board it needs, and the download for its current release.

Success: a visitor lands on the index, recognises their use case, and reaches a
downloaded, flashable binary without opening a repository.

## Positioning

All packs share one codebase, one version stream and one release channel, and each
release ships the exact source commit plus the LGPL compliance bundle next to the
binary. The web UI runs entirely in the browser served by the device: no cloud, no
account, no app.

## Operating Context

- Firmware is flashed over USB (WebFlasher / esptool) or updated over the air at
  `/ota`; images are RSA-signed and verified on boot.
- Configuration happens on the device's own web pages, on the local network.
- Builds are reproducible from a pinned toolchain and pinned libraries; no internet
  service is involved at runtime.
- Distributing a binary implies LGPL obligations, already satisfied per release by a
  separate `_third-party.zip` asset.

## Capabilities and Constraints

- Five packs have prebuilt releases (Cam-M S3, Rambox32, Harmonica Box,
  GcodeSender P4, Node32-OEE). Others exist in the repository without a public build.
- Two variants of one product can share a repository and differ only by board
  (Cam-M: ESP32-S3 now, ESP32-P4 later; GcodeSender: P4 and S3).
- One global firmware version; each pack gets its own release tag
  (`vX.YYY-<pack>`).
- English only. Static pages: fast, no tracking, no cookies, no external fonts.

## Brand Commitments

- Name **Node32-HUB** for the family; each pack keeps its own product name
  (Cam-M, Rambox32, Harmonica Box, GcodeSender, Node32-OEE).
- Voice: technical, direct, no marketing superlatives; say what it does and what it
  needs.
- MIT for first-party code; third-party licences listed per release.

## Evidence on Hand

- Real screenshots and screen recordings per pack (`REPOS/*/image/`).
- Release history, SBOM and third-party notices in this repository.
