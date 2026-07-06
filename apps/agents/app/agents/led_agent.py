"""led -- the LED signaling agent.

Its whole world is the two breadboard LEDs. It decides how to blink one or
both (rates, patterns) and answers yes/no questions with them:

    red  (led_1, GPIO 4)  = no  / false
    blue (led_2, GPIO 5)  = yes / true

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
from langchain_core.tools import tool

from ..api.device import get_device
from . import events
from .base import BaseAgent

# Which physical component each logical LED name maps to.
LEDS = {"red": "led_1", "blue": "led_2", "both": "leds"}
PATTERNS = ("sos", "heartbeat", "strobe")
# Words small models like to use as rates.
SPEED_MS = {"fast": "150", "quick": "150", "rapid": "150",
            "normal": "500", "medium": "500", "slow": "1200"}


def _normalize(led: str, action: str, arg: str) -> tuple[str, str, str] | dict:
    """Forgive the ways small models phrase LED commands, deterministically:
    action='sos' -> pattern sos; arg='fast' -> ms; blink on 'both' -> together.
    Returns (component, action, arg) or an {'error': ...} the model can fix."""
    name = LEDS.get(led.lower().strip())
    if name is None:
        return {"error": f"unknown led '{led}', use red|blue|both"}
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
    """Control an LED. led: "red", "blue" or "both".
    Actions for red/blue: on | off | blink (arg=interval ms) | pattern
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
    description = ("Signals with the two breadboard LEDs: blink rates, "
                   "patterns, and yes/no answers (blue=yes, red=no).")

    def system_prompt(self) -> str:
        return (
            "You are the LED signaling agent for a physical IoT device with "
            "two LEDs (red and blue).\n\n"
            "If the user asks you to control the LEDs ('blink the blue one "
            "fast', 'red LED SOS', 'both alternate'): call set_led -- rates: "
            "fast=150ms, normal=500ms, slow=1200ms; patterns: sos, heartbeat, "
            'strobe. Then reply with JSON {"reasoning": "<one sentence>", '
            '"message": "<one short sentence>"}.\n\n'
            "For ANY other request, treat it as a yes/no question about the "
            "world. Never call a tool for these. Work out the true answer "
            "from facts, then reply with JSON with keys in exactly this "
            "order:\n"
            '{"reasoning": "<your step-by-step thinking>", "answer": "yes" '
            'or "no", "message": "<one short sentence stating the answer>"}\n'
            "The hardware shows your answer on the LEDs for you.\n\n"
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
        """The model decided; the LEDs are set here, deterministically."""
        answer = output.get("answer")
        if answer not in ("yes", "no"):
            return
        # The model sometimes tacks a bogus "answer" onto a control run
        # ('blink the blue one') -- actuating it would clobber the LED state
        # the user just asked for. Only show an answer when the input
        # actually reads like a question; otherwise drop it.
        q = input_text.lower().strip()
        looks_question = q.endswith("?") or q.startswith(
            ("is ", "are ", "can ", "does ", "do ", "did ", "will ", "would ",
             "was ", "were ", "should ", "could ", "has ", "have ", "am ",
             "who ", "what ", "when ", "where ", "why ", "how "))
        if not looks_question and any(t["tool"] == "set_led" for t in trace):
            output["answer"] = None
            return
        yield events.tool_call("show_answer", {"answer": answer})
        result = await show_answer(answer)
        yield events.tool_result("show_answer", result)
        output["actions"].append({
            "tool": "show_answer", "args": {"answer": answer},
            "ok": not (isinstance(result, dict) and result.get("error"))})


AGENT = LedAgent()
