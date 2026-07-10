"""Client for the iot-assistant device's HTTP API.

The ESP32 exposes its components (LEDs, displays, audio) over a tiny JSON
API on port 80; this wraps it for tooling and for the LLM agent. Stdlib
only, so it runs on any cluster node without a venv.

    from device import IotDevice
    dev = IotDevice("http://192.168.1.50")
    dev.components()                     # [{"name": "led_green", "status": "..."}]
    dev.command("led_green", "blink", "250")
"""
import json
import os
import urllib.parse
import urllib.request

DEFAULT_URL = os.environ.get("IOT_DEVICE_URL", "http://iot-assistant.local")


class IotDevice:
    def __init__(self, base_url: str = DEFAULT_URL, timeout: float = 5.0):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def _get(self, path: str):
        with urllib.request.urlopen(self.base_url + path, timeout=self.timeout) as r:
            return json.loads(r.read().decode())

    def health(self) -> dict:
        return self._get("/health")

    def components(self) -> list:
        return self._get("/api/components")

    def command(self, name: str, action: str, arg: str = "") -> dict:
        """Dispatch <name> <action> [arg] to the device. Raises on HTTP errors
        except 4xx, which come back as {"error": ...} for the agent to read."""
        params = urllib.parse.urlencode(
            {"name": name, "action": action, "arg": arg})
        req = urllib.request.Request(
            f"{self.base_url}/api/command?{params}", method="POST")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                return json.loads(r.read().decode())
        except urllib.error.HTTPError as e:
            body = e.read().decode()
            try:
                return json.loads(body)
            except json.JSONDecodeError:
                return {"error": f"http {e.code}: {body[:200]}"}


if __name__ == "__main__":
    # Smoke test: python device.py [base_url]
    import sys
    dev = IotDevice(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_URL)
    print("health:", dev.health())
    for c in dev.components():
        print(f"  {c['name']:<10} {c['status']}")
