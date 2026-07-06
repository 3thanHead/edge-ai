#pragma once
#include <Arduino.h>

// Abstract base for every hardware component (LED, relay, button, sensor...).
// The Board owns a collection of these; the main loop calls begin() once and
// loop() every tick, so main.cpp never touches device specifics.
//
// handleCommand() is the generic actuation hook: any control surface -- the
// serial console today, the AI assistant later -- can drive a component by
// name + action string without knowing its concrete type.
class Component {
 public:
  explicit Component(const char* name) : name_(name) {}
  virtual ~Component() = default;

  // Configure hardware. Called once at startup.
  virtual void begin() = 0;

  // Time-based behaviour (blinking, debouncing, polling). Called every tick.
  virtual void loop() {}

  // Actuate by string command. Returns true if the action was handled.
  virtual bool handleCommand(const String& action, const String& arg) {
    return false;
  }

  // Human/machine-readable current state or reading (e.g. "on", "23.4C 41%").
  // Empty by default. This is the read side of the same generic interface the
  // LLM agent uses -- how it inspects a sensor or an actuator's state.
  virtual String status() const { return String(); }

  const char* name() const { return name_; }

 protected:
  const char* name_;
};
