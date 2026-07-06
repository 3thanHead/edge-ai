#include "net/HttpApi.h"

void HttpApi::begin(const char* deviceName, const char* fwVersion) {
  deviceName_ = deviceName;
  fwVersion_ = fwVersion;

  server_.on("/health", HTTP_GET, [this]() { handleHealth(); });
  server_.on("/api/components", HTTP_GET, [this]() { handleList(); });
  server_.on("/api/command", HTTP_POST, [this]() { handleCommand(); });
  server_.onNotFound([this]() {
    server_.send(404, "application/json", "{\"error\":\"not found\"}");
  });

  server_.begin();
  Serial.println("[http] api listening on :80");
}

void HttpApi::loop() { server_.handleClient(); }

String HttpApi::jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

void HttpApi::handleHealth() {
  String json = "{\"device\":\"" + jsonEscape(deviceName_) +
                "\",\"version\":\"" + jsonEscape(fwVersion_) +
                "\",\"ip\":\"" + WiFi.localIP().toString() +
                "\",\"uptime_s\":" + String(millis() / 1000) + "}";
  server_.send(200, "application/json", json);
}

void HttpApi::handleList() {
  String json = "[";
  bool first = true;
  for (auto* c : registry_.all()) {
    if (!first) json += ",";
    first = false;
    json += "{\"name\":\"" + jsonEscape(c->name()) +
            "\",\"status\":\"" + jsonEscape(c->status()) + "\"}";
  }
  json += "]";
  server_.send(200, "application/json", json);
}

void HttpApi::handleCommand() {
  String name = server_.arg("name");
  String action = server_.arg("action");
  String arg = server_.arg("arg");

  if (name.isEmpty() || action.isEmpty()) {
    server_.send(400, "application/json",
                 "{\"error\":\"required: name, action (optional: arg)\"}");
    return;
  }

  Component* c = registry_.find(name);
  if (c == nullptr) {
    server_.send(404, "application/json",
                 "{\"error\":\"unknown component '" + jsonEscape(name) + "'\"}");
    return;
  }
  if (!c->handleCommand(action, arg)) {
    server_.send(422, "application/json",
                 "{\"error\":\"'" + jsonEscape(name) + "' can't '" +
                     jsonEscape(action) + "'\"}");
    return;
  }

  Serial.printf("[http] ok %s %s %s\n", name.c_str(), action.c_str(), arg.c_str());
  server_.send(200, "application/json",
               "{\"ok\":true,\"name\":\"" + jsonEscape(name) +
                   "\",\"status\":\"" + jsonEscape(c->status()) + "\"}");
}
