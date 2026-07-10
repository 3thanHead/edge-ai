#pragma once
#include "core/ComponentRegistry.h"

// Minimal line-based control surface over the serial port. Parses
//   <component> <action> [arg]
// and dispatches to that component's handleCommand(), plus the meta commands
// `list` and `help`.
//
// This is a deliberate stand-in for the AI assistant's tool interface: same
// "name an actuator, name an action" shape, so when the voice phase lands the
// transport changes but the component layer underneath does not.
class SerialConsole {
 public:
  explicit SerialConsole(ComponentRegistry& registry) : registry_(registry) {}

  void begin();
  void loop();

 private:
  ComponentRegistry& registry_;
  String buffer_;

  void dispatch(const String& line);
  void listComponents();
  void printHelp();
};
