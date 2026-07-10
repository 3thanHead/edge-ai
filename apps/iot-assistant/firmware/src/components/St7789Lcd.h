#pragma once
#include <Adafruit_ST7789.h>
#include <SPI.h>

#include "core/Component.h"

// The kit's 2.0" 240x320 ST7789 over 4-wire SPI, mounted landscape (320x240).
// Text-first for now: the device's "face" renders status lines; drawing
// primitives can hang off this component later.
//
// Actions: text <msg>            '|' in the message = newline
//          clear [color]         black|white|red|green|blue|yellow|orange
//          backlight <on|off|0-255>
class St7789Lcd : public Component {
 public:
  explicit St7789Lcd(const char* name);

  void begin() override;
  bool handleCommand(const String& action, const String& arg) override;
  String status() const override;

  void showText(const String& msg);
  void setBacklight(uint8_t duty);

 private:
  bool fillColor(const String& name);

  SPIClass spi_;
  Adafruit_ST7789 tft_;
  uint8_t backlight_ = 255;
  String content_;
};
