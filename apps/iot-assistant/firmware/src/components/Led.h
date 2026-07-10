#pragma once
#include "components/DigitalOutput.h"

// An LED: a digital output with non-blocking blink, named patterns, custom
// on/off sequences and a PWM "pulse" (breathe). Brightness rides the ESP32
// LEDC peripheral, so each LED needs its own LEDC channel (0-15). If you
// never set a brightness it behaves as a plain on/off output at full duty.
//
// Actions: on | off | toggle | brightness <0-255> | blink [ms] | solid
//          pattern <sos|heartbeat|strobe> | seq <on,off,on,off,...ms>
//          pulse [periodMs]
class Led : public DigitalOutput {
 public:
  Led(const char* name, int pin, int ledcChannel, bool activeHigh = true);

  void begin() override;
  void loop() override;
  bool handleCommand(const String& action, const String& arg) override;
  String status() const override;

  void setBrightness(uint8_t brightness);  // 0-255, applied when lit
  uint8_t brightness() const { return brightness_; }
  void blink(uint32_t intervalMs);          // toggle every intervalMs
  bool playPattern(const String& name);     // sos | heartbeat | strobe
  bool playSeq(const String& csv);          // "150,150,450,600" ms, on first
  void pulse(uint32_t periodMs);            // triangle-wave brightness
  void stopBlink();                         // back to static on/off

 protected:
  void write(bool value) override;  // PWM duty instead of digitalWrite

 private:
  enum class Mode { STATIC, BLINK, PATTERN, PULSE };

  void startSteps(const uint16_t* steps, size_t count, const char* label);

  int channel_;
  uint8_t brightness_ = 255;  // duty used when "on"
  Mode mode_ = Mode::STATIC;

  uint32_t blinkInterval_ = 0;
  uint32_t lastToggle_ = 0;

  // Pattern playback: steps_[] holds ms durations, on/off alternating,
  // starting ON. Loops forever until another action replaces it.
  static constexpr size_t kMaxSteps = 24;
  uint16_t steps_[kMaxSteps];
  size_t stepCount_ = 0;
  size_t stepIdx_ = 0;
  uint32_t stepStart_ = 0;
  String patternName_;

  uint32_t pulsePeriod_ = 0;
  uint32_t pulseStart_ = 0;

  static constexpr int kPwmFreqHz = 5000;
  static constexpr int kPwmResBits = 8;  // 8-bit duty -> 0-255
};
