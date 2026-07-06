"""agents -- the cluster's agent-serving API.

Assembly only: every route lives in app/api/ (one module per surface), the
agents and their discovery in app/agents/. Full contract: docs/api.md.

REST
    GET  /health                     liveness + device/audio status
    GET  /api/agents                 registered agents
    POST /api/agents/{name}/run      {"input": "..."} -> {"output": ..., "events": [...]}
    GET  /api/device/components      proxy of the ESP32's component list

WS
    /ws/agents/{name}                send {"input": "..."}; receive the live
                                     AgentEvent stream, ending with a "final"
                                     (or "error") event
    /ws/audio/ingest                 the ESP32 pushes binary mic frames here
    /ws/audio/subscribe              consumers receive those frames fanned out
"""
import logging

from fastapi import FastAPI

from . import agents, api
from .api.device import get_device

logging.basicConfig(level=logging.INFO, format="%(name)s: %(message)s")

app = FastAPI(title="agents", version="0.1.0")
for router in api.routers:
    app.include_router(router)


@app.on_event("startup")
async def startup():
    agents.load()
    get_device()  # kick off the MQTT connection early
