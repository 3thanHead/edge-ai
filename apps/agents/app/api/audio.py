"""Audio handlers -- the ESP32 pushes mic frames in on one WebSocket, any
number of consumers (agents, debug clients) subscribe on another.

WS
    /ws/audio/ingest      the ESP32 pushes binary mic frames here
    /ws/audio/subscribe   consumers receive those frames fanned out

Frames are opaque binary blobs (raw PCM from the codec); the hub never
touches the payload, it only fans out. Slow subscribers get dropped frames,
not backpressure -- a live mic stream must never stall the ingest side.
"""
import asyncio
import logging

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

log = logging.getLogger("agents.audio")

router = APIRouter()

_QUEUE_FRAMES = 64  # ~ a couple seconds of 20 ms frames per subscriber


class AudioHub:
    def __init__(self):
        self._subscribers: set[asyncio.Queue] = set()
        self.ingest_connected = False
        self.frames_in = 0

    def publish(self, frame: bytes):
        self.frames_in += 1
        for q in self._subscribers:
            try:
                q.put_nowait(frame)
            except asyncio.QueueFull:
                # Drop oldest so the subscriber stays roughly live.
                try:
                    q.get_nowait()
                    q.put_nowait(frame)
                except asyncio.QueueEmpty:
                    pass

    def subscribe(self) -> asyncio.Queue:
        q: asyncio.Queue = asyncio.Queue(maxsize=_QUEUE_FRAMES)
        self._subscribers.add(q)
        log.info("audio subscriber added (%d total)", len(self._subscribers))
        return q

    def unsubscribe(self, q: asyncio.Queue):
        self._subscribers.discard(q)

    def stats(self) -> dict:
        return {"ingest_connected": self.ingest_connected,
                "frames_in": self.frames_in,
                "subscribers": len(self._subscribers)}


hub = AudioHub()


@router.websocket("/ws/audio/ingest")
async def ws_audio_ingest(ws: WebSocket):
    """One producer: the ESP32. Binary frames only; anything else is ignored."""
    await ws.accept()
    hub.ingest_connected = True
    log.info("audio ingest connected")
    try:
        while True:
            msg = await ws.receive()
            if msg.get("bytes") is not None:
                hub.publish(msg["bytes"])
            elif msg.get("type") == "websocket.disconnect":
                break
    except WebSocketDisconnect:
        pass
    finally:
        hub.ingest_connected = False
        log.info("audio ingest disconnected (%d frames total)", hub.frames_in)


@router.websocket("/ws/audio/subscribe")
async def ws_audio_subscribe(ws: WebSocket):
    await ws.accept()
    q = hub.subscribe()
    try:
        while True:
            await ws.send_bytes(await q.get())
    except WebSocketDisconnect:
        pass
    finally:
        hub.unsubscribe(q)
