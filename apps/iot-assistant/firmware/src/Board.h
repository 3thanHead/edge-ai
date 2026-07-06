#pragma once
#include "core/ComponentRegistry.h"
#include "components/Led.h"
#include "components/LedGroup.h"
#include "components/OnboardRgb.h"
#include "components/ServoMotor.h"
#include "components/Face.h"

// The physical device: instantiates every component wired to this board and
// exposes them through a ComponentRegistry. This is the ONLY class that knows
// the board's concrete layout (it reads pins from board_config.h). Add a
// component by declaring it here and registering it in the constructor.
//
// Demo set: two breadboard LEDs (+ the `leds` group coordinating them), the
// onboard RGB, an SG90 servo, and the ST7789 face. Audio (mic + speaker) is
// Phase 3.
class Board {
 public:
  Board();

  void begin();
  void loop();

  ComponentRegistry& components() { return registry_; }

 private:
  ComponentRegistry registry_;

  Led led1_;       // red  = no/false
  Led led2_;       // blue = yes/true
  LedGroup leds_;
  OnboardRgb onboard_;
  ServoMotor servo_;
  Face face_;
};
