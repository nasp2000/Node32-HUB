"""Basic connectivity and health checks."""


class TestHealth:
    def test_healthz_unauthenticated(self, http):
        """/healthz must respond 200 without auth."""
        r = http("GET", "/healthz")
        assert r.status_code == 200

    def test_favicon(self, http):
        """Favicon returns 204 No Content."""
        r = http("GET", "/favicon.ico")
        assert r.status_code == 204

    def test_static_css(self, http):
        """core.css is served with correct content type."""
        r = http("GET", "/core.css")
        assert r.status_code == 200
        ct = r.headers.get("content-type", "").lower()
        assert "text/css" in ct or "text/plain" in ct

    def test_static_js(self, http):
        """auto.js is served."""
        r = http("GET", "/auto.js")
        assert r.status_code == 200

    def test_config_manager_js(self, http):
        """config-manager.js is served."""
        r = http("GET", "/config-manager.js")
        assert r.status_code == 200

    def test_lang_endpoint(self, http):
        """Language JSON is served."""
        r = http("GET", "/api/lang")
        assert r.status_code == 200
        data = r.json()
        # Must have at least some i18n keys
        assert isinstance(data, dict)
        assert len(data) > 0
