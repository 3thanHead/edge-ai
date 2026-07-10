# chat — API

Base URL: `http://<master>:8800` (compose default). `GET /` serves the
single-page UI; everything below is what that UI talks to.

## REST

### `GET /api/models`
Union of model names available across the cluster's nodes (each node's
`/api/tags`, merged). Falls back to the load-balanced endpoint alone when no
per-node info is configured.

```json
{"models": ["llama3.2:3b", "moondream:latest"]}
```

### `GET /api/nodes`
Node names a chat can be pinned to (in addition to the default load-balanced
endpoint). Empty when `CLUSTER_NODES` isn't configured, so the UI hides the
picker.

```json
{"nodes": ["jetson", "minipc"]}
```

### `GET /api/agents`
Agents available from the agents app (`apps/agents`, proxied). Empty when it's
down/absent so the UI hides the agent picker.

```json
{"agents": [{"name": "led", "description": "..."}]}
```

## WebSocket

### `/ws/chat`
One socket per tab; one generation in flight at a time (a new request or a
cancel aborts the previous one).

Client → server:

```json
{"model": "llama3.2:3b", "messages": [{"role": "user", "content": "hi"}]}
{"model": "...", "messages": [...], "node": "jetson"}      // pin to one node
{"agent": "led", "messages": [...]}                         // route to an agent
{"type": "cancel"}                                          // stop generating
```

Server → client:

```json
{"type": "node", "name": "jetson"}          // which node is serving this turn
{"type": "token", "content": "..."}         // streamed model tokens
{"type": "agent", "event": {...}}           // relayed agent event (see apps/agents/docs/api.md)
{"type": "done"}
{"type": "cancelled"}
{"type": "error", "message": "..."}
```

Agent turns relay the agents app's event stream (`start | thinking |
tool_call | tool_result | final | error`) wrapped as `{"type": "agent",
"event": ...}`, then `{"type": "done"}`.
