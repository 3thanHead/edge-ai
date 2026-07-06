#include "net/MqttLink.h"

#include <WiFi.h>

#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

void MqttLink::begin(const char* deviceName) {
  base_ = String("iot/") + deviceName;
  enabled_ = strlen(MQTT_HOST) > 0;
  if (!enabled_) {
    Serial.println("[mqtt] no MQTT_HOST baked in -- mqtt disabled "
                   "(set it in the repo-root .env and rebuild)");
    return;
  }
  mqtt_.setServer(MQTT_HOST, MQTT_PORT);
  mqtt_.setBufferSize(1024);  // default 256 is too small for the state JSON
  mqtt_.setCallback([this](char* t, uint8_t* p, unsigned int n) {
    onMessage(t, p, n);
  });
  Serial.printf("[mqtt] broker %s:%d topics %s/#\n", MQTT_HOST, MQTT_PORT,
                base_.c_str());
}

void MqttLink::ensureConnected() {
  if (mqtt_.connected() || WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastAttempt_ < 5000) return;  // retry at most every 5 s
  lastAttempt_ = millis();

  String avail = base_ + "/availability";
  // LWT: broker marks us offline if we vanish without a clean disconnect.
  if (mqtt_.connect(base_.c_str(), nullptr, nullptr, avail.c_str(),
                    /*willQos=*/1, /*willRetain=*/true, "offline")) {
    mqtt_.publish(avail.c_str(), "online", /*retained=*/true);
    mqtt_.subscribe((base_ + "/cmd").c_str());
    Serial.println("[mqtt] connected");
    publishState();
  }
}

void MqttLink::loop() {
  if (!enabled_) return;
  ensureConnected();
  if (!mqtt_.connected()) return;
  mqtt_.loop();
  if (millis() - lastState_ >= 10000) publishState();
}

void MqttLink::onMessage(char* topic, uint8_t* payload, unsigned int len) {
  (void)topic;  // only /cmd is subscribed
  String line;
  line.reserve(len);
  for (unsigned int i = 0; i < len; ++i) line += (char)payload[i];
  dispatch(line);
}

void MqttLink::dispatch(const String& raw) {
  // Same "<name> <action> [arg]" shape as the serial console and HTTP API.
  String s = raw;
  s.trim();
  int sp1 = s.indexOf(' ');
  String name = (sp1 < 0) ? s : s.substring(0, sp1);
  String rest = (sp1 < 0) ? "" : s.substring(sp1 + 1);
  rest.trim();
  int sp2 = rest.indexOf(' ');
  String action = (sp2 < 0) ? rest : rest.substring(0, sp2);
  String arg = (sp2 < 0) ? "" : rest.substring(sp2 + 1);
  arg.trim();

  String resp;
  Component* c = registry_.find(name);
  if (c == nullptr) {
    resp = "{\"ok\":false,\"error\":\"unknown component '" + jsonEscape(name) +
           "'\"}";
  } else if (!c->handleCommand(action, arg)) {
    resp = "{\"ok\":false,\"error\":\"'" + jsonEscape(name) + "' can't '" +
           jsonEscape(action) + "'\"}";
  } else {
    Serial.printf("[mqtt] ok %s %s %s\n", name.c_str(), action.c_str(),
                  arg.c_str());
    resp = "{\"ok\":true,\"name\":\"" + jsonEscape(name) + "\",\"status\":\"" +
           jsonEscape(c->status()) + "\"}";
    publishState();
  }
  mqtt_.publish((base_ + "/resp").c_str(), resp.c_str());
}

void MqttLink::publishState() {
  lastState_ = millis();
  String json = "[";
  bool first = true;
  for (auto* c : registry_.all()) {
    if (!first) json += ",";
    first = false;
    json += "{\"name\":\"" + jsonEscape(c->name()) + "\",\"status\":\"" +
            jsonEscape(c->status()) + "\"}";
  }
  json += "]";
  mqtt_.publish((base_ + "/state").c_str(), json.c_str(), /*retained=*/true);
}

String MqttLink::jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}
