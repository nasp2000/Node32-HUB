"""Network status endpoint tests.

These are read-only tests that check network-related status endpoints.
"""


class TestApiNetwork:
    def test_wifi_status(self, http):
        """GET /api/wifi-status returns JSON."""
        r = http("GET", "/api/wifi-status")
        assert r.status_code == 200
        data = r.json()
        assert isinstance(data, dict)

    def test_eth_status(self, http):
        """GET /api/eth-status returns JSON (may be disabled)."""
        r = http("GET", "/api/eth-status")
        assert r.status_code in (200, 404)
        if r.status_code == 200:
            assert isinstance(r.json(), dict)

    def test_telnet_status(self, http):
        """GET /api/telnet-status returns JSON (may be disabled)."""
        r = http("GET", "/api/telnet-status")
        assert r.status_code in (200, 404)
        if r.status_code == 200:
            assert isinstance(r.json(), dict)

    def test_wifi_scan(self, http):
        """GET /api/wifi-scan returns list or dict."""
        r = http("GET", "/api/wifi-scan")
        assert r.status_code == 200
        data = r.json()
        assert isinstance(data, (dict, list))
