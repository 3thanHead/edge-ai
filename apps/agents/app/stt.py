"""Speech-to-text over the device's mic stream.

Framework-free building blocks (no FastAPI imports, so this file is testable
standalone against a live hub):

    Segmenter    energy VAD over the firmware's 16 kHz mono int16 PCM frames
                 -- buffers speech, cuts on trailing silence, discards blips.
    Transcriber  lazy faster-whisper (CPU int8); feed it a segment's PCM,
                 get text back. Model + language via STT_MODEL / STT_LANG.

The wiring (hub -> Segmenter -> Transcriber -> transcript fan-out) lives in
api/audio.py; this module knows nothing about transport.
"""
import logging
import os

log = logging.getLogger("agents.stt")

SAMPLE_RATE = 16000  # the firmware's fixed format: 16 kHz mono int16 LE


def _rms(pcm: bytes) -> int:
    """Integer RMS of int16 LE PCM. Pure python; ~1k samples per call."""
    n = len(pcm) // 2
    if n == 0:
        return 0
    total = 0
    mv = memoryview(pcm).cast("h")
    for s in mv:
        total += s * s
    return int((total / n) ** 0.5)


class Segmenter:
    """Chops a continuous frame stream into speech segments.

    Feed frames in stream order; feed() returns a finished segment's PCM
    bytes when one closes, else None. Tuned for the firmware's 64 ms frames:
    speech opens after 2 consecutive voiced frames (with ~380 ms of pre-roll
    kept), closes after ~640 ms of silence, blips shorter than ~350 ms of
    voiced audio are discarded, and runaway segments cut at 15 s.
    """

    PRE_ROLL = 6          # frames kept before speech opens (~380 ms)
    OPEN_AFTER = 2        # consecutive voiced frames to open
    CLOSE_AFTER = 10      # consecutive silent frames to close (~640 ms)
    MIN_VOICED = 6        # voiced frames a segment needs to be worth keeping
    MAX_SECONDS = 15      # force-cut runaway segments

    def __init__(self):
        self.floor = 120.0        # adaptive noise floor (RMS, EMA)
        self.pre: list[bytes] = []
        self.seg: list[bytes] = []
        self.open = False
        self.voiced_run = 0
        self.silent_run = 0
        self.voiced_total = 0
        self.peak_rms = 0

    def threshold(self) -> int:
        # Tuned against real captures from the breadboard mics: speech frames
        # run RMS ~150-500 over a ~50-90 noise floor, so 3x-floor missed most
        # of it. 2.2x with a 150 absolute floor catches conversational speech;
        # whisper's non-speech filters mop up the extra false segments.
        return max(150, int(self.floor * 2.2))

    def feed(self, frame: bytes) -> bytes | None:
        rms = _rms(frame)
        voiced = rms > self.threshold()
        if not voiced:
            # only quiet frames teach the noise floor, so speech can't raise it
            self.floor = self.floor * 0.95 + rms * 0.05

        if not self.open:
            self.pre.append(frame)
            if len(self.pre) > self.PRE_ROLL:
                self.pre.pop(0)
            self.voiced_run = self.voiced_run + 1 if voiced else 0
            if self.voiced_run >= self.OPEN_AFTER:
                self.open = True
                self.seg = list(self.pre)
                self.pre = []
                self.silent_run = 0
                self.voiced_total = self.voiced_run
                self.peak_rms = rms
            return None

        self.seg.append(frame)
        if voiced:
            self.voiced_total += 1
            self.silent_run = 0
            self.peak_rms = max(self.peak_rms, rms)
        else:
            self.silent_run += 1

        seg_seconds = sum(len(f) for f in self.seg) / 2 / SAMPLE_RATE
        if self.silent_run >= self.CLOSE_AFTER or seg_seconds >= self.MAX_SECONDS:
            pcm = b"".join(self.seg)
            keep = self.voiced_total >= self.MIN_VOICED
            self.open = False
            self.seg = []
            self.voiced_run = 0
            return pcm if keep else None
        return None


class Transcriber:
    """faster-whisper wrapper: lazy model load, blocking transcribe().

    Call transcribe() from a worker thread (it's CPU-bound). Returns the
    recognized text, or "" when whisper judged the segment to be non-speech.
    """

    def __init__(self):
        self.model_name = os.environ.get("STT_MODEL", "base")
        self.language = os.environ.get("STT_LANG", "en")
        self._model = None

    def load(self):
        if self._model is None:
            from faster_whisper import WhisperModel
            log.info("loading whisper model '%s' (cpu/int8)...", self.model_name)
            self._model = WhisperModel(self.model_name, device="cpu",
                                       compute_type="int8")
            log.info("whisper model ready")
        return self._model

    def transcribe(self, pcm: bytes) -> str:
        import numpy as np  # ships with ctranslate2/faster-whisper
        audio = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0
        # The breadboard mics run quiet; normalize toward full scale (capped
        # at 20x so pure noise isn't blown up) -- whisper degrades badly on
        # low-level audio and starts repetition-looping.
        peak = float(np.max(np.abs(audio))) if audio.size else 0.0
        if peak > 0:
            audio = audio * min(0.9 / peak, 20.0)
        segments, _info = self.load().transcribe(
            audio, language=self.language, beam_size=2,
            condition_on_previous_text=False, vad_filter=False)
        parts = []
        for seg in segments:
            # Whisper hallucinates pleasantries on noise and loops phrases on
            # garbage; drop non-speech, low-confidence, and loopy segments.
            if seg.no_speech_prob > 0.6 or seg.avg_logprob < -1.2:
                continue
            if seg.compression_ratio > 2.2:
                continue
            parts.append(seg.text.strip())
        return " ".join(p for p in parts if p).strip()


def available() -> bool:
    """True when faster-whisper is importable (STT can run)."""
    try:
        import faster_whisper  # noqa: F401
        return True
    except ImportError:
        return False
