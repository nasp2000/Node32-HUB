import os
import pytest
import requests


def pytest_addoption(parser):
    parser.addoption(
        "--host",
        default=os.environ.get("NODE32_HOST", "node32-hub.local"),
        help="Node32-HUB hostname or IP",
    )
    parser.addoption(
        "--auth",
        default=os.environ.get("NODE32_AUTH", ""),
        help="HTTP Basic Auth as 'user:pass' (blank = no auth)",
    )
    parser.addoption(
        "--timeout",
        type=float,
        default=float(os.environ.get("NODE32_TIMEOUT", "10")),
        help="Request timeout in seconds",
    )


@pytest.fixture(scope="session")
def host(request):
    return request.config.getoption("--host")


@pytest.fixture(scope="session")
def auth(request):
    val = request.config.getoption("--auth")
    if val and ":" in val:
        user, pwd = val.split(":", 1)
        return (user, pwd)
    return None


@pytest.fixture(scope="session")
def timeout(request):
    return request.config.getoption("--timeout")


@pytest.fixture(scope="session")
def http(host, auth, timeout):
    """Pre-configured requests session."""

    def request(method, path, **kwargs):
        url = f"http://{host}{path}"
        kwargs.setdefault("timeout", timeout)
        if auth:
            kwargs.setdefault("auth", auth)
        return requests.request(method, url, **kwargs)

    return request


@pytest.fixture(scope="session")
def api_base(host):
    return f"http://{host}/api"


@pytest.fixture(scope="session")
def sensor_flags(http):
    """Detect which sensors are compiled in."""
    try:
        r = http("GET", "/api/sensor-flags")
        if r.status_code == 200:
            return r.json()
    except Exception:
        pass
    return {}
