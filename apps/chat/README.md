# chat

A minimal, clean web UI for chatting with the home [LLM cluster](../../infra/llm-cluster/).
Tokens stream into the browser over a WebSocket as the model generates them — like
Claude Code's live output — and you pick which cluster model to use per message.

By default it talks to the cluster through the Mini PC's single HAProxy endpoint
(load-balanced, fault-tolerant), but you can also pin a chat to a specific node.

## What it does
- **Model picker** — populated live from the cluster (`/api/tags`); always reflects what's loaded.
- **Node picker** — run load-balanced (Auto) through HAProxy, or pin to one node by hostname;
  the "served by" badge shows which node answered. Hidden when no per-node info is configured.
- **Streaming chat** — browser ↔ FastAPI over WebSocket; FastAPI streams `/api/chat` from
  Ollama token-by-token and forwards each token straight to the page.
- **Multi-turn** — full conversation history is sent with each request.

> Agentic / tool output rendering is intentionally out of scope for now — this is the
> plain streaming chat surface we'll build that on top of.

## Run
```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env            # or `./edge up chat` injects the master from fleet.json
uvicorn app.main:app --host 0.0.0.0 --port 8800
```
Open http://localhost:8800. (Requires the cluster master + at least one node up.)

## API

Full contract — REST endpoints and the `/ws/chat` message protocol — in
[docs/api.md](docs/api.md).

## Shape
- `app/main.py` — FastAPI: `GET /api/models`, `GET /api/nodes`, `GET /api/agents`, `WS /ws/chat`, serves the frontend.
- `app/static/index.html` — single-page UI (no build step).
- point `LLM_BASE_URL` elsewhere via `.env` to target a different Ollama/cluster, no code changes.
