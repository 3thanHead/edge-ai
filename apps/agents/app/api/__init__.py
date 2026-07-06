"""The API surface, one module per route group (see docs/api.md):

    health.py    GET /health
    agents.py    /api/agents/* + /ws/agents/{name}
    device.py    /api/device/* + the ESP32 client the agents actuate through
    audio.py     /ws/audio/*   + the mic-frame fan-out hub

Each module owns everything for its surface -- routes, state, config -- and
exports `router`; main.py just mounts them.
"""
from . import agents, audio, device, health

routers = [health.router, agents.router, device.router, audio.router]
