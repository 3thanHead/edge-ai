#pragma once
#include "core/Component.h"

// LAFVIN audio codec module -- identified on-device (control-bus scan):
//   ES8311 @ 0x18  mono codec (speaker DAC; its ADC is unused)
//   ES7210 @ 0x41  4-ch ADC driving the module's two mics -> I2S DOUT
// The classic xiaozhi-style 2-in-1 module: both chips are I2S SLAVES sharing
// BCLK/WS/MCLK from the ESP32 master; register init ported from esp-adf's
// audio_hal drivers for our fixed config (16 kHz, 16-bit I2S, MCLK = 256*fs
// = 4.096 MHz from the MCLK pad).
//
// I2S data path + a bit-banged I2C control bus (both hardware I2C
// controllers are taken by the OLEDs; codec control traffic is tiny) + the
// PA_EN speaker-amp gate.
//
// Actions: beep                  short 880 Hz chirp (gates the amp itself)
//          tone <hz>[,ms]        sine burst (ms capped at 2000)
//          volume <0-100>        speaker loudness (ES8311 DAC volume)
//          micgain <0-14>        ES7210 PGA gain, 3 dB/step (default 10 = 30 dB)
//          amp <on|off>          PA_EN; tone/beep gate it automatically
//          scan                  re-probe the control bus, log the hits
//
// Debug actions (result lands in status(), so it comes back in the HTTP
// command ack -- lets the codec be interrogated remotely, no serial needed):
//          regr <hexaddr>,<hexreg>          read a codec register
//          regw <hexaddr>,<hexreg>,<hexval> write a codec register
//          rxpeek                           raw I2S RX: L/R peaks + samples
//          swapio                           swap I2S data in/out pins live
//                                           (tests DIN/DOUT label confusion)
class AudioCodec : public Component {
 public:
  explicit AudioCodec(const char* name);

  void begin() override;
  bool handleCommand(const String& action, const String& arg) override;
  String status() const override;

 private:
  void i2sInit();
  bool applyPins();  // (re)route I2S pins, honoring swapped_
  void playTone(float hz, uint32_t ms);
  String scanBus();

  // Chip bring-up (register sequences from esp-adf audio_hal drivers).
  bool es8311Init();
  bool es7210Init();
  void setDacVolume(uint8_t volume);   // 0-100 -> ES8311 REG32
  void setMicGain(uint8_t gain);       // 0-14  -> ES7210 REG43/44

  // Minimal open-drain bit-banged I2C for the codec control port.
  void i2cIdle();
  void i2cStart();
  void i2cStop();
  bool i2cWriteByte(uint8_t b);
  uint8_t i2cReadByte(bool ack);
  bool i2cProbe(uint8_t addr);
  bool regWrite(uint8_t addr, uint8_t reg, uint8_t val);
  int regRead(uint8_t addr, uint8_t reg);  // -1 on NACK

  bool i2sReady_ = false;
  bool swapped_ = false;  // I2S data pins swapped vs board_config
  bool amp_ = false;
  uint8_t volume_ = 60;
  uint8_t micGain_ = 14;   // ES7210 PGA max (37.5 dB) -- breadboard mics run quiet
  int es8311Addr_ = -1;
  int es7210Addr_ = -1;
  bool es8311Ok_ = false;
  bool es7210Ok_ = false;
  String found_;  // ACKing addresses from the last scan, e.g. "0x18 0x41"
  String debug_;  // last regr/regw/rxpeek result, surfaced via status()
};
