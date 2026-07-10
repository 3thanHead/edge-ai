"""Audio handlers -- the ESP32 pushes mic frames in on one WebSocket, any
number of consumers (agents, debug clients) subscribe on another. An STT
worker rides the same hub and fans recognized speech out as transcripts.

WS
    /ws/audio/ingest        the ESP32 pushes binary mic frames here
    /ws/audio/subscribe     consumers receive those frames fanned out
    /ws/audio/transcripts   JSON transcript events from the STT worker
REST
    GET /api/audio/transcripts   the most recent transcripts

Frames are opaque binary blobs (raw PCM from the codec); the hub never
touches the payload, it only fans out. Slow subscribers get dropped frames,
not backpressure -- a live mic stream must never stall the ingest side.

Current producer format (firmware's net/AudioStream, fw 0.4): 16 kHz mono
int16 LE PCM in 1024-sample (2048-byte, 64 ms) frames. Consumers that render
or transcribe should assume this shape; the hub itself doesn't care.

STT: energy VAD (app/stt.py) chops the stream into utterances, faster-whisper
(CPU int8, model from STT_MODEL, default "base") transcribes each in a worker
thread. Absent faster-whisper or STT_DISABLED=1 -> the worker just doesn't
start; everything else is unaffected.
"""
import asyncio
import collections
import logging
import os
import time

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from .. import stt

log = logging.getLogger("agents.audio")

router = APIRouter()

_QUEUE_FRAMES = 64  # ~ a couple seconds of 20 ms frames per subscriber


class AudioHub:
    def __init__(self):
        self._subscribers: set[asyncio.Queue] = set()
        # Count, not bool: a rebooting device briefly overlaps its old zombie
        # socket with the new one; the zombie's exit must not mark the fresh
        # connection as down.
        self.ingest_count = 0
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
        return {"ingest_connected": self.ingest_count > 0,
                "frames_in": self.frames_in,
                "subscribers": len(self._subscribers),
                "stt": transcripts.stats()}


hub = AudioHub()


class TranscriptHub:
    """Fan-out + short history for STT results (JSON dicts, not PCM)."""

    def __init__(self):
        self._subscribers: set[asyncio.Queue] = set()
        self.recent: collections.deque = collections.deque(maxlen=50)
        self.state = "off"  # off | loading | listening

    def publish(self, event: dict):
        self.recent.append(event)
        for q in self._subscribers:
            try:
                q.put_nowait(event)
            except asyncio.QueueFull:
                pass  # transcripts are tiny; a stuck client just misses some

    def subscribe(self) -> asyncio.Queue:
        q: asyncio.Queue = asyncio.Queue(maxsize=16)
        self._subscribers.add(q)
        return q

    def unsubscribe(self, q: asyncio.Queue):
        self._subscribers.discard(q)

    def stats(self) -> dict:
        return {"state": self.state, "transcripts": len(self.recent),
                "subscribers": len(self._subscribers)}


transcripts = TranscriptHub()


async def _stt_worker():
    """hub PCM -> Segmenter -> Transcriber (thread) -> transcript fan-out."""
    transcriber = stt.Transcriber()
    transcripts.state = "loading"
    try:
        # Load up front (downloads the model on first ever run) so the first
        # utterance isn't paying the load inside its latency.
        await asyncio.to_thread(transcriber.load)
    except Exception as e:
        transcripts.state = "off"
        log.error("stt disabled: whisper model load failed: %s", e)
        return
    transcripts.state = "listening"
    log.info("stt listening (model=%s)", transcriber.model_name)

    seg = stt.Segmenter()
    q = hub.subscribe()
    try:
        while True:
            pcm = seg.feed(await q.get())
            if pcm is None:
                continue
            dur = len(pcm) / 2 / stt.SAMPLE_RATE
            text = await asyncio.to_thread(transcriber.transcribe, pcm)
            if not text:
                continue
            event = {"text": text, "dur_s": round(dur, 2),
                     "rms": seg.peak_rms, "ts": time.time()}
            log.info("stt: %r (%.1fs)", text, dur)
            transcripts.publish(event)
    finally:
        hub.unsubscribe(q)


_stt_task: asyncio.Task | None = None


@router.on_event("startup")
async def _start_stt():
    # Idempotent: the startup hook can fire more than once (router event
    # handlers get merged into the app), and two workers means every
    # utterance is transcribed and published twice.
    global _stt_task
    if _stt_task is not None:
        log.info("stt worker already running; ignoring duplicate startup")
        return
    if os.environ.get("STT_DISABLED") == "1":
        log.info("stt disabled via STT_DISABLED")
        return
    if not stt.available():
        log.warning("stt off: faster-whisper not installed")
        return
    _stt_task = asyncio.create_task(_stt_worker())


@router.websocket("/ws/audio/ingest")
async def ws_audio_ingest(ws: WebSocket):
    """One producer: the ESP32. Binary frames only; anything else is ignored."""
    await ws.accept()
    hub.ingest_count += 1
    log.info("audio ingest connected (%d live)", hub.ingest_count)
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
        hub.ingest_count -= 1
        log.info("audio ingest disconnected (%d live, %d frames total)",
                 hub.ingest_count, hub.frames_in)


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


@router.websocket("/ws/audio/transcripts")
async def ws_audio_transcripts(ws: WebSocket):
    await ws.accept()
    # State first, then history, so a late joiner still sees recent speech.
    await ws.send_json({"type": "stt_state", "state": transcripts.state})
    for event in list(transcripts.recent)[-10:]:
        await ws.send_json({"type": "transcript", **event})
    q = transcripts.subscribe()
    try:
        while True:
            await ws.send_json({"type": "transcript", **(await q.get())})
    except WebSocketDisconnect:
        pass
    finally:
        transcripts.unsubscribe(q)


@router.get("/api/audio/transcripts")
async def recent_transcripts():
    return {"state": transcripts.state, "transcripts": list(transcripts.recent)}
