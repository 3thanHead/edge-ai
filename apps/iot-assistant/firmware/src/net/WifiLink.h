#pragma once
#include <Arduino.h>

// Joins the home WiFi (credentials injected at build time from the repo-root
// .env, same pattern as camera-vision) and keeps the link up. If no SSID was
// baked in, stays offline gracefully -- the serial console still works.
class WifiLink {
 public:
  void begin(const char* hostname);
  void loop();  // reconnect watchdog

  bool connected() const;
  String ip() const;

 private:
  bool configured_ = false;
  uint32_t lastAttempt_ = 0;
};
