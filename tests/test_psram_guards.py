"""PSRAM compile-time guard validation.

Tests that the #error / #warning directives in user_config.h's PSRAM
dependency section are correctly structured and trigger on the right
flag combinations.  Pure-Python tests — no firmware build required.
"""

import os
import re
import pytest

PROJECT_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
USER_CONFIG_PATH = os.path.join(PROJECT_ROOT, "chatgpt", "user_config.h")


def read_guard_section():
    """Return only the PSRAM dependency guard section as a string."""
    with open(USER_CONFIG_PATH, "r", encoding="utf-8") as f:
        content = f.read().replace("\r\n", "\n")
    # Find the guard section
    start_m = re.search(r"// PSRAM dependency guards\n// -+\n", content)
    assert start_m, "PSRAM guard section header not found"
    start = start_m.end()
    # Find the next section boundary (----)
    end_m = re.search(r"\n// -{5,}", content[start:])
    assert end_m, "End of PSRAM guard section not found"
    return content[start:start + end_m.start()]


GUARD_SECTION = read_guard_section()


def parse_guard_blocks(section):
    """Yield (name, [inner_if_conditions, ...], directive_type) for each block."""
    pattern = re.compile(
        r"// (.+?)\n"
        r"#if (.+?)\n"
        r"(?:  #if (.+?)\n)?"
        r"\s*#(error|warning) (.+?)\n"
        r"\s*#endif\n"
        r"\s*#endif",
        re.MULTILINE
    )
    for m in pattern.finditer(section):
        raw_name = m.group(1).rstrip(".")
        name = raw_name
        for keyword in ["Camera", "GRBL", "USB/IP server", "Sensor history"]:
            if keyword in raw_name:
                name = keyword
                break
        outer_if = m.group(2).strip()
        inner_if = m.group(3)
        inner_if = inner_if.strip() if inner_if else ""
        directive_type = m.group(4)
        directive_text = m.group(5)
        dtype = directive_type  # "error" or "warning"
        yield (name, (outer_if, inner_if), dtype, directive_text)


GUARD_BLOCKS = list(parse_guard_blocks(GUARD_SECTION))


def eval_cond(expr, flags):
    """Evaluate a simplified preprocessor #if expression."""
    s = expr
    # Step 1: Handle defined() first (before value substitution)
    s = re.sub(r"defined\((\w+)\)",
               lambda m: "1" if m.group(1) in flags else "0", s)
    # Step 2: Replace known identifiers with their values
    for k, v in flags.items():
        s = s.replace(k, str(v))
    # Step 3: Map operators
    s = s.replace("||", " or ").replace("&&", " and ")
    s = s.replace("==", " == ")
    s = s.replace("!", " not ")
    # Step 4: Remove any remaining identifiers (undefined = 0)
    s = re.sub(r"\b[A-Z_][A-Z_0-9]+\b", "0", s)
    try:
        return bool(eval(s, {"__builtins__": {}}, {}))
    except Exception:
        return False


def simulate_block(outer_if, inner_if, dtype, flags):
    """Return True if the directive fires for the given flags."""
    if not eval_cond(outer_if, flags):
        return False
    if inner_if and not eval_cond(inner_if, flags):
        return False
    return True


# ---- structure tests --------------------------------------------------------

class TestPsramGuardStructure:
    def test_guard_section_exists(self):
        assert len(GUARD_SECTION) > 0

    def test_all_expected_modules_guarded(self):
        guarded = {b[0] for b in GUARD_BLOCKS}
        expected = {"Camera", "GRBL", "USB/IP server", "Sensor history"}
        missing = expected - guarded
        assert not missing, f"Missing PSRAM guards: {missing}"

    def test_each_guard_uses_board_has_psram(self):
        for name, (outer, inner), dtype, directive in GUARD_BLOCKS:
            combined = f"{outer} {inner}"
            assert "BOARD_HAS_PSRAM" in combined, (
                f"Guard '{name}' does not check BOARD_HAS_PSRAM"
            )

    def test_no_empty_guards(self):
        for name, conds, dtype, directive in GUARD_BLOCKS:
            assert '"' in directive, f"Guard '{name}' has empty message"

    def test_four_guards_found(self):
        assert len(GUARD_BLOCKS) == 4, f"Expected 4 guards, got {len(GUARD_BLOCKS)}"

    def test_two_errors_two_warnings(self):
        errors = [b for b in GUARD_BLOCKS if b[2] == "error"]
        warnings = [b for b in GUARD_BLOCKS if b[2] == "warning"]
        assert len(errors) == 2, f"Expected 2 #error, got {len(errors)}"
        assert len(warnings) == 2, f"Expected 2 #warning, got {len(warnings)}"


# ---- logic tests ------------------------------------------------------------

class TestPsramGuardLogic:
    def _block(self, name):
        for n, conds, dtype, directive in GUARD_BLOCKS:
            if n == name:
                return (conds[0], conds[1], dtype)
        return None

    # --- Camera (#error) ---
    def test_camera_without_psram_errors(self):
        outer, inner, dtype = self._block("Camera")
        assert simulate_block(outer, inner, dtype, {"ENABLE_CAMERA": 1, "BOARD_HAS_PSRAM": 0})
        assert dtype == "error"

    def test_camera_with_psram_ok(self):
        outer, inner, dtype = self._block("Camera")
        assert not simulate_block(outer, inner, dtype, {"ENABLE_CAMERA": 1, "BOARD_HAS_PSRAM": 1})

    def test_camera_disabled_ok(self):
        outer, inner, dtype = self._block("Camera")
        assert not simulate_block(outer, inner, dtype, {"ENABLE_CAMERA": 0, "BOARD_HAS_PSRAM": 0})

    # --- USB/IP server (#error) ---
    def test_usbip_without_psram_errors(self):
        outer, inner, dtype = self._block("USB/IP server")
        assert simulate_block(outer, inner, dtype, {"ENABLE_USBIP_SERVER": 1, "BOARD_HAS_PSRAM": 0})
        assert dtype == "error"

    def test_usbip_with_psram_ok(self):
        outer, inner, dtype = self._block("USB/IP server")
        assert not simulate_block(outer, inner, dtype, {"ENABLE_USBIP_SERVER": 1, "BOARD_HAS_PSRAM": 1})

    # --- GRBL (#warning) ---
    def test_grbl_without_psram_warns(self):
        outer, inner, dtype = self._block("GRBL")
        assert simulate_block(outer, inner, dtype, {"ENABLE_GRBL": 1, "BOARD_HAS_PSRAM": 0})
        assert dtype == "warning"

    def test_grbl_with_psram_ok(self):
        outer, inner, dtype = self._block("GRBL")
        assert not simulate_block(outer, inner, dtype, {"ENABLE_GRBL": 1, "BOARD_HAS_PSRAM": 1})

    # --- Sensor history (#warning, multiple outer conditions) ---
    def test_sensor_history_without_psram_warns(self):
        outer, inner, dtype = self._block("Sensor history")
        for flag in ["ENABLE_AHT20_BMP280", "ENABLE_RS232_CHARTS", "ENABLE_MQ135"]:
            assert simulate_block(outer, inner, dtype, {flag: 1, "BOARD_HAS_PSRAM": 0}), f"{flag} should fire"
        assert dtype == "warning"

    def test_sensor_history_with_psram_ok(self):
        outer, inner, dtype = self._block("Sensor history")
        for flag in ["ENABLE_AHT20_BMP280", "ENABLE_RS232_CHARTS", "ENABLE_MQ135"]:
            assert not simulate_block(outer, inner, dtype, {flag: 1, "BOARD_HAS_PSRAM": 1}), f"{flag}+PSRAM should not fire"
