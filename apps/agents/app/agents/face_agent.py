"""face -- the LCD face emotion agent.

Reads a chat message and decides which face the robot should make on the
ST7789 (EMO-style eyes). Emotions land on the device over MQTT
(fire-and-forget -- an emotion change needs no ack), falling back to HTTP
when no broker is configured.

final.output schema:
    {"emotion": "<one of the moods>", "reasoning": "<one sentence>"}
"""
from langchain_core.tools import tool

from ..api.device import get_device
from .base import BaseAgent

MOODS = ["neutral", "happy", "sad", "angry", "sleepy", "surprised"]


@tool
async def show_emotion(emotion: str) -> dict:
    """Show an emotion on the robot's face. One of: neutral, happy, sad,
    angry, sleepy, surprised."""
    mood = emotion.lower().strip()
    if mood not in MOODS:
        return {"error": f"unknown emotion '{emotion}', use one of {MOODS}"}
    dev = get_device()
    if dev.publish_command("face", mood):
        return {"ok": True, "emotion": mood, "via": "mqtt"}
    return await dev.command("face", mood)


class FaceAgent(BaseAgent):
    name = "face"
    description = ("Picks the robot's facial expression (LCD eyes) to match "
                   "the mood of a chat message.")
    max_steps = 3

    def system_prompt(self) -> str:
        return (
            "You choose the facial expression for a small robot companion. "
            "Given a chat message, judge its emotional tone from the robot's "
            "point of view and call show_emotion with exactly one of: "
            f"{', '.join(MOODS)}.\n"
            "Guidance: good news/jokes/greetings=happy, bad news/goodbyes=sad, "
            "insults/frustration=angry, questions/surprises/exclamations="
            "surprised, boring or late-night chatter=sleepy, otherwise neutral.\n\n"
            "Then reply with ONLY this JSON object:\n"
            '{"emotion": "<the mood you chose>", "reasoning": "<one sentence>"}'
        )

    def tools(self):
        return [show_emotion]

    def build_output(self, model_json: dict, raw_text: str,
                     trace: list[dict]) -> dict:
        emotion = None
        for t in trace:
            ok = not (isinstance(t["result"], dict) and t["result"].get("error"))
            if t["tool"] == "show_emotion" and ok:
                emotion = t["result"].get("emotion") if isinstance(t["result"], dict) else None
                emotion = emotion or str(t["args"].get("emotion") or "").lower()
        return {"emotion": emotion,
                "reasoning": str(model_json.get("reasoning") or raw_text or "")}


AGENT = FaceAgent()
