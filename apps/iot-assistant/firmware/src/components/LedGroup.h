#pragma once
#include "core/Component.h"
#include "components/Led.h"

// Coordinates the two breadboard LEDs as one unit so multi-LED patterns and
// the yes/no answer convention are a single atomic command (one tool call for
// the agent, no race between two requests).
//
// Convention: red (led_1, GPIO 4) = no/false, blue (led_2, GPIO 5) = yes/true.
//
// Actions: alternate [ms]        anti-phase blink
//          together [ms]         in-phase blink
//          pattern <name>        both play a Led pattern (sos|heartbeat|strobe)
//          answer <yes|no>       heartbeat the answer's LED, other goes dark
//          on | off | solid
class LedGroup : public Component {
 public:
  LedGroup(const char* name, Led& red, Led& blue)
      : Component(name), red_(red), blue_(blue) {}

  void begin() override {}  // members are registered components; Board begins them
  bool handleCommand(const String& action, const String& arg) override;
  String status() const override;

 private:
  Led& red_;
  Led& blue_;
  String mode_ = "idle";
};
