"""Configuration endpoint tests.

Most config endpoints are GET-only in these tests (POST would modify device
state).  We validate the schema of each known config section.
"""


class TestApiConfig:
    def test_unified_config_wifi(self, http):
        """GET /api/config/wifi returns narrow wifi config."""
        r = http("GET", "/api/config/wifi")
        assert r.status_code == 200
        data = r.json()
        # Must contain wifi_ssid
        assert "wifi_ssid" in data

    def test_unified_config_email(self, http):
        """GET /api/config/email returns email config."""
        r = http("GET", "/api/config/email")
        assert r.status_code == 200
        data = r.json()
        assert isinstance(data, dict)

    def test_unified_config_printer(self, http):
        """GET /api/config/printer returns printer/system config."""
        r = http("GET", "/api/config/printer")
        assert r.status_code == 200
        data = r.json()
        assert isinstance(data, dict)

    def test_unified_config_rs232(self, http):
        """GET /api/config/rs232 returns RS232 config."""
        r = http("GET", "/api/config/rs232")
        assert r.status_code == 200
        data = r.json()
        assert isinstance(data, dict)

    def test_unified_config_access(self, http):
        """GET /api/config/access-point returns AP config."""
        r = http("GET", "/api/config/access-point")
        assert r.status_code == 200
        data = r.json()
        assert isinstance(data, dict)

    def test_access_config(self, http):
        """GET /api/access-config returns access page settings."""
        r = http("GET", "/api/access-config")
        assert r.status_code == 200
        data = r.json()
        assert isinstance(data, dict)

    def test_saved_networks(self, http):
        """GET /api/config/saved_networks returns list."""
        r = http("GET", "/api/config/saved_networks")
        assert r.status_code == 200
        data = r.json()
        assert isinstance(data, (dict, list))
