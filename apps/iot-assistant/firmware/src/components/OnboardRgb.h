#pragma once
#include "core/Component.h"

// The DevKit's onboard addressable RGB LED (WS2812-style, driven with the
// Arduino core's neopixelWrite -- no library needed). Commands:
//   on | off | toggle
//   color <r,g,b>        0-255 each
//   blink [ms]           blink the current colour
//   solid                stop blinking
class OnboardRgb : public Component {
 public:
  OnboardRgb(const char* name, int pin);

  void begin() override;
  void loop() override;
  bool handleCommand(const String& action, const String& arg) override;
  String status() const override;

  void setColor(uint8_t r, uint8_t g, uint8_t b);
  void set(bool on);

 private:
  int pin_;
  bool on_ = false;
  // Modest default so the LED isn't blinding at full duty.
  uint8_t r_ = 0, g_ = 40, b_ = 0;
  uint32_t blinkInterval_ = 0;
  uint32_t lastToggle_ = 0;

  void apply();
};
