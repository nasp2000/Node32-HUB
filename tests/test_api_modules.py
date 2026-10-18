"""Module-specific endpoint tests.

Tests for optional modules (Modbus, CAN, GRBL, etc.) that may or may not be
compiled in.  Uses /api/sensor-flags or endpoint behaviour to skip gracefully.
"""

import pytest
import re


def _enabled(endpoint_name, http):
    """Return True if the endpoint responds 200, else False."""
    try:
        r = http("GET", endpoint_name)
        return r.status_code == 200
    except Exception:
        return False


class TestApiModules:
    def test_modbus_config(self, http):
        if not _enabled("/api/modbus/config", http):
            pytest.skip("Modbus not compiled in or disabled")
        r = http("GET", "/api/modbus/config")
        assert r.status_code == 200
        assert isinstance(r.json(), dict)

    def test_can_status(self, http):
        if not _enabled("/api/can/status", http):
            pytest.skip("CAN not compiled in or disabled")
        r = http("GET", "/api/can/status")
        assert r.status_code == 200
        assert isinstance(r.json(), dict)

    def test_buzzer_config(self, http):
        if not _enabled("/api/buzzer-config", http):
            pytest.skip("Buzzer not compiled in")
        r = http("GET", "/api/buzzer-config")
        assert r.status_code == 200
        assert isinstance(r.json(), dict)

    def test_grbl_status(self, http):
        if not _enabled("/api/grbl/status", http):
            pytest.skip("GRBL not compiled in")
        r = http("GET", "/api/grbl/status")
        assert r.status_code == 200
        assert isinstance(r.json(), dict)

    def test_unikon_status(self, http):
        if not _enabled("/api/unikon-status", http):
            pytest.skip("Unikon not compiled in")
        r = http("GET", "/api/unikon-status")
        assert r.status_code == 200
        assert isinstance(r.json(), dict)

    def test_ipg_laser_available(self, http):
        """IPG laser available is always 200 if compiled."""
        r = http("GET", "/api/ipg-laser-available")
        assert r.status_code == 200

    def test_radio_status(self, http):
        if not _enabled("/api/radio/status", http):
            pytest.skip("Radio (UDA1334A) not compiled in")
        r = http("GET", "/api/radio/status")
        assert r.status_code == 200
        assert isinstance(r.json(), dict)

    def test_usb_server_status(self, http):
        if not _enabled("/api/usb-server-status", http):
            pytest.skip("USB/IP server not compiled in")
        r = http("GET", "/api/usb-server-status")
        assert r.status_code in (200, 404)
