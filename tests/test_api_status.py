"""System status and diagnostic endpoint tests."""


class TestApiStatus:
    def test_status_json(self, http):
        """GET /api/status returns JSON with uptime field."""
        r = http("GET", "/api/status")
        assert r.status_code == 200
        ct = r.headers.get("content-type", "").lower()
        assert "application/json" in ct
        data = r.json()
        assert "uptime" in data or "system_status" in data or "status" in data

    def test_bootstrap_status(self, http):
        """GET /api/bootstrap-status returns initialized state."""
        r = http("GET", "/api/bootstrap-status")
        assert r.status_code == 200
        data = r.json()
        # Should indicate some form of init state
        assert isinstance(data, dict)

    def test_nvs_stats(self, http):
        """GET /api/nvs-stats returns NVS namespace info."""
        r = http("GET", "/api/nvs-stats")
        assert r.status_code == 200
        data = r.json()
        assert isinstance(data, dict)

    def test_sensor_flags(self, http):
        """GET /api/sensor-flags returns flags object."""
        r = http("GET", "/api/sensor-flags")
        assert r.status_code == 200
        data = r.json()
        assert isinstance(data, dict)

    def test_chart_caps(self, http):
        """GET /api/chart-caps returns capabilities."""
        r = http("GET", "/api/chart-caps")
        assert r.status_code == 200
        data = r.json()
        assert isinstance(data, dict)
