#include "net/AudioStream.h"

#include <driver/i2s.h>

#include "board_config.h"

void AudioStream::begin() {
#ifdef MQTT_HOST
  host_ = MQTT_HOST;  // the agents stack lives on the same box as mosquitto
#endif
  if (!host_.length()) {
    Serial.printf("[%s] no MQTT_HOST baked in; mic streaming disabled\n", name_);
    return;
  }

  ws_.begin(host_.c_str(), kPort, "/ws/audio/ingest");
  ws_.onEvent([this](WStype_t type, uint8_t*, size_t) {
    if (type == WStype_CONNECTED) {
      connected_ = true;
      Serial.printf("[%s] ingest connected -> ws://%s:%u\n", name_,
                    host_.c_str(), kPort);
    } else if (type == WStype_DISCONNECTED) {
      connected_ = false;
    }
  });
  ws_.setReconnectInterval(3000);
}

void AudioStream::loop() {
  if (!host_.length()) return;
  ws_.loop();
  if (!enabled_ || !connected_) return;

  // Drain whatever the RX DMA has, non-blocking. Stereo pairs come in
  // interleaved L/R (the module's two mics); average them down to mono.
  int16_t raw[256];  // 128 stereo frames per read
  for (int burst = 0; burst < 4; ++burst) {
    size_t got = 0;
    if (i2s_read(I2S_NUM_0, raw, sizeof(raw), &got, 0) != ESP_OK || got == 0)
      break;
    size_t frames = got / (2 * sizeof(int16_t));
    for (size_t f = 0; f < frames; ++f) {
      mono_[fill_++] =
          (int16_t)(((int32_t)raw[2 * f] + (int32_t)raw[2 * f + 1]) / 2);
      if (fill_ == kChunkSamples) {
        ws_.sendBIN((uint8_t*)mono_, sizeof(mono_));
        ++framesSent_;
        fill_ = 0;
        int16_t peak = 0;
        for (size_t i = 0; i < kChunkSamples; ++i) {
          int16_t v = mono_[i] < 0 ? -mono_[i] : mono_[i];
          if (v > peak) peak = v;
        }
        peak_ = peak;
      }
    }
  }
}

bool AudioStream::handleCommand(const String& action, const String& arg) {
  if (action == "on") {
    enabled_ = true;
    return true;
  }
  if (action == "off") {
    enabled_ = false;
    fill_ = 0;
    return true;
  }
  return false;
}

String AudioStream::status() const {
  if (!host_.length()) return "disabled (no host)";
  String s = enabled_ ? "on" : "off";
  s += connected_ ? " link=up" : " link=down";
  s += " sent=" + String(framesSent_) + " peak=" + String(peak_);
  return s;
}
