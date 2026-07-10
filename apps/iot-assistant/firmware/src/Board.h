#pragma once
#include "core/ComponentRegistry.h"
#include "components/Led.h"
#include "components/LedGroup.h"
#include "components/St7789Lcd.h"
#include "components/OledDisplay.h"
#include "components/AudioCodec.h"

// The physical device: instantiates every component wired to this board and
// exposes them through a ComponentRegistry. This is the ONLY class that knows
// the board's concrete layout (it reads pins from board_config.h). Add a
// component by declaring it here and registering it in the constructor.
//
// Wired set: three status LEDs (+ the `leds` group coordinating them), the
// 2.0" ST7789 SPI face, two 0.96" I2C displays, and the audio codec module
// (I2S + control bus + amp gate).
class Board {
 public:
  Board();

  void begin();
  void loop();

  ComponentRegistry& components() { return registry_; }

 private:
  ComponentRegistry registry_;

  Led ledGreen_;    // = yes/true
  Led ledYellow_;   // = maybe/unknown
  Led ledRed_;      // = no/false
  LedGroup leds_;
  St7789Lcd lcd_;
  OledDisplay oled1_;
  OledDisplay oled2_;
  AudioCodec audio_;
};
