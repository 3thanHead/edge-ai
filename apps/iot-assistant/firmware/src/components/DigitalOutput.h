#pragma once
#include "core/Component.h"

// A single digital output pin exposed as on/off/toggle -- the building block
// for LEDs, relays, buzzers: anything that is fundamentally "drive a pin".
// activeHigh=false handles hardware that sinks current (LED tied to VCC, most
// relay boards), so callers still speak plain on()/off().
//
// write() is virtual so subclasses (e.g. Led) can drive the pin differently
// -- via PWM instead of digitalWrite -- while inheriting on/off/toggle.
class DigitalOutput : public Component {
 public:
  DigitalOutput(const char* name, int pin, bool activeHigh = true);

  void begin() override;
  bool handleCommand(const String& action, const String& arg) override;
  String status() const override;

  void on();
  void off();
  void toggle();
  void set(bool value);

  bool isOn() const { return state_; }
  int pin() const { return pin_; }

 protected:
  int pin_;
  bool activeHigh_;
  bool state_ = false;

  virtual void write(bool value);  // applies activeHigh polarity
};
