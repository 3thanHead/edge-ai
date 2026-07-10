#include "components/DigitalOutput.h"

DigitalOutput::DigitalOutput(const char* name, int pin, bool activeHigh)
    : Component(name), pin_(pin), activeHigh_(activeHigh) {}

void DigitalOutput::begin() {
  pinMode(pin_, OUTPUT);
  write(false);
}

void DigitalOutput::write(bool value) {
  digitalWrite(pin_, (value == activeHigh_) ? HIGH : LOW);
  state_ = value;
}

void DigitalOutput::set(bool value) { write(value); }
void DigitalOutput::on() { write(true); }
void DigitalOutput::off() { write(false); }
void DigitalOutput::toggle() { write(!state_); }

bool DigitalOutput::handleCommand(const String& action, const String& arg) {
  if (action == "on") { on(); return true; }
  if (action == "off") { off(); return true; }
  if (action == "toggle") { toggle(); return true; }
  return false;
}

String DigitalOutput::status() const { return state_ ? "on" : "off"; }
