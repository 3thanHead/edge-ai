#pragma once
#include <WiFiClient.h>
#include <PubSubClient.h>

#include "core/ComponentRegistry.h"

// MQTT twin of the HTTP API -- the push side of the device's control surface.
// Connects to the broker on the cluster master (MQTT_HOST baked in from the
// repo-root .env; empty = MQTT disabled) and speaks:
//
//   iot/<device>/cmd           SUB   "<name> <action> [arg]" -- same shape as
//                                    the serial console / HTTP API
//   iot/<device>/resp          PUB   {"ok":...} result of each cmd
//   iot/<device>/state         PUB   retained JSON of every component's
//                                    status; on connect, after each command,
//                                    and every 10 s
//   iot/<device>/availability  PUB   retained "online"; broker flips it to
//                                    "offline" via LWT when the device drops
class MqttLink {
 public:
  explicit MqttLink(ComponentRegistry& registry) : registry_(registry) {}

  void begin(const char* deviceName);
  void loop();
  bool connected() { return mqtt_.connected(); }

  // Publish the full component state (retained). Cheap enough to call after
  // any command from any surface.
  void publishState();

 private:
  ComponentRegistry& registry_;
  WiFiClient net_;
  PubSubClient mqtt_{net_};

  String base_;  // "iot/<device>"
  bool enabled_ = false;
  uint32_t lastAttempt_ = 0;
  uint32_t lastState_ = 0;

  void ensureConnected();
  void onMessage(char* topic, uint8_t* payload, unsigned int len);
  void dispatch(const String& line);

  static String jsonEscape(const String& s);
};
