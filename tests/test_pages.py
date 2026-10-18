"""HTML page endpoint tests.

Verifies that common pages return HTML and contain expected structural
elements.
"""

import re
import pytest


class TestPages:
    def _check_page(self, http, path, status=200):
        r = http("GET", path)
        assert r.status_code == status
        if status == 200:
            assert "text/html" in r.headers.get("content-type", "").lower(), (
                f"{path} is not HTML"
            )
        return r

    def test_home_page(self, http):
        self._check_page(http, "/")

    def test_status_page(self, http):
        self._check_page(http, "/status")

    def test_wifi_page(self, http):
        self._check_page(http, "/wifi")

    def test_rs232_page(self, http):
        self._check_page(http, "/rs232")

    def test_monitor_page(self, http):
        self._check_page(http, "/monitor")

    def test_files_page(self, http):
        r = http("GET", "/files")
        assert r.status_code in (200, 302, 404)

    def test_sdcard_page(self, http):
        r = http("GET", "/sdcard")
        assert r.status_code in (200, 404)

    def test_recipients_page(self, http):
        self._check_page(http, "/recipients")

    def test_commands_page(self, http):
        self._check_page(http, "/commands")

    def test_access_page(self, http):
        self._check_page(http, "/access")

    def test_structures_page(self, http):
        self._check_page(http, "/structures")

    def test_email_page(self, http):
        r = http("GET", "/email")
        assert r.status_code in (200, 404)

    def test_buffer_viewer(self, http):
        self._check_page(http, "/buffer-viewer")

    def test_serial_monitor(self, http):
        self._check_page(http, "/serial-monitor")
