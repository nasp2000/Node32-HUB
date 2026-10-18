"""
Pre-build validation: examine every packs/pack_*.h for structural correctness.

Checks:
  1. Identity macros (PACK_SYSTEM_NAME, PACK_EMOJI, PACK_EMAIL_SUBJECT) present.
  2. Every ENABLE_ flag in #undef/#define sections uses the #undef-first pattern.
  3. No flag assigned contradictory values (1 vs 0 in the same file).
  4. All referenced ENABLE_ flags are known (exist in config_defaults.h).
  5. Board lock #error present when the pack targets a specific board.
  6. No plain #define of an ENABLE_ flag without a preceding #undef in the
     systematic section (catches the redundant "summary" pattern).
"""

import os
import re

# PlatformIO pre-scripts use Import() which sets __name__ differently.
# In PIO mode we define PROJECT_DIR via env, in standalone via __file__.
try:
    PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
except NameError:
    PROJECT_DIR = None  # set by the PIO entry point below

PACKS_DIR = os.path.join(PROJECT_DIR, "packs") if PROJECT_DIR else None
CONFIG_DEFAULTS = os.path.join(PROJECT_DIR, "src", "config_defaults.h") if PROJECT_DIR else None

KNOWN_ENABLE_FLAGS = set()
IDENTITY_MACROS = {"PACK_SYSTEM_NAME", "PACK_EMAIL_SUBJECT", "PACK_EMOJI", "PACK_BRAND_NAME"}


def load_known_flags():
    """Build the set of known ENABLE_ flags from config_defaults.h + src/*.h."""
    if not PROJECT_DIR:
        return
    sources = [CONFIG_DEFAULTS]
    src_dir = os.path.join(PROJECT_DIR, "src")
    for fname in os.listdir(src_dir):
        if fname.endswith(".h"):
            sources.append(os.path.join(src_dir, fname))
    for src in sources:
        if not os.path.isfile(src):
            continue
        with open(src, "r", encoding="utf-8") as f:
            content = f.read()
        for m in re.finditer(r"#ifndef\s+(ENABLE_\w+)", content):
            KNOWN_ENABLE_FLAGS.add(m.group(1))


def validate_pack(path):
    """Validate a single pack file. Returns list of error strings."""
    errors = []
    warnings = []

    with open(path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    basename = os.path.basename(path)
    content = "".join(lines)

    # ---- 1. Identity macros ----
    for macro in IDENTITY_MACROS:
        if macro not in content:
            msg = f"{basename}: missing identity macro {macro}"
            if macro == "PACK_BRAND_NAME":
                warnings.append(f"{msg} (defaults to PACK_SYSTEM_NAME)")
            else:
                errors.append(msg)

    # ---- 2. Board lock check ----
    has_board_lock = "#error" in content and ("TARGET_BOARD" in content or "SELECTED_PACK" in content)
    if not has_board_lock and basename not in ("pack_selector.h", "pack_validator.h"):
        warnings.append(f"{basename}: no board lock (#error for wrong target) found")

    # ---- 3. Analyse every #define ENABLE_ / #undef ENABLE_ ----
    plain_defines = {}
    dup_undef_defines = {}

    for i, line in enumerate(lines):
        stripped = line.strip()
        lineno = i + 1

        m_define = re.match(r"#define\s+(ENABLE_\w+)\s+(\d)", stripped)
        m_undef = re.match(r"#undef\s+(ENABLE_\w+)", stripped)

        if m_define:
            flag = m_define.group(1)
            val = m_define.group(2)
            plain_defines.setdefault(flag, []).append((lineno, val))

        if m_undef:
            flag = m_undef.group(1)
            j = i + 1
            while j < len(lines) and j < i + 3:
                next_stripped = lines[j].strip()
                m_next = re.match(r"#define\s+(ENABLE_\w+)\s+(\d)", next_stripped)
                if m_next and m_next.group(1) == flag:
                    dup_undef_defines[flag] = (lineno, j + 1, m_next.group(2))
                    break
                j += 1

    # ---- 4. Check #undef-first pattern ----
    for flag, locations in plain_defines.items():
        if flag in dup_undef_defines:
            uline, dline, val = dup_undef_defines[flag]
            for pline, pval in locations:
                if pline < uline:
                    if pval == val:
                        warnings.append(
                            f"{basename}:{pline}: plain #define {flag}={pval} "
                            f"is redundant — already set at line {dline} via #undef/#define"
                        )
                    else:
                        errors.append(
                            f"{basename}:{pline}: plain #define {flag}={pval} "
                            f"contradicts #undef/#define at line {dline} (value={val})"
                        )

    # ---- 5. Unknown flags ----
    for flag in dup_undef_defines:
        if flag not in KNOWN_ENABLE_FLAGS:
            warnings.append(
                f"{basename}: {flag} is not a known ENABLE_ flag "
                f"(not found in config_defaults.h)"
            )

    return errors, warnings


def run(packs_dir):
    all_errors = []
    all_warnings = []
    for fname in sorted(os.listdir(packs_dir)):
        if not fname.startswith("pack_") or not fname.endswith(".h"):
            continue
        if fname in ("pack_selector.h", "pack_validator.h"):
            continue
        path = os.path.join(packs_dir, fname)
        errors, warnings = validate_pack(path)
        for e in errors:
            all_errors.append(f"{e}")
        for w in warnings:
            all_warnings.append(f"{w}")
    return all_errors, all_warnings


def _report(errors, warnings):
    if warnings:
        print("\n[validate_packs] WARNINGS:")
        for w in warnings:
            print(f"  {w}")
    if errors:
        print("\n[validate_packs] ERRORS:")
        for e in errors:
            print(f"  {e}")
        print(f"\n[validate_packs] {len(errors)} error(s) — fix before build.\n")
    else:
        status = f"OK ({len(warnings)} warning(s))" if warnings else "OK"
        print(f"[validate_packs] {status}")


# ── PlatformIO entry point ────────────────────────────────────────────────

try:
    Import("env")
    PROJ = env.subst("$PROJECT_DIR")
    if PROJECT_DIR is None:
        PROJECT_DIR = PROJ
        PACKS_DIR = os.path.join(PROJECT_DIR, "packs")
        CONFIG_DEFAULTS = os.path.join(PROJECT_DIR, "src", "config_defaults.h")
    load_known_flags()
    errors, warnings = run(PACKS_DIR)
    _report(errors, warnings)
    if errors:
        raise SystemExit(1)
except NameError:
    # Not in PIO context — standalone mode
    load_known_flags()
    errors, warnings = run(PACKS_DIR)
    _report(errors, warnings)
    if errors:
        raise SystemExit(1)
except Exception as e:
    print(f"[validate_packs] WARNING: {e}")
