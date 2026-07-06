#include "components/LedGroup.h"

bool LedGroup::handleCommand(const String& action, const String& arg) {
  uint32_t ms = arg.length() ? (uint32_t)arg.toInt() : 500;

  if (action == "alternate") {
    // Set opposite states, then blink both at the same interval -- each
    // toggle preserves the anti-phase.
    red_.handleCommand("on", "");
    blue_.handleCommand("off", "");
    red_.blink(ms);
    blue_.blink(ms);
    mode_ = "alternate " + String(ms) + "ms";
    return true;
  }
  if (action == "together") {
    red_.handleCommand("on", "");
    blue_.handleCommand("on", "");
    red_.blink(ms);
    blue_.blink(ms);
    mode_ = "together " + String(ms) + "ms";
    return true;
  }
  if (action == "pattern") {
    if (!red_.playPattern(arg)) return false;
    blue_.playPattern(arg);
    mode_ = "pattern " + arg;
    return true;
  }
  if (action == "answer") {
    Led& lit = (arg == "yes" || arg == "true") ? blue_ : red_;
    Led& dark = (&lit == &blue_) ? red_ : blue_;
    dark.handleCommand("off", "");
    lit.playPattern("heartbeat");
    mode_ = "answer " + String(&lit == &blue_ ? "yes" : "no");
    return true;
  }
  if (action == "on") {
    red_.handleCommand("on", "");
    blue_.handleCommand("on", "");
    mode_ = "on";
    return true;
  }
  if (action == "off" || action == "solid" || action == "stop") {
    red_.handleCommand(action == "off" ? "off" : "solid", "");
    blue_.handleCommand(action == "off" ? "off" : "solid", "");
    mode_ = action == "off" ? "off" : "idle";
    return true;
  }
  return false;
}

String LedGroup::status() const { return mode_; }
