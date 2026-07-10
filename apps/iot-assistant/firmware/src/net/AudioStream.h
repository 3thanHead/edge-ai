#pragma once
#include <WebSocketsClient.h>

#include "core/Component.h"

// Streams the codec's mic audio to the agents hub over WiFi:
// I2S RX -> downmix to 16 kHz mono int16 PCM -> 64 ms binary WS frames to
// ws://<MQTT_HOST>:8810/ws/audio/ingest. The agents app fans the frames out
// to subscribers -- the chat UI's live feed among them.
//
// Lives in net/ but implements Component (name "mic") so every control
// surface can gate it. It reads I2S_NUM_0 directly -- the driver is
// installed by AudioCodec::begin(), so register this AFTER the audio
// component. Until the codec chip is identified and initialized the samples
// may be zeros or noise; the feed still proves the wiring + transport.
//
// Actions: on | off
class AudioStream : public Component {
 public:
  explicit AudioStream(const char* name) : Component(name) {}

  void begin() override;
  void loop() override;
  bool handleCommand(const String& action, const String& arg) override;
  String status() const override;

 private:
  static constexpr uint16_t kPort = 8810;          // the agents app
  static constexpr size_t kChunkSamples = 1024;    // 64 ms @ 16 kHz mono

  WebSocketsClient ws_;
  String host_;
  bool enabled_ = true;   // stream whenever the socket is up
  bool connected_ = false;
  int16_t mono_[kChunkSamples];
  size_t fill_ = 0;
  uint32_t framesSent_ = 0;
  int16_t peak_ = 0;  // |max| of the last sent chunk -- proves real signal
};
