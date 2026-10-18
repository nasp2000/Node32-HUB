"""Sensor endpoint tests.

These tests read sensor_flags from /api/sensor-flags and conditionally
test only the sensor endpoints that are compiled in.
"""

import pytest


def _check(have, sensor_name):
    """Return True if the sensor flag is present and truthy."""
    val = have.get(sensor_name)
    return val is True or val == 1 or val == "1"


class TestApiSensors:
    @pytest.fixture(autouse=True)
    def _setup(self, sensor_flags):
        self.flags = sensor_flags

    def test_sensor_config(self, http):
        if not _check(self.flags, "aht20_bmp280"):
            pytest.skip("AHT20/BMP280 not compiled in")
        r = http("GET", "/api/sensor-config")
        assert r.status_code == 200
        assert isinstance(r.json(), dict)

    def test_sensor_slots(self, http):
        if not _check(self.flags, "aht20_bmp280"):
            pytest.skip("AHT20/BMP280 not compiled in")
        r = http("GET", "/api/sensor-slots")
        assert r.status_code == 200
        data = r.json()
        # Should be a list of slots or an object
        assert isinstance(data, (dict, list))

    def test_mq135_config(self, http):
        if not _check(self.flags, "mq135"):
            pytest.skip("MQ135 not compiled in")
        r = http("GET", "/api/mq135-config")
        assert r.status_code == 200
        assert isinstance(r.json(), dict)

    def test_sensor_history(self, http):
        if not _check(self.flags, "aht20_bmp280") and not _check(self.flags, "mq135"):
            pytest.skip("No sensor with history support compiled in")
        r = http("GET", "/api/sensor-history")
        # May return 200 (data) or 200 (empty) or 500 (no data yet)
        assert r.status_code in (200, 500)
