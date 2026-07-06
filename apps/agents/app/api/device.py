"""Device handlers -- the iot-assistant ESP32, over both of its control
surfaces, plus the REST proxy in front of it.

REST
    GET /api/device/components   proxy of the ESP32's component list

HTTP  -- request/response with an ack; used when the agent needs to know the
         command landed (LED answers, servo moves).
MQTT  -- fire-and-forget publish to iot/<device>/cmd; used for low-stakes,
         high-frequency updates like face emotions. The device also publishes
         retained state to iot/<device>/state, which we cache here.

Both speak the same "<name> <action> [arg]" command shape as the device's
serial console.

Config (env, no IPs committed):
    IOT_DEVICE_URL  the ESP32's HTTP API
    MQTT_HOST/PORT  broker the ESP32 is connected to (compose's mosquitto)
    DEVICE_NAME     topic segment, default iot-assistant
"""
import json
import logging
import os
import threading

import httpx
import paho.mqtt.client as mqtt
from fastapi import APIRouter
from fastapi.responses import JSONResponse

log = logging.getLogger("agents.device")

IOT_DEVICE_URL = os.environ.get("IOT_DEVICE_URL", "http://iot-assistant.local").rstrip("/")
MQTT_HOST = os.environ.get("MQTT_HOST", "")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
DEVICE_NAME = os.environ.get("DEVICE_NAME", "iot-assistant")

router = APIRouter()


class DeviceClient:
    def __init__(self):
        self._http = httpx.AsyncClient(base_url=IOT_DEVICE_URL, timeout=6.0)
        self._mqtt: mqtt.Client | None = None
        self._state: list[dict] = []
        self._available: bool | None = None
        self._base = f"iot/{DEVICE_NAME}"
        if MQTT_HOST:
            self._start_mqtt()

    # -- HTTP ------------------------------------------------------------
    async def health(self) -> dict:
        r = await self._http.get("/health")
        r.raise_for_status()
        return r.json()

    async def components(self) -> list[dict]:
        r = await self._http.get("/api/components")
        r.raise_for_status()
        return r.json()

    async def command(self, name: str, action: str, arg: str = "") -> dict:
        """Acked command over HTTP. 4xx/422 come back as {"error": ...} so the
        agent can read what went wrong instead of blowing up."""
        try:
            r = await self._http.post(
                "/api/command", params={"name": name, "action": action, "arg": arg})
            return r.json()
        except httpx.HTTPError as e:
            return {"error": f"device unreachable: {e}"}

    # -- MQTT ------------------------------------------------------------
    def _start_mqtt(self):
        c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="agents-api")

        def on_connect(client, _userdata, _flags, _rc, _props=None):
            client.subscribe(f"{self._base}/state")
            client.subscribe(f"{self._base}/availability")

        def on_message(_client, _userdata, msg):
            if msg.topic.endswith("/state"):
                try:
                    self._state = json.loads(msg.payload)
                except json.JSONDecodeError:
                    pass
            elif msg.topic.endswith("/availability"):
                self._available = msg.payload == b"online"

        c.on_connect = on_connect
        c.on_message = on_message
        c.connect_async(MQTT_HOST, MQTT_PORT)
        c.loop_start()  # paho runs its own network thread
        self._mqtt = c
        log.info("mqtt: connecting to %s:%s", MQTT_HOST, MQTT_PORT)

    def publish_command(self, name: str, action: str, arg: str = "") -> bool:
        """Fire-and-forget command over MQTT. Returns False if MQTT is not
        configured/connected (callers can fall back to HTTP)."""
        if self._mqtt is None or not self._mqtt.is_connected():
            return False
        line = f"{name} {action} {arg}".strip()
        self._mqtt.publish(f"{self._base}/cmd", line)
        return True

    # -- cached push state -------------------------------------------------
    @property
    def state(self) -> list[dict]:
        return self._state

    @property
    def available(self) -> bool | None:
        """True/False from the availability topic, None = no MQTT signal yet."""
        return self._available


_lock = threading.Lock()
_instance: DeviceClient | None = None


def get_device() -> DeviceClient:
    global _instance
    with _lock:
        if _instance is None:
            _instance = DeviceClient()
        return _instance


@router.get("/api/device/components")
async def device_components():
    try:
        return await get_device().components()
    except Exception as e:
        return JSONResponse({"error": str(e)}, status_code=502)
