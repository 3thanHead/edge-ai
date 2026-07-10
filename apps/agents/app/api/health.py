"""GET /health -- liveness plus a one-look status of everything downstream."""
from fastapi import APIRouter

from .. import agents
from .audio import hub
from .device import get_device

router = APIRouter()


@router.get("/health")
async def health():
    try:
        device = await get_device().health()
    except Exception as e:
        device = {"error": str(e)}
    return {"ok": True, "agents": [a["name"] for a in agents.describe()],
            "device": device, "audio": hub.stats()}
