#!/usr/bin/env python3
"""iot-assistant agent -- an LLM on the home-lab cluster driving the ESP32.

Give it an instruction in plain English; it tool-calls the device's HTTP API
(list_components / control_component) until the job is done.

    python app/main.py "blink the green led fast and write hello on the lcd"
    python app/main.py --device http://192.168.1.50 "turn everything off"

Endpoints (env-overridable, no IPs committed):
    LLM_BASE_URL    Ollama/HAProxy master, else read from infra/llm-cluster/
                    fleet.json (edge fleet), else http://localhost:11434
    IOT_DEVICE_URL  the ESP32, default http://iot-assistant.local
    LLM_MODEL       default llama3.2:3b (must support tool calling)
"""
import argparse
import json
import os
import sys
import urllib.request
from pathlib import Path

from device import IotDevice, DEFAULT_URL

REPO_ROOT = Path(__file__).resolve().parents[3]
FLEET = REPO_ROOT / "infra" / "llm-cluster" / "fleet.json"

# The device's action vocabulary, told to the model up front so it doesn't
# have to guess. Keep in sync with the firmware components' handleCommand().
ACTIONS_HELP = """\
Component action reference:
- led_green / led_yellow / led_red : on | off | toggle | blink <ms> | solid | brightness <0-255>
- leds (all three as a unit) : alternate <ms> | together <ms> | answer <yes|no|maybe> | off
- lcd (2.0" color) : text <msg, '|' = newline> | clear [color] | backlight <on|off|0-255>
- oled_1 / oled_2 (0.96" mono) : text <msg, '|' = newline> | clear | invert <on|off>
- audio : beep | tone <hz>[,ms] | volume <0-100> | amp <on|off>
`arg` is a single string, e.g. control_component("lcd","text","hello|world")."""

TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "list_components",
            "description": "List every component on the IoT device with its "
                           "name and current status.",
            "parameters": {"type": "object", "properties": {}},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "control_component",
            "description": "Send an action to one component on the IoT device. "
                           + ACTIONS_HELP,
            "parameters": {
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "component name, e.g. led_green"},
                    "action": {"type": "string", "description": "action verb, e.g. blink"},
                    "arg": {"type": "string", "description": "optional argument, e.g. '250'"},
                },
                "required": ["name", "action"],
            },
        },
    },
]


def llm_base_url() -> str:
    if os.environ.get("LLM_BASE_URL"):
        return os.environ["LLM_BASE_URL"].rstrip("/")
    if FLEET.exists():
        host = json.loads(FLEET.read_text()).get("master", {}).get("host", "")
        if host:
            return f"http://{host}:11434"
    return "http://localhost:11434"


def chat(base_url: str, model: str, messages: list) -> dict:
    payload = json.dumps({"model": model, "messages": messages,
                          "tools": TOOLS, "stream": False}).encode()
    req = urllib.request.Request(
        f"{base_url}/api/chat", data=payload,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read().decode())["message"]


def run_tool(dev: IotDevice, name: str, args: dict) -> str:
    if name == "list_components":
        return json.dumps(dev.components())
    if name == "control_component":
        return json.dumps(dev.command(
            args.get("name", ""), args.get("action", ""),
            str(args.get("arg", "") or "")))
    return json.dumps({"error": f"unknown tool {name}"})


def main():
    ap = argparse.ArgumentParser(description="LLM agent for the iot-assistant device")
    ap.add_argument("instruction", help="what to do, in plain English")
    ap.add_argument("--device", default=DEFAULT_URL, help="device base URL")
    ap.add_argument("--model", default=os.environ.get("LLM_MODEL", "llama3.2:3b"))
    ap.add_argument("--max-steps", type=int, default=8)
    args = ap.parse_args()

    dev = IotDevice(args.device)
    base = llm_base_url()

    # Fail fast with a clear message if either end is down.
    try:
        health = dev.health()
    except OSError as e:
        sys.exit(f"device unreachable at {args.device}: {e}\n"
                 f"(set IOT_DEVICE_URL or --device to the ESP32's IP)")
    print(f"device: {health['device']} fw {health['version']} @ {args.device}")
    print(f"llm:    {args.model} @ {base}")

    messages = [
        {"role": "system", "content":
            "You control a physical IoT device via tools. Use list_components "
            "to see what exists, then control_component to act. Perform the "
            "user's request, then reply with one short sentence summarizing "
            "what you did.\n" + ACTIONS_HELP},
        {"role": "user", "content": args.instruction},
    ]

    for _ in range(args.max_steps):
        msg = chat(base, args.model, messages)
        messages.append(msg)
        calls = msg.get("tool_calls") or []
        if not calls:
            print(f"agent: {msg.get('content', '').strip()}")
            return
        for call in calls:
            fn = call["function"]
            fn_args = fn.get("arguments") or {}
            if isinstance(fn_args, str):
                fn_args = json.loads(fn_args or "{}")
            result = run_tool(dev, fn["name"], fn_args)
            print(f"  tool: {fn['name']}({json.dumps(fn_args)}) -> {result}")
            messages.append({"role": "tool", "content": result})

    print("agent: stopped after max steps")


if __name__ == "__main__":
    main()
