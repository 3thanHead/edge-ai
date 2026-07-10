#include "components/Led.h"

// Named patterns: ms durations, on/off alternating, first entry is ON.
// The long final OFF is the gap before the pattern repeats.
static const uint16_t kSos[] = {150, 150, 150, 150, 150, 500,   // S: dot dot dot
                                450, 150, 450, 150, 450, 500,   // O: dash dash dash
                                150, 150, 150, 150, 150, 1400}; // S + gap
static const uint16_t kHeartbeat[] = {80, 120, 80, 720};        // lub-dub ... rest
static const uint16_t kStrobe[] = {40, 60};

Led::Led(const char* name, int pin, int ledcChannel, bool activeHigh)
    : DigitalOutput(name, pin, activeHigh), channel_(ledcChannel) {}

void Led::begin() {
  ledcSetup(channel_, kPwmFreqHz, kPwmResBits);
  ledcAttachPin(pin_, channel_);
  write(false);
}

void Led::write(bool value) {
  uint8_t duty = value ? brightness_ : 0;
  if (!activeHigh_) duty = 255 - duty;  // invert for current-sinking wiring
  ledcWrite(channel_, duty);
  state_ = value;
}

void Led::setBrightness(uint8_t brightness) {
  brightness_ = brightness;
  if (state_) write(true);  // re-apply immediately if currently lit
}

void Led::blink(uint32_t intervalMs) {
  mode_ = Mode::BLINK;
  blinkInterval_ = intervalMs;
  lastToggle_ = millis();
}

void Led::startSteps(const uint16_t* steps, size_t count, const char* label) {
  stepCount_ = min(count, kMaxSteps);
  memcpy(steps_, steps, stepCount_ * sizeof(uint16_t));
  stepIdx_ = 0;
  stepStart_ = millis();
  patternName_ = label;
  mode_ = Mode::PATTERN;
  write(true);  // step 0 is always ON
}

bool Led::playPattern(const String& name) {
  if (name == "sos")       { startSteps(kSos, sizeof(kSos) / 2, "sos"); return true; }
  if (name == "heartbeat") { startSteps(kHeartbeat, sizeof(kHeartbeat) / 2, "heartbeat"); return true; }
  if (name == "strobe")    { startSteps(kStrobe, sizeof(kStrobe) / 2, "strobe"); return true; }
  return false;
}

bool Led::playSeq(const String& csv) {
  uint16_t steps[kMaxSteps];
  size_t n = 0;
  int from = 0;
  while (from < (int)csv.length() && n < kMaxSteps) {
    int comma = csv.indexOf(',', from);
    String tok = (comma < 0) ? csv.substring(from) : csv.substring(from, comma);
    tok.trim();
    long ms = tok.toInt();
    if (ms <= 0) return false;
    steps[n++] = (uint16_t)min(ms, 60000L);
    if (comma < 0) break;
    from = comma + 1;
  }
  if (n < 2) return false;
  startSteps(steps, n, "seq");
  return true;
}

void Led::pulse(uint32_t periodMs) {
  mode_ = Mode::PULSE;
  pulsePeriod_ = max(periodMs, (uint32_t)200);
  pulseStart_ = millis();
}

void Led::stopBlink() {
  if (mode_ == Mode::PULSE) write(state_);  // restore a clean duty level
  mode_ = Mode::STATIC;
}

void Led::loop() {
  uint32_t now = millis();
  switch (mode_) {
    case Mode::BLINK:
      if (blinkInterval_ && now - lastToggle_ >= blinkInterval_) {
        lastToggle_ = now;
        toggle();
      }
      break;
    case Mode::PATTERN:
      if (now - stepStart_ >= steps_[stepIdx_]) {
        stepStart_ = now;
        stepIdx_ = (stepIdx_ + 1) % stepCount_;
        write(stepIdx_ % 2 == 0);  // even steps ON, odd steps OFF
      }
      break;
    case Mode::PULSE: {
      // Triangle wave: 0 -> brightness -> 0 over pulsePeriod_.
      uint32_t t = (now - pulseStart_) % pulsePeriod_;
      float phase = (float)t / pulsePeriod_;
      float tri = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
      uint8_t duty = (uint8_t)(tri * brightness_);
      ledcWrite(channel_, activeHigh_ ? duty : 255 - duty);
      state_ = duty > 0;
      break;
    }
    case Mode::STATIC:
      break;
  }
}

bool Led::handleCommand(const String& action, const String& arg) {
  if (action == "brightness") {
    setBrightness((uint8_t)constrain(arg.toInt(), 0, 255));
    return true;
  }
  if (action == "blink") {
    blink(arg.length() ? (uint32_t)arg.toInt() : 500);
    return true;
  }
  if (action == "pattern") return playPattern(arg);
  if (action == "seq")     return playSeq(arg);
  if (action == "pulse") {
    pulse(arg.length() ? (uint32_t)arg.toInt() : 2000);
    return true;
  }
  if (action == "solid" || action == "stop") {
    stopBlink();
    return true;
  }
  // on/off/toggle from DigitalOutput; any of them cancels an animation.
  if (DigitalOutput::handleCommand(action, arg)) {
    mode_ = Mode::STATIC;
    return true;
  }
  return false;
}

String Led::status() const {
  switch (mode_) {
    case Mode::BLINK:   return "blink " + String(blinkInterval_) + "ms";
    case Mode::PATTERN: return "pattern " + patternName_;
    case Mode::PULSE:   return "pulse " + String(pulsePeriod_) + "ms";
    default:            return String(state_ ? "on" : "off") + " b=" + String(brightness_);
  }
}
