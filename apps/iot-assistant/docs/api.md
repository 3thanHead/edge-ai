# iot-assistant — API

The device exposes its components over three control surfaces sharing one
command shape: `<component> <action> [arg]`. Component names, actions, and
args are listed in the README's control-surfaces section; `list` (serial) or
`GET /api/components` (HTTP) enumerates them live.

## HTTP (`http://<esp32>:80`)

### `GET /health`

```json
{"device": "iot-assistant", "version": "0.3", "ip": "192.168.x.y", "uptime_s": 123}
```

### `GET /api/components`
Every registered component with its current status.

```json
[{"name": "led_1", "status": "off"}, {"name": "servo", "status": "angle=90"}]
```

### `POST /api/command?name=<component>&action=<verb>&arg=<optional>`
Acked command (query or form params). Example:
`POST /api/command?name=led_1&action=blink&arg=250`

```json
{"ok": true, "name": "led_1", "status": "blink@250ms"}
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

## Serial console (115200 baud)

Same commands, typed at `make monitor`; plus `list`, `get <name>`, `help`.

## Consumers

- `apps/agents` actuates this API as LLM tools (HTTP acked, MQTT
  fire-and-forget) — see `apps/agents/docs/api.md`.
- `app/` here is the standalone CLI harness: `python app/main.py "blink led 1 fast"`.
