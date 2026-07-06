#include "net/WifiLink.h"

#include <WiFi.h>
#include <ESPmDNS.h>

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

void WifiLink::begin(const char* hostname) {
  configured_ = strlen(WIFI_SSID) > 0;
  if (!configured_) {
    Serial.println("[wifi] no WIFI_SSID baked in -- offline mode "
                   "(set it in the repo-root .env and rebuild)");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] joining '%s'", WIFI_SSID);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, ip=%s\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin(hostname)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[wifi] mdns: http://%s.local/\n", hostname);
    }
  } else {
    Serial.println("[wifi] connect timed out -- will keep retrying in loop()");
  }
  lastAttempt_ = millis();
}

void WifiLink::loop() {
  if (!configured_) return;
  if (WiFi.status() == WL_CONNECTED) return;
  // Light-touch watchdog: kick a reconnect at most every 10 s.
  if (millis() - lastAttempt_ >= 10000) {
    lastAttempt_ = millis();
    Serial.println("[wifi] reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

bool WifiLink::connected() const { return WiFi.status() == WL_CONNECTED; }

String WifiLink::ip() const {
  return connected() ? WiFi.localIP().toString() : String("");
}
