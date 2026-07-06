#include <Arduino.h>

#include "Board.h"
#include "SerialConsole.h"
#include "board_config.h"
#include "net/WifiLink.h"
#include "net/HttpApi.h"
#include "net/MqttLink.h"

static Board board;
static SerialConsole console(board.components());
static WifiLink wifi;
static HttpApi api(board.components());
static MqttLink mqtt(board.components());

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[iot-assistant] " BOARD_NAME " fw " FW_VERSION " booting");
  Serial.printf("chip: %s rev%d | flash %uMB | PSRAM %u bytes\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
                (unsigned)ESP.getPsramSize());

  board.begin();
  console.begin();

  wifi.begin("iot-assistant");
  api.begin(BOARD_NAME, FW_VERSION);
  mqtt.begin("iot-assistant");

  // Init blink so a fresh flash is visibly alive; the API/console can
  // override at any time (e.g. "led_1 off").
  if (auto* l = board.components().find("led_1")) l->handleCommand("blink", "250");
  if (auto* l = board.components().find("led_2")) l->handleCommand("blink", "500");
  if (auto* o = board.components().find("onboard")) {
    o->handleCommand("color", "0,40,0");
    o->handleCommand("blink", "1000");
  }

  Serial.println("[iot-assistant] ready -- type 'help'");
}

void loop() {
  board.loop();
  console.loop();
  wifi.loop();
  api.loop();
  mqtt.loop();
}
