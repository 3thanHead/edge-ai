#include "components/OnboardRgb.h"

OnboardRgb::OnboardRgb(const char* name, int pin)
    : Component(name), pin_(pin) {}

void OnboardRgb::begin() { apply(); }

void OnboardRgb::apply() {
  if (on_) {
    neopixelWrite(pin_, r_, g_, b_);
  } else {
    neopixelWrite(pin_, 0, 0, 0);
  }
}

void OnboardRgb::set(bool on) {
  on_ = on;
  apply();
}

void OnboardRgb::setColor(uint8_t r, uint8_t g, uint8_t b) {
  r_ = r; g_ = g; b_ = b;
  on_ = (r || g || b);
  apply();
}

void OnboardRgb::loop() {
  if (blinkInterval_ == 0) return;
  uint32_t now = millis();
  if (now - lastToggle_ >= blinkInterval_) {
    lastToggle_ = now;
    set(!on_);
  }
}

bool OnboardRgb::handleCommand(const String& action, const String& arg) {
  if (action == "on") { blinkInterval_ = 0; set(true); return true; }
  if (action == "off") { blinkInterval_ = 0; set(false); return true; }
  if (action == "toggle") { set(!on_); return true; }
  if (action == "color") {
    int c1 = arg.indexOf(',');
    int c2 = arg.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) return false;
    setColor((uint8_t)constrain(arg.substring(0, c1).toInt(), 0, 255),
             (uint8_t)constrain(arg.substring(c1 + 1, c2).toInt(), 0, 255),
             (uint8_t)constrain(arg.substring(c2 + 1).toInt(), 0, 255));
    return true;
  }
  if (action == "blink") {
    blinkInterval_ = arg.length() ? (uint32_t)arg.toInt() : 500;
    lastToggle_ = millis();
    return true;
  }
  if (action == "solid" || action == "stop") { blinkInterval_ = 0; return true; }
  return false;
}

String OnboardRgb::status() const {
  String s = on_ ? "on " : "off ";
  s += String(r_) + "," + String(g_) + "," + String(b_);
  if (blinkInterval_ > 0) s += " blink " + String(blinkInterval_) + "ms";
  return s;
}
