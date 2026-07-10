#pragma once
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "core/Component.h"

// One Hosymond 0.96" I2C display (SSD1306-class, 128x64 @ 0x3C). The two
// units each get their own SDA/SCL pair, so one rides Wire and the other
// Wire1 -- pass the bus in. If the panel isn't detected at begin() the
// component stays registered but rejects commands (status says why).
//
// Actions: text <msg>       '|' in the message = newline
//          clear
//          fill <white|black>
//          invert <on|off>
//          contrast <0-255>
class OledDisplay : public Component {
 public:
  OledDisplay(const char* name, TwoWire& wire, int sdaPin, int sclPin);

  void begin() override;
  bool handleCommand(const String& action, const String& arg) override;
  String status() const override;

  void showText(const String& msg);

 private:
  TwoWire& wire_;
  int sdaPin_;
  int sclPin_;
  Adafruit_SSD1306 display_;
  bool present_ = false;
  String content_;
};
