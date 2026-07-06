#include "components/ServoMotor.h"

// 16-point compass rose, 22.5 degrees apart, N=0.
static const struct { const char* name; int bearing; } kCompass[] = {
    {"n", 0},    {"nne", 23},  {"ne", 45},   {"ene", 68},
    {"e", 90},   {"ese", 113}, {"se", 135},  {"sse", 158},
    {"s", 180},  {"ssw", 203}, {"sw", 225},  {"wsw", 248},
    {"w", 270},  {"wnw", 293}, {"nw", 315},  {"nnw", 338},
    {"north", 0}, {"east", 90}, {"south", 180}, {"west", 270},
};

ServoMotor::ServoMotor(const char* name, int pin, int rangeDeg)
    : Component(name), pin_(pin), rangeDeg_(max(1, rangeDeg)) {}

void ServoMotor::begin() {
  servo_.setPeriodHertz(50);       // standard 50 Hz servo frame
  servo_.attach(pin_, 500, 2400);  // typical SG90 pulse range (us)
  setAngle(angle_);
}

void ServoMotor::setAngle(int deg) {
  angle_ = constrain(deg, 0, rangeDeg_);
  heading_ = -1;
  servo_.write(map(angle_, 0, rangeDeg_, 0, 180));  // lib expects 0-180
}

void ServoMotor::setHeading(int bearing) {
  bearing = ((bearing % 360) + 360) % 360;
  setAngle((int)((long)bearing * rangeDeg_ / 360));
  heading_ = bearing;
}

bool ServoMotor::handleCommand(const String& action, const String& arg) {
  if (action == "angle")   { setAngle(arg.toInt()); return true; }
  if (action == "heading") { setHeading(arg.toInt()); return true; }
  if (action == "compass") {
    String pt = arg;
    pt.toLowerCase();
    pt.trim();
    for (auto& c : kCompass) {
      if (pt == c.name) { setHeading(c.bearing); return true; }
    }
    return false;
  }
  return false;
}

String ServoMotor::status() const {
  String s = String(angle_) + "deg";
  if (heading_ >= 0) s += " (heading " + String(heading_) + ")";
  return s;
}
