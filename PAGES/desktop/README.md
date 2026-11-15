# Desktop pages — reference screenshots (to fill)

Same convention as `PAGES/mobile/`, applied to the **desktop** pages later:
**exactly one image per page, always the most recent**, stored here and versioned.

> Status: **structure ready, images pending.** The mobile set is already complete
> in `PAGES/mobile/`; this folder is the place for the desktop ones when we do the
> same pass for the PC pages.

---

## 1. Rules (identical to the mobile set)

- **One image per page**, named after the route in snake_case: `grbl_controller.png`,
  `camera.png`, `status.png`, …
- **Always the latest**: any change that alters a page (layout, buttons, text,
  state) must refresh its image **in the same change**.
- Docs stay in `PAGES/<page>.md`; the image is referenced from there and indexed in
  the table below.
- **Never** a screenshot of the phone page here — those live in `PAGES/mobile/`
  (`/m` is the phone default; the desktop page is the `?desk=1` / PC view).

## 2. How to capture (desktop)

| Item | Value |
|---|---|
| Viewport | **1440 × 900** (full page if the page scrolls) |
| UI language | English (`ui_lang_cache` cleared / `?lang` default) |
| Data | realistic mock or the bench device — no personal data |
| Path | `PAGES/desktop/<page>.png` |
| Name | route snake_case, one file per page (no `_v2`, no dates) |

## 3. Index (fill as we go)

Legend: **pending** = image not taken yet · **done** = image present here.

### Core
| Page | Route | Image | Status |
|---|---|---|---|
| Home | `/` | `home.png` | pending |
| Status | `/status` | `status.png` | pending |
| Files | `/files` | `files.png` | pending |
| SD card | `/sdcard` | `sdcard.png` | pending |
| Monitor | `/monitor` | `monitor.png` | pending |
| Access | `/access` | `access.png` | pending |
| OTA | `/ota` | `ota.png` | pending |

### CNC (GcodeSender)
| Page | Route | Image | Status |
|---|---|---|---|
| GRBL controller | `/grbl-controller?desk=1` | `grbl_controller.png` | pending |

### Camera
| Page | Route | Image | Status |
|---|---|---|---|
| Camera | `/camera?desk=1` | `camera.png` | pending |

### Rambox32
| Page | Route | Image | Status |
|---|---|---|---|
| Rambox (clock) | `/rambox?desk=1` | `rambox.png` | pending |
| RS232 | `/rs232` | `rs232.png` | pending |

### Remaining packs / pages
The full list lives in `PAGES/WEB_PAGES_REFERENCE.md` — every row that is not a
`/m` page gets one image here when its page is worked on (alerts, sensors, charts,
Modbus, CAN, radio, OEE/proximity, USB server, FTP, …).

## 4. Adding an image (checklist)

1. Capture at 1440×900 with the device/mock loaded and the page fully rendered.
2. Save as `PAGES/desktop/<page>.png` (exactly one file for that page).
3. Reference it in `PAGES/<page>.md` (first line of the doc, like the mobile docs).
4. Mark the row **done** in §3; if the page is new, add a row.
5. Same commit as the page change that made the image necessary.
