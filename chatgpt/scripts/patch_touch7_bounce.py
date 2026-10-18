"""
Pre-build patch (s3_touch7 env ONLY): tune the RGB panel configuration in the
pinned ESP32_Display_Panel lib.

TWO knobs, both in BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7.h:

1. BOUNCE BUFFER = ON (ESP_PANEL_BOARD_WIDTH * 10).
   The RGB GDMA streams from an internal-SRAM bounce buffer instead of reading
   the PSRAM framebuffers directly. DMA-direct reading PSRAM at 16 MHz underruns
   while the CPU/LVGL writes the same PSRAM -> shaken image / glitches.
   COST: the bounce ISR memcpy's PSRAM on every chunk, so ANY flash op while
   the panel streams panics (S3 SPI1 shares flash+PSRAM; cache suspend). All
   flash ops must therefore stop the panel first (Touch7PanelHold del/rebuild).

2. PCLK = 16 MHz. With the bounce buffer the GDMA does not read PSRAM, so full
   speed is fine (tuning history: 16 underran on DMA-direct, 13.5 was the sweet
   spot there; 12 lost the panel lock -> test pattern).

Idempotent: converts the OFF/vendor line to the ON line, then leaves it. Only
the exact pinned lines are replaced; anything else aborts loudly.

Never call sys.exit(0) here (SCons treats it as "stop now" and skips the build).
"""
import os
import re
import sys

REL = os.path.join(
    "src", "board", "supported", "waveshare",
    "BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7.h",
)

ON_LINE = ("#define ESP_PANEL_BOARD_LCD_RGB_BOUNCE_BUF_SIZE (ESP_PANEL_BOARD_WIDTH * 10) "
           "// TOUCH7-BOUNCE-ON: GDMA streams from SRAM (no PSRAM underrun)")
OFF_LINE = ("#define ESP_PANEL_BOARD_LCD_RGB_BOUNCE_BUF_SIZE (0) "
            "// TOUCH7-BOUNCE-OFF: DMA-direct, zero ISR PSRAM copy (flash-race fix)")
VENDOR_LINE = "#define ESP_PANEL_BOARD_LCD_RGB_BOUNCE_BUF_SIZE (ESP_PANEL_BOARD_WIDTH * 10)"

# Bounce ON by default: the GDMA streams from SRAM (no PSRAM underrun). The
# bounce ISR memcpy's PSRAM, so EVERY flash op while streaming must stop the
# panel first (Touch7PanelHold del/rebuild) — enforced in the code. Set
# T7_BOUNCE_ON=0 to fall back to DMA-direct (needs PCLK ~13.5 MHz).
BOUNCE_ON = os.environ.get("T7_BOUNCE_ON", "1").strip().lower() not in ("0", "false", "no", "off")

CLK_RE = re.compile(
    r"#define\s+ESP_PANEL_BOARD_LCD_RGB_CLK_HZ\s+\([^)]*\)[^\n]*")
CLK_135 = ("#define ESP_PANEL_BOARD_LCD_RGB_CLK_HZ          (13500000) "
           "// TOUCH7-PCLK: 13.5 MHz (bounce off: 16 underran, 15 trembled, 12 lost lock)")
CLK_16 = ("#define ESP_PANEL_BOARD_LCD_RGB_CLK_HZ          (16 * 1000 * 1000) "
          "// TOUCH7-PCLK: 16 MHz (bounce on: GDMA reads SRAM)")


def find_header():
    cands = []
    env_dir = os.environ.get("PLATFORMIO_LIBDEPS_DIR")
    if env_dir:
        cands.append(os.path.join(env_dir, "s3_touch7", "ESP32_Display_Panel", REL))
    try:
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    except NameError:
        root = os.getcwd()
    for tree in (".pio-linux", ".pio"):
        cands.append(os.path.join(root, tree, "libdeps", "s3_touch7",
                                  "ESP32_Display_Panel", REL))
    for c in cands:
        if os.path.isfile(c):
            return c
    return None


def main():
    path = find_header()
    if not path:
        print("[patch_touch7_bounce] lib header not installed yet, skipping "
              "(will apply once PlatformIO downloads the lib)")
        return 0
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()

    changed = False
    desired = ON_LINE if BOUNCE_ON else OFF_LINE
    if desired in src:
        pass
    elif ON_LINE in src:
        src = src.replace(ON_LINE, desired, 1)
        changed = True
    elif OFF_LINE in src:
        src = src.replace(OFF_LINE, desired, 1)
        changed = True
    elif VENDOR_LINE in src:
        src = src.replace(VENDOR_LINE, desired, 1)
        changed = True
    else:
        print("[patch_touch7_bounce] ERROR: bounce line not found in %s "
              "(lib version drift? refusing to patch)" % path)
        return 1

    clk_new = CLK_16 if BOUNCE_ON else CLK_135
    src, n = CLK_RE.subn(clk_new, src, count=1)
    if n == 0:
        print("[patch_touch7_bounce] WARN: CLK line not found, PCLK left as-is")
    else:
        changed = True

    if changed:
        with open(path, "w", encoding="utf-8") as f:
            f.write(src)
        print("[patch_touch7_bounce] applied (bounce %s): %s"
              % ("ON + PCLK 16 MHz" if BOUNCE_ON else "OFF + PCLK 13.5 MHz", path))
    else:
        print("[patch_touch7_bounce] already applied: %s" % path)
    return 0


if main() != 0:
    raise SystemExit(1)
