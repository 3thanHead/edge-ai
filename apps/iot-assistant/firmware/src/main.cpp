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

  // Boot fresh: every component comes up in its OFF state (LEDs dark, onboard
  // RGB off, servo centered). The API and serial console drive it from there.

  Serial.println("[iot-assistant] ready -- type 'help'");
}

void loop() {
  board.loop();
  console.loop();
  wifi.loop();
  api.loop();
  mqtt.loop();
}
