"""led -- the LED signaling agent.

Its whole world is the three breadboard LEDs. It decides how to blink one or
all of them (rates, patterns) and answers yes/no questions with them:

    green  (led_green,  GPIO 21) = yes / true
    yellow (led_yellow, GPIO 47) = maybe / unknown
    red    (led_red,    GPIO 48) = no  / false

Commands land on the device over HTTP (acked), so the agent knows each
change took effect.

final.output schema (the "consistent JSON" contract):
    {
      "answer":    "yes" | "no" | null,   # null when not a yes/no question
      "reasoning": "<one sentence>",
      "actions":   [{"led": "...", "action": "...", "arg": "..."}],
      "message":   "<one sentence for the user>"
    }
"""
import re

from langchain_core.tools import tool

from ..api.device import get_device
from . import events
from .base import BaseAgent

# Which physical component each logical LED name maps to. "blue" is kept as an
# alias for green so older muscle-memory commands still land right.
LEDS = {"red": "led_red", "yellow": "led_yellow", "green": "led_green",
        "blue": "led_green", "both": "leds", "all": "leds"}
PATTERNS = ("sos", "heartbeat", "strobe")
# Words small models like to use as rates.
SPEED_MS = {"fast": "150", "quick": "150", "rapid": "150",
            "normal": "500", "medium": "500", "slow": "1200"}

# Tell a control command ('blink the green led', 'stop both lights') from a
# yes/no question ('is the sky blue?') by the user's WORDS, so a small model's
# stray "answer" on a control run doesn't light an answer LED. A request is a
# control command if it names an LED/light, uses an LED-specific verb, or pairs
# a generic on/off/stop/turn verb with a colour. Bare 'is the stove on?' or
# 'the earth is flat' stay questions.
_LED_NOUNS = {"led", "leds", "light", "lights", "lamp", "lamps"}
_LED_VERBS = {"blink", "blinking", "sos", "strobe", "alternate", "pulse",
              "pulsing", "brightness", "toggle", "solid", "dim", "flash", "flashing"}
_STATE_VERBS = {"on", "off", "stop", "turn", "enable", "disable", "shut"}
_COLORS = {"red", "yellow", "green", "blue", "both", "all"}


def _is_led_command(text: str) -> bool:
    words = set(re.findall(r"[a-z]+", text.lower()))
    if words & (_LED_NOUNS | _LED_VERBS):
        return True
    return bool((words & _STATE_VERBS) and (words & _COLORS))


def _normalize(led: str, action: str, arg: str) -> tuple[str, str, str] | dict:
    """Forgive the ways small models phrase LED commands, deterministically:
    action='sos' -> pattern sos; arg='fast' -> ms; blink on 'both' -> together.
    Returns (component, action, arg) or an {'error': ...} the model can fix."""
    name = LEDS.get(led.lower().strip())
    if name is None:
        return {"error": f"unknown led '{led}', use red|yellow|green|both"}
    a = action.lower().strip()
    arg = str(arg).lower().strip()

    if a in PATTERNS:                      # "action": "sos"
        a, arg = "pattern", a
    if a in SPEED_MS:                      # "action": "fast"
        a, arg = "blink", SPEED_MS[a]
    if arg in SPEED_MS:
        arg = SPEED_MS[arg]
    if a == "pattern" and arg not in PATTERNS:
        return {"error": f"unknown pattern '{arg}', use one of {list(PATTERNS)}"}
    if name == "leds" and a == "blink":    # group speaks together/alternate
        a = "together"
    if name != "leds" and a in ("alternate", "together"):
        name, a = "leds", "alternate" if a == "alternate" else "together"

    allowed_single = ("on", "off", "toggle", "blink", "solid", "pattern",
                      "pulse", "seq", "brightness")
    allowed_group = ("alternate", "together", "pattern", "answer", "on", "off", "solid")
    allowed = allowed_group if name == "leds" else allowed_single
    if a not in allowed:
        return {"error": f"unknown action '{action}' for {led}, use one of {list(allowed)}"}
    return (name, a, arg)


@tool
async def set_led(led: str, action: str, arg: str = "") -> dict:
    """Control an LED. led: "red", "yellow", "green" or "both" (all three).
    Actions for a single LED: on | off | blink (arg=interval ms) | pattern
    (arg=sos|heartbeat|strobe) | pulse (arg=period ms) | seq (arg=on,off,... ms).
    Actions for both: alternate (arg=ms) | together (arg=ms) |
    pattern (arg=name) | off."""
    cmd = _normalize(led, action, arg)
    if isinstance(cmd, dict):
        return cmd
    return await get_device().command(*cmd)


def _norm_answer(value) -> str | None:
    if isinstance(value, bool):
        return "yes" if value else "no"
    s = str(value).lower().strip()
    if s in ("yes", "true", "1"):
        return "yes"
    if s in ("no", "false", "0"):
        return "no"
    return None


# NOTE deliberately NOT a model tool: small models reason correctly but then
# wire the wrong verdict into tool args (Ollama emits args alphabetically, so
# "answer" gets generated before "reasoning" -- an answer without its chain
# of thought). The verdict is read from the final JSON instead, where the
# prompt forces reasoning-first order, and actuated here in code.
async def show_answer(answer: str) -> dict:
    return await get_device().command("leds", "answer", answer)


class LedAgent(BaseAgent):
    name = "led"
    description = ("Signals with the three breadboard LEDs: blink rates, "
                   "patterns, and yes/no answers (green=yes, red=no).")

    def system_prompt(self) -> str:
        return (
            "You are the LED signaling agent for a physical IoT device with "
            "three LEDs (red, yellow and green).\n\n"
            "If the user asks you to control the LEDs ('blink the green one "
            "fast', 'red LED SOS', 'both alternate'): call set_led -- rates: "
            "fast=150ms, normal=500ms, slow=1200ms; patterns: sos, heartbeat, "
            'strobe. Then reply with JSON {"reasoning": "<one sentence>", '
            '"message": "<one short sentence>"}.\n\n'
            "For ANY other request, treat it as a yes/no question about the "
            "world. Do NOT call any tool for these -- just reason and answer. "
            "Think first, then give a verdict that MUST match your reasoning's "
            'conclusion: "answer" is "yes" if the statement is true, "no" if '
            "it is false. Reply with ONLY this JSON, keys in this exact order:\n"
            '{"reasoning": "<one or two sentences ending in your conclusion>", '
            '"answer": "yes" or "no", "message": "<one short sentence stating the answer>"}\n'
            "Examples:\n"
            'Q: Is grass green? {"reasoning": "Grass contains chlorophyll, '
            'which is green, so grass is green.", "answer": "yes", "message": '
            '"Yes, grass is green."}\n'
            'Q: Is the moon made of cheese? {"reasoning": "The moon is rock '
            'and dust, not cheese.", "answer": "no", "message": "No, the moon '
            'is not made of cheese."}\n'
            "The hardware shows your answer on the LEDs (green = yes, red = no).\n\n"
            "Reply with JSON only."
        )

    def tools(self):
        return [set_led]

    def build_output(self, model_json: dict, raw_text: str,
                     trace: list[dict]) -> dict:
        """Actions come from what actually happened (the trace); the verdict
        and prose come from the model's final JSON."""
        answer = _norm_answer(model_json.get("answer")) \
            if "answer" in model_json else None
        actions = [
            {"tool": t["tool"], "args": t["args"],
             "ok": not (isinstance(t["result"], dict) and t["result"].get("error"))}
            for t in trace
        ]
        message = str(model_json.get("message") or "").strip()
        if not message:
            if answer:
                message = f"The answer is {answer}."
            elif actions:
                message = "LEDs updated."
            else:
                message = raw_text or "I can only signal with the two LEDs."
        return {"answer": answer,
                "reasoning": str(model_json.get("reasoning") or ""),
                "actions": actions, "message": message}

    async def act_on_output(self, output, trace, input_text):
        """The model decided; the LEDs are set here, deterministically.

        show_answer uses the device's `leds answer` action, which lights the
        verdict's LED (green=yes, red=no) AND turns the other one off -- so a
        yes/no answer always leaves a single clean LED, whatever the LEDs were
        doing before."""
        answer = output.get("answer")
        if answer not in ("yes", "no"):
            return
        # A control command ('blink the green one') that the model wrongly
        # tagged with a yes/no answer must not trigger an answer heartbeat --
        # it would clobber the LEDs the user asked for. Judge control-vs-
        # question by the user's WORDS (device-control vocabulary a factual
        # question wouldn't use), not by punctuation: 'the earth is flat' and
        # 'true or false: fire is cold' are questions and must still answer.
        if _is_led_command(input_text):
            output["answer"] = None
            return
        yield events.tool_call("show_answer", {"answer": answer})
        result = await show_answer(answer)
        yield events.tool_result("show_answer", result)
        output["actions"].append({
            "tool": "show_answer", "args": {"answer": answer},
            "ok": not (isinstance(result, dict) and result.get("error"))})


AGENT = LedAgent()
