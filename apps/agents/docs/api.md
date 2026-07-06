# agents — API

Base URL: `http://<master>:8810` (compose default). FastAPI also serves
interactive docs at `/docs` (REST only; WS endpoints are documented here).

Handlers live in `app/api/`, one module per surface: `health.py`, `agents.py`,
`device.py`, `audio.py`.

## REST

### `GET /health`
Liveness plus a one-look status of everything downstream.

```json
{
  "ok": true,
  "agents": ["face", "led"],
  "device": {"device": "iot-assistant", "version": "0.3", "ip": "...", "uptime_s": 123},
  "audio": {"ingest_connected": false, "frames_in": 0, "subscribers": 0}
}
```
`device` becomes `{"error": "..."}` when the ESP32 is unreachable; the endpoint
itself still returns 200.

### `GET /api/agents`
The registered agents (auto-discovered from `app/agents/`).

```json
[{"name": "led", "description": "Signals with the two breadboard LEDs..."}]
```

### `POST /api/agents/{name}/run`
Blocking run: collects the whole event stream, returns the final output plus
the events that led to it. The WS endpoint is the live version of this.

Request: `{"input": "blink the blue led fast"}`

```json
{
  "agent": "led",
  "output": {...final JSON, see per-agent schemas below...},
  "error": null,
  "events": [{"type": "start", ...}, ..., {"type": "final", ...}]
}
```
Errors: `404` unknown agent, `400` missing `input`.

### `GET /api/device/components`
Proxy of the ESP32's component list (see `apps/iot-assistant/docs/api.md`).
`502 {"error": "..."}` when the device is unreachable.

## WebSocket

### `/ws/agents/{name}`
Send `{"input": "..."}` (or bare text); receive the run's live event stream,
one JSON object per message, ending with `final` (or `error`). The socket
stays open for more inputs. Unknown agent ⇒ one `error` event, then close.

Event protocol (`app/agents/events.py`):

```json
{"type": "start",       "agent": "led", "input": "..."}
{"type": "thinking",    "text": "partial model text"}
{"type": "tool_call",   "tool": "set_led", "args": {...}}
{"type": "tool_result", "tool": "set_led", "result": {...}}
{"type": "final",       "output": {...structured agent JSON...}}
{"type": "error",       "message": "..."}
```

### `/ws/audio/ingest`
One producer: the ESP32 pushes binary mic frames (opaque PCM; the hub never
parses the payload). Non-binary messages are ignored.

### `/ws/audio/subscribe`
Consumers receive the ingested frames fanned out, as binary messages. Slow
subscribers get dropped (oldest-first) frames, not backpressure.

## Agent output schemas

`final.output` is always a JSON object; each agent guarantees its own contract
(assembled in code from the tool trace, not trusted from the model).

**led**
```json
{
  "answer":    "yes" | "no" | null,
  "reasoning": "<one sentence>",
  "actions":   [{"tool": "set_led", "args": {...}, "ok": true}],
  "message":   "<one sentence for the user>"
}
```

**face**
```json
{"emotion": "neutral|happy|sad|angry|sleepy|surprised", "reasoning": "<one sentence>"}
```
