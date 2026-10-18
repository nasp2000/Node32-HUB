# Node32-HUB Integration Tests

HTTP-based integration test suite for Node32-HUB firmware.
Tests run against a **live device** on the network.

## Requirements

- Python 3.8+
- `pip install -r requirements.txt`

## Usage

```bash
# Default: connect to http://node32-hub.local
python -m pytest tests/ -v

# Specific device
python -m pytest tests/ -v --host 192.168.1.100

# With HTTP Basic Auth
python -m pytest tests/ -v --auth admin:secret

# PowerShell helper
.\tests\run_tests.ps1 -Host 192.168.1.100 -Verbose
```

Environment variables:
- `NODE32_HOST` — device hostname/IP (default: `node32-hub.local`)
- `NODE32_AUTH` — Basic auth as `user:pass`
- `NODE32_TIMEOUT` — request timeout in seconds (default: 10)

## Adding tests

1. Create `tests/test_*.py`
2. Use the `http` fixture (pre-configured requests session)
3. Use `pytest.skip()` for optional modules

```python
def test_something(http):
    r = http("GET", "/api/foo")
    assert r.status_code == 200
    assert isinstance(r.json(), dict)
```

## CI integration

```yaml
- name: Integration tests
  env:
    NODE32_HOST: 10.0.0.42
    NODE32_AUTH: admin:secret
  run: |
    pip install -r tests/requirements.txt
    python -m pytest tests/ -v --junitxml=reports/test-results.xml
```
