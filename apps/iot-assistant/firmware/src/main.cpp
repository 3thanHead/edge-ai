#include <Arduino.h>

#include "Board.h"
#include "SerialConsole.h"
#include "board_config.h"
#include "net/WifiLink.h"
#include "net/HttpApi.h"
#include "net/MqttLink.h"
#include "net/AudioStream.h"

static Board board;
static SerialConsole console(board.components());
static WifiLink wifi;
static HttpApi api(board.components());
static MqttLink mqtt(board.components());
static AudioStream mic("mic");  // I2S RX -> agents hub /ws/audio/ingest

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[iot-assistant] " BOARD_NAME " fw " FW_VERSION " booting");
  Serial.printf("chip: %s rev%d | flash %uMB | PSRAM %u bytes\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
                (unsigned)ESP.getPsramSize());

  // The mic streamer rides the component registry (so every control surface
  // can gate it) but must begin AFTER the audio component installs the I2S
  // driver -- audio registers first inside Board, so appending here is safe.
  board.components().add(&mic);

  board.begin();
  console.begin();

  wifi.begin("iot-assistant");
  api.begin(BOARD_NAME, FW_VERSION);
  mqtt.begin("iot-assistant");

  // -- Boot smoke test (temporary): prove out the new wiring on sight. -------
  // All three displays go white and all three LEDs blink together. Delete
  // this block to restore the boot-fresh convention (everything off/blank).
  for (const char* screen : {"lcd", "oled_1", "oled_2"}) {
    Component* c = board.components().find(screen);
    if (c) c->handleCommand(String(screen) == "lcd" ? "clear" : "fill", "white");
  }
  if (Component* leds = board.components().find("leds")) {
    leds->handleCommand("together", "500");
  }

  Serial.println("[iot-assistant] ready -- type 'help'");
}

void loop() {
  board.loop();
  console.loop();
  wifi.loop();
  api.loop();
  mqtt.loop();

  // IP splash disabled while the boot smoke test runs -- it would overwrite
  // the white LCD once WiFi lands. Re-enable with the boot-fresh convention.
  // static bool splashed = false;
  // if (!splashed && wifi.connected()) {
  //   splashed = true;
  //   Component* lcd = board.components().find("lcd");
  //   if (lcd) {
  //     lcd->handleCommand("text",
  //                        "iot-assistant|fw " FW_VERSION "|" + wifi.ip());
  //   }
  // }
}
