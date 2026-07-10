# iot-assistant — API

The device exposes its components over three control surfaces sharing one
command shape: `<component> <action> [arg]`. Component names, actions, and
args are listed in the README's control-surfaces section; `list` (serial) or
`GET /api/components` (HTTP) enumerates them live.

## HTTP (`http://<esp32>:80`)

### `GET /health`

```json
{"device": "iot-assistant", "version": "0.4", "ip": "192.168.x.y", "uptime_s": 123}
```

### `GET /api/components`
Every registered component with its current status.

```json
[{"name": "led_green", "status": "off"}, {"name": "lcd", "status": "bl=255"}]
```

### `POST /api/command?name=<component>&action=<verb>&arg=<optional>`
Acked command (query or form params). Example:
`POST /api/command?name=led_green&action=blink&arg=250`

```json
{"ok": true, "name": "led_green", "status": "blink 250ms"}
```
Errors: `400` missing name/action, `404` unknown component, `422` the
component rejected the action. All errors are `{"error": "..."}`.

## MQTT (broker = the agents stack's mosquitto)

Topic base: `iot/<device>` (default `iot/iot-assistant`).

| topic | direction | payload |
|---|---|---|
| `.../cmd` | → device | `<component> <action> [arg]` as plain text, fire-and-forget |
| `.../resp` | device → | ack/result for each cmd |
| `.../state` | device → | **retained** JSON of all component statuses, on change + every 10 s |
| `.../availability` | device → | `online` / `offline` (LWT) |

## Audio out (device → agents hub)

The `mic` component pushes the codec's mic capture as binary WS frames to
`ws://<MQTT_HOST>:8810/ws/audio/ingest` — 16 kHz mono int16 LE PCM,
1024 samples (2048 B / 64 ms) per frame, auto-reconnecting, on by default
(`mic off` to stop). Consumers subscribe via the agents app's
`/ws/audio/subscribe`; the chat UI's 🎙 button renders the live feed.

## Serial console (115200 baud)

Same commands, typed at `make monitor`; plus `list`, `get <name>`, `help`.

## Consumers

- `apps/agents` actuates this API as LLM tools (HTTP acked, MQTT
  fire-and-forget) — see `apps/agents/docs/api.md`.
- `app/` here is the standalone CLI harness: `python app/main.py "blink the green led fast"`.
