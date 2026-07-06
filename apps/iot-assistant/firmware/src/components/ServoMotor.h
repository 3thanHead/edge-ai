#pragma once
#include "core/Component.h"
#include <ESP32Servo.h>

// A hobby servo (SG90-class). Named ServoMotor to avoid colliding with the
// ESP32Servo library's own `Servo` type.
//
// Two coordinate systems:
//   angle <deg>      physical servo angle, 0..rangeDeg (native)
//   heading <0-360>  compass-style bearing, scaled into the servo's range
//   compass <pt>     N|NNE|NE|...|NW -> heading (N=0, E=90, S=180, W=270)
// On a 180-degree SG90 headings compress 2:1 (E=45deg physical); drop in a
// 360-degree positional servo and set SERVO_RANGE_DEG accordingly.
//
// Note: ESP32Servo allocates an LEDC timer/channel internally, so budget it
// against the Led/Fan/RgbLed channels (the S3 has 8 LEDC channels total).
class ServoMotor : public Component {
 public:
  ServoMotor(const char* name, int pin, int rangeDeg = 180);

  void begin() override;
  bool handleCommand(const String& action, const String& arg) override;
  String status() const override;

  void setAngle(int deg);        // physical, clamped to 0..rangeDeg
  void setHeading(int bearing);  // 0-360, scaled into 0..rangeDeg

 private:
  Servo servo_;
  int pin_;
  int rangeDeg_;
  int angle_ = 90;
  int heading_ = -1;  // last commanded bearing, -1 = angle was set directly
};
