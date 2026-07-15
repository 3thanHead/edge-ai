#!/usr/bin/env python3
"""chat — a minimal streaming chatbot UI on top of the home LLM cluster.

Serves a single-page chat frontend and streams tokens from Ollama back to the
browser over a WebSocket as they are generated (like Claude Code's live output).
Models are listed straight from the cluster, so the dropdown always reflects
whatever is actually loaded across the nodes. A chat can run load-balanced through
HAProxy (default) or be pinned to a specific node via the node picker.

    uvicorn app.main:app --host 0.0.0.0 --port 8800
"""
import asyncio
import json
import os
from pathlib import Path

import httpx
import websockets
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

# Cluster endpoint (HAProxy -> whichever Ollama node is up). Native Ollama API:
# /api/chat to stream a chat — the single, load-balanced, fault-tolerant endpoint
# that ALL generation goes through. `edge up`/`edge deploy` inject the real master
# from fleet.json; the default is just a fallback for a standalone run.
OLLAMA_URL = os.environ.get("LLM_BASE_URL", "http://localhost:11434").rstrip("/")

# Per-node access serves two features beyond the load-balanced LB:
#   1. Model DISCOVERY — /api/tags through the LB only reflects the one node it routes to,
#      so we union /api/tags across nodes to build the full dropdown.
#   2. Node PINNING — the user can target one node directly, bypassing the LB.
# `edge up`/`edge deploy` inject CLUSTER_NODES as comma-separated `name=url[|keep_alive]`
# pairs from fleet.json (name = the node's hostname; keep_alive = that node's optional
# model residency, e.g. "24h" or -1 = never unload). Legacy plain-url entries are still
# accepted (name defaults to the url). Empty => fall back to the LB endpoint (local/dev).
def _parse_keep_alive(raw):
    """Ollama's keep_alive: bare numbers stay numbers (-1 = never unload; a string
    \"-1\" would fail its duration parser), duration strings like "24h" pass through,
    empty => None (the node keeps Ollama's default)."""
    if not raw:
        return None
    try:
        return int(raw)
    except ValueError:
        return raw


def _parse_nodes(raw):
    nodes = {}
    for item in raw.split(","):
        item = item.strip()
        if not item:
            continue
        name, sep, rest = item.partition("=")
        if not sep:  # legacy: bare URL, name == URL
            u = name.strip().rstrip("/")
            nodes[u] = {"url": u, "keep_alive": None}
            continue
        url, _, keep = rest.partition("|")
        nodes[name.strip()] = {"url": url.strip().rstrip("/"),
                               "keep_alive": _parse_keep_alive(keep.strip())}
    return nodes


NODES = _parse_nodes(os.environ.get("CLUSTER_NODES", ""))   # hostname -> {url, keep_alive}

# The agents app (apps/agents) — chat can hand a message to one of its agents
# instead of the plain model, and streams the agent's working events (tool
# calls, results, final JSON) into the UI. Empty/unreachable => the agent
# picker simply hides; plain chat is unaffected.
AGENTS_URL = os.environ.get("AGENTS_URL", "http://localhost:8810").rstrip("/")

STATIC_DIR = Path(__file__).parent / "static"

app = FastAPI(title="cluster-chat")


async def _node_models(client, url):
    try:
        resp = await client.get(f"{url}/api/tags")
        resp.raise_for_status()
        return [m["name"] for m in resp.json().get("models", [])]
    except httpx.HTTPError:
        return []  # a down node just contributes nothing to the union


async def _node_running(client, url):
    try:
        resp = await client.get(f"{url}/api/ps")
        resp.raise_for_status()
        return [m["name"] for m in resp.json().get("models", [])]
    except httpx.HTTPError:
        return []


@app.get("/api/models")
async def list_models():
    """Union of model names available across the cluster's nodes, plus which of
    them are loaded in memory somewhere right now (union of /api/ps) — the UI
    defaults to a running model so the first message skips the cold load."""
    sources = [n["url"] for n in NODES.values()] or [OLLAMA_URL]
    async with httpx.AsyncClient(timeout=5) as client:
        per_node, per_node_running = await asyncio.gather(
            asyncio.gather(*(_node_models(client, u) for u in sources)),
            asyncio.gather(*(_node_running(client, u) for u in sources)),
        )
    return {"models": sorted({name for names in per_node for name in names}),
            "running": sorted({name for names in per_node_running for name in names})}


@app.get("/api/nodes")
async def list_nodes():
    """Node names a chat can be pinned to (in addition to the default load-balanced LB).
    Empty when no per-node info is configured, so the UI hides the picker."""
    return {"nodes": sorted(NODES)}


@app.get("/api/agents")
async def list_agents():
    """Agents available from the agents app; empty when it's down/absent so
    the UI hides the agent picker."""
    try:
        async with httpx.AsyncClient(timeout=4) as client:
            resp = await client.get(f"{AGENTS_URL}/api/agents")
            resp.raise_for_status()
            return {"agents": resp.json()}
    except httpx.HTTPError:
        return {"agents": []}


# Fire-and-forget keep-alive bumps; the set holds strong refs so asyncio can't GC
# a task mid-flight.
_keep_alive_tasks = set()


async def _touch_keep_alive(url, model, keep_alive):
    """Apply a node's configured keep_alive after the LB routed a generation to it.

    keep_alive only travels inside a generation request, and a load-balanced request
    can't know its node up front — so once HAProxy reports who served (X-Served-By),
    poke that node directly. /api/generate with no prompt doesn't generate anything:
    it just (re)sets how long the already-loaded model stays in memory."""
    try:
        async with httpx.AsyncClient(timeout=10) as client:
            await client.post(f"{url}/api/generate",
                              json={"model": model, "keep_alive": keep_alive})
    except httpx.HTTPError:
        pass  # a node that just served us but won't answer simply misses its bump


async def _stream_chat(ws: WebSocket, model, messages, node=None):
    """Stream one completion to the browser. Cancelling this task unwinds the httpx
    context managers, which closes the upstream request so Ollama stops generating.

    `node` (a name from /api/nodes) pins generation to that node's Ollama directly;
    otherwise it goes through the load-balanced LB endpoint. Either way the serving
    node's fleet.json keep_alive (if any) is applied: in the request itself when
    pinned, via a follow-up bump when the LB picked the node."""
    target = NODES.get(node) if node else None     # None => use the LB
    url = target["url"] if target else OLLAMA_URL
    payload = {"model": model, "messages": messages, "stream": True}
    if target and target["keep_alive"] is not None:
        payload["keep_alive"] = target["keep_alive"]
    served = None
    try:
        async with httpx.AsyncClient(timeout=None) as client:
            async with client.stream("POST", f"{url}/api/chat", json=payload) as resp:
                resp.raise_for_status()
                # Through the LB, HAProxy stamps the serving node on X-Served-By. Going
                # direct to a node there's no such header, so report the pinned node itself.
                served = node if target else resp.headers.get("x-served-by")
                if served:
                    await ws.send_json({"type": "node", "name": served})
                async for line in resp.aiter_lines():
                    if not line.strip():
                        continue
                    chunk = json.loads(line)
                    token = chunk.get("message", {}).get("content", "")
                    if token:
                        await ws.send_json({"type": "token", "content": token})
                    if chunk.get("done"):
                        break
        served_cfg = NODES.get(served) if (served and not target) else None
        if served_cfg and served_cfg["keep_alive"] is not None:
            task = asyncio.create_task(
                _touch_keep_alive(served_cfg["url"], model, served_cfg["keep_alive"]))
            _keep_alive_tasks.add(task)
            task.add_done_callback(_keep_alive_tasks.discard)
        await ws.send_json({"type": "done"})
    except httpx.HTTPError as exc:
        await ws.send_json({"type": "error", "message": f"cluster error: {exc}"})


async def _stream_agent(ws: WebSocket, agent: str, input_text: str):
    """Run one agent turn via the agents app, relaying its working events
    (start/thinking/tool_call/tool_result/final/error) to the browser as
    {"type": "agent", "event": {...}}. Cancelling this task closes the
    upstream socket, which ends the run server-side."""
    url = AGENTS_URL.replace("http://", "ws://").replace("https://", "wss://")
    try:
        async with websockets.connect(f"{url}/ws/agents/{agent}") as upstream:
            await upstream.send(json.dumps({"input": input_text}))
            async for raw in upstream:
                event = json.loads(raw)
                await ws.send_json({"type": "agent", "event": event})
                if event.get("type") in ("final", "error"):
                    break
        await ws.send_json({"type": "done"})
    except (OSError, websockets.WebSocketException) as exc:
        await ws.send_json({"type": "error",
                            "message": f"agents app unreachable: {exc}"})


@app.websocket("/ws/audio")
async def audio_feed(ws: WebSocket):
    """Relay the ESP32's live mic feed to the browser.

    The device streams 16 kHz mono int16 PCM frames to the agents app's audio
    hub (/ws/audio/ingest); this proxies the hub's /ws/audio/subscribe fan-out
    so the frontend only ever talks to the chat host. Binary passthrough --
    the payload is never touched. Closes with reason when the hub is down so
    the UI can say why."""
    await ws.accept()
    url = AGENTS_URL.replace("http://", "ws://").replace("https://", "wss://")
    try:
        async with websockets.connect(f"{url}/ws/audio/subscribe") as upstream:
            async for frame in upstream:
                if isinstance(frame, bytes):
                    await ws.send_bytes(frame)
    except (OSError, websockets.WebSocketException):
        await ws.close(code=1011, reason="agents audio hub unreachable")
    except (WebSocketDisconnect, RuntimeError):
        pass  # browser closed the panel; upstream unwinds via the context manager


@app.websocket("/ws/transcripts")
async def transcripts_feed(ws: WebSocket):
    """Relay the agents app's live speech-to-text feed (JSON events from
    /ws/audio/transcripts) to the browser, same pattern as /ws/audio."""
    await ws.accept()
    url = AGENTS_URL.replace("http://", "ws://").replace("https://", "wss://")
    try:
        async with websockets.connect(f"{url}/ws/audio/transcripts") as upstream:
            async for msg in upstream:
                if isinstance(msg, str):
                    await ws.send_text(msg)
    except (OSError, websockets.WebSocketException):
        await ws.close(code=1011, reason="agents transcript feed unreachable")
    except (WebSocketDisconnect, RuntimeError):
        pass


async def _cancel(task):
    """Cancel an in-flight generation task and wait for it to fully unwind."""
    if task and not task.done():
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass


@app.websocket("/ws/chat")
async def chat(ws: WebSocket):
    """Stream a chat completion token-by-token to the browser, cancellable mid-stream.

    Generation runs in a background task so the socket stays readable; a {"type":"cancel"}
    message (or a new chat while one is in flight) aborts it. Client sends either
    {"model","messages"} to generate or {"type":"cancel"} to stop. Server sends
    {"type":"token"|"done"|"cancelled"|"error"}.
    """
    await ws.accept()
    gen = None
    try:
        while True:
            req = await ws.receive_json()
            if req.get("type") == "cancel":
                if gen and not gen.done():
                    await _cancel(gen)
                    await ws.send_json({"type": "cancelled"})
                continue

            model = req.get("model")
            messages = req.get("messages", [])
            agent = req.get("agent")  # optional: route to an agents-app agent
            node = req.get("node")  # optional: pin to one node, else load-balanced

            if agent:
                if not messages:
                    await ws.send_json({"type": "error", "message": "messages are required"})
                    continue
                await _cancel(gen)
                gen = asyncio.create_task(
                    _stream_agent(ws, agent, messages[-1].get("content", "")))
                continue

            if not model or not messages:
                await ws.send_json({"type": "error", "message": "model and messages are required"})
                continue

            await _cancel(gen)  # never run two generations on one socket at once
            gen = asyncio.create_task(_stream_chat(ws, model, messages, node))
    except WebSocketDisconnect:
        await _cancel(gen)  # client closed the tab mid-stream -> stop generating upstream


# Serve the frontend. Mounted last so the API routes above take precedence.
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


@app.get("/")
async def index():
    return FileResponse(STATIC_DIR / "index.html")
