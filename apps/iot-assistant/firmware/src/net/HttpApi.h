#pragma once
#include <WebServer.h>

#include "core/ComponentRegistry.h"

// The device's agent-facing HTTP API -- the network twin of the serial
// console, speaking the same <name> <action> [arg] shape over JSON:
//
//   GET  /health                          liveness + identity
//   GET  /api/components                  every component + its status()
//   POST /api/command?name=&action=&arg=  dispatch to handleCommand()
//
// Any client on the LAN (the cluster's LLM agent via tool-calling, curl, a
// dashboard) can enumerate what this device has and actuate it by name.
class HttpApi {
 public:
  explicit HttpApi(ComponentRegistry& registry) : registry_(registry) {}

  void begin(const char* deviceName, const char* fwVersion);
  void loop();

 private:
  ComponentRegistry& registry_;
  WebServer server_{80};
  const char* deviceName_ = "";
  const char* fwVersion_ = "";

  void handleHealth();
  void handleList();
  void handleCommand();

  static String jsonEscape(const String& s);
};
