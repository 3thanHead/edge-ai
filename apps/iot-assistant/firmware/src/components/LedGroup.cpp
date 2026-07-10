#include "components/LedGroup.h"

void LedGroup::each(const String& action, const String& arg) {
  green_.handleCommand(action, arg);
  yellow_.handleCommand(action, arg);
  red_.handleCommand(action, arg);
}

bool LedGroup::handleCommand(const String& action, const String& arg) {
  uint32_t ms = arg.length() ? (uint32_t)arg.toInt() : 500;

  if (action == "alternate") {
    // Green+red start ON, yellow OFF, then everything blinks at the same
    // interval -- each toggle preserves the anti-phase split.
    green_.handleCommand("on", "");
    red_.handleCommand("on", "");
    yellow_.handleCommand("off", "");
    green_.blink(ms);
    yellow_.blink(ms);
    red_.blink(ms);
    mode_ = "alternate " + String(ms) + "ms";
    return true;
  }
  if (action == "together") {
    each("on", "");
    green_.blink(ms);
    yellow_.blink(ms);
    red_.blink(ms);
    mode_ = "together " + String(ms) + "ms";
    return true;
  }
  if (action == "pattern") {
    if (!green_.playPattern(arg)) return false;
    yellow_.playPattern(arg);
    red_.playPattern(arg);
    mode_ = "pattern " + arg;
    return true;
  }
  if (action == "answer") {
    String verdict = arg;
    verdict.toLowerCase();
    Led* lit = &red_;
    const char* label = "no";
    if (verdict == "yes" || verdict == "true") {
      lit = &green_;
      label = "yes";
    } else if (verdict == "maybe" || verdict == "unknown" || verdict == "unsure") {
      lit = &yellow_;
      label = "maybe";
    }
    for (Led* led : {&green_, &yellow_, &red_}) {
      if (led != lit) led->handleCommand("off", "");
    }
    lit->playPattern("heartbeat");
    mode_ = "answer " + String(label);
    return true;
  }
  if (action == "on") {
    each("on", "");
    mode_ = "on";
    return true;
  }
  if (action == "off" || action == "solid" || action == "stop") {
    each(action == "off" ? "off" : "solid", "");
    mode_ = action == "off" ? "off" : "idle";
    return true;
  }
  return false;
}

String LedGroup::status() const { return mode_; }
