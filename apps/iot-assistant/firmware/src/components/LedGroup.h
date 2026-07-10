#pragma once
#include "core/Component.h"
#include "components/Led.h"

// Coordinates the three breadboard LEDs (green/yellow/red) as one unit so
// multi-LED patterns and the answer convention are a single atomic command
// (one tool call for the agent, no race between requests).
//
// Convention: green = yes/true, red = no/false, yellow = maybe/unknown.
//
// Actions: alternate [ms]        green+red vs yellow, anti-phase blink
//          together [ms]         all three in-phase blink
//          pattern <name>        all play a Led pattern (sos|heartbeat|strobe)
//          answer <yes|no|maybe> heartbeat the answer's LED, others go dark
//          on | off | solid
class LedGroup : public Component {
 public:
  LedGroup(const char* name, Led& green, Led& yellow, Led& red)
      : Component(name), green_(green), yellow_(yellow), red_(red) {}

  void begin() override {}  // members are registered components; Board begins them
  bool handleCommand(const String& action, const String& arg) override;
  String status() const override;

 private:
  void each(const String& action, const String& arg);

  Led& green_;
  Led& yellow_;
  Led& red_;
  String mode_ = "idle";
};
