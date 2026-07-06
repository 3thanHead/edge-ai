# agents

LangChain agents served over REST/WS, running against the home [LLM cluster](../../infra/llm-cluster/)
and actuating the [iot-assistant](../iot-assistant/) ESP32. The chat app can route a
message to any agent here and stream its working steps live into the UI.

Ships with its own **mosquitto** broker (the ESP32's MQTT home) in the compose file —
deploy this stack on the cluster master so the device has one stable address for both.

## Agents

| agent | purpose |
|---|---|
| `led` | Signals with the two breadboard LEDs: blink rates, patterns (sos/heartbeat/strobe), and yes/no answers — **blue (GPIO 5) = yes, red (GPIO 4) = no**. Answers questions by reasoning first, then the hardware shows the verdict. |
| `face` | Reads a chat message and picks the robot's LCD expression (neutral/happy/sad/angry/sleepy/surprised), sent to the device over MQTT (HTTP fallback). |

### Adding an agent

Drop a module in `app/agents/` that exports `AGENT` (a `BaseAgent` instance):
name, description, a purpose prompt, LangChain `@tool` functions. Nothing else to
register — the module is auto-discovered. Agents are stateless, so one instance
serves concurrent runs; LLM calls load-balance across the cluster via HAProxy.

**Design note for small models (llama3.2:3b):** let the model *decide* and let code
*actuate*. The guaranteed JSON contract is assembled in `build_output()` from the
trace of executed tool calls (ground truth), and decision→hardware translation
happens in `act_on_output()` — deterministic, not model-wired. Tool inputs are
normalized forgivingly (`action="sos"` → `pattern sos`, `arg="fast"` → `150`).

## API

Full contract — endpoints, WS event protocol, per-agent output schemas — in
[docs/api.md](docs/api.md). At a glance:

```
GET  /health                    liveness + device + audio stats
GET  /api/agents                registered agents
POST /api/agents/{name}/run     {"input": "..."} -> {"output": {...}, "events": [...]}
GET  /api/device/components     proxy of the ESP32's component list
WS   /ws/agents/{name}          send {"input": "..."}; receive live events:
                                start | thinking | tool_call | tool_result | final | error
WS   /ws/audio/ingest           ESP32 pushes binary mic frames (opaque PCM)
WS   /ws/audio/subscribe        consumers receive those frames fanned out
```

## Run

```bash
cp .env.example .env            # point it at the cluster master + the ESP32
docker compose up --build       # agents API on :8810 + mosquitto on :1883
curl http://localhost:8810/api/agents
```

Or from the repo root: `./edge up agents` (endpoints injected from fleet.json +
root .env), `./edge deploy agents` to ship it to the master.

Dev without docker: `pip install -r requirements.txt`, then
`LLM_BASE_URL=... IOT_DEVICE_URL=... uvicorn app.main:app --port 8810`.

**The cluster model must support tool calling** (`llama3.2:3b`, the fleet default —
`./edge model set llama3.2:3b` if the nodes are serving something else). Bigger
model = better judgement; the harness stays the same.

## Shape

The layout mirrors the API: `app/api/` holds one handler module per surface
(each owns its routes, state, and config), `app/agents/` the agents and their
discovery. `main.py` only assembles.

```
app/
  main.py          FastAPI assembly: mounts the api/ routers
  api/
    health.py      GET /health
    agents.py      /api/agents/* + /ws/agents/{name}
    device.py      /api/device/* + ESP32 client: HTTP (acked) + MQTT + state cache
    audio.py       /ws/audio/*   + mic-frame fan-out hub (drop-oldest per slow subscriber)
  agents/
    __init__.py    auto-discovers modules here exporting AGENT
    base.py        BaseAgent: tool loop, JSON contract, small-model armor
    events.py      the event wire protocol
    led_agent.py face_agent.py
docs/api.md        the API contract
```
