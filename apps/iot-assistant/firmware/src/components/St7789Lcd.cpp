#include "components/St7789Lcd.h"

#include "board_config.h"

St7789Lcd::St7789Lcd(const char* name)
    : Component(name),
      spi_(FSPI),
      tft_(&spi_, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST) {}

void St7789Lcd::begin() {
  spi_.begin(PIN_LCD_SCLK, /*miso=*/-1, PIN_LCD_MOSI, PIN_LCD_CS);
  tft_.init(240, 320);
  // 20 MHz: breadboard jumpers won't carry the lib's 40 MHz default cleanly.
  tft_.setSPISpeed(20000000);
  tft_.setRotation(1);  // landscape, 320x240
  tft_.fillScreen(ST77XX_BLACK);

  ledcSetup(LEDC_CH_LCD_BL, 5000, 8);
  ledcAttachPin(PIN_LCD_BL, LEDC_CH_LCD_BL);
  setBacklight(255);
}

void St7789Lcd::setBacklight(uint8_t duty) {
  backlight_ = duty;
  ledcWrite(LEDC_CH_LCD_BL, duty);
}

void St7789Lcd::showText(const String& msg) {
  content_ = msg;
  tft_.fillScreen(ST77XX_BLACK);
  tft_.setTextColor(ST77XX_WHITE);
  tft_.setTextSize(3);  // ~17 columns across the 320px landscape face
  tft_.setTextWrap(true);
  tft_.setCursor(0, 8);
  String line = msg;
  line.replace("|", "\n");
  tft_.print(line);
}

bool St7789Lcd::fillColor(const String& name) {
  struct { const char* name; uint16_t color; } table[] = {
      {"black", ST77XX_BLACK},   {"white", ST77XX_WHITE},
      {"red", ST77XX_RED},       {"green", ST77XX_GREEN},
      {"blue", ST77XX_BLUE},     {"yellow", ST77XX_YELLOW},
      {"orange", ST77XX_ORANGE}, {"cyan", ST77XX_CYAN},
      {"magenta", ST77XX_MAGENTA},
  };
  for (auto& e : table) {
    if (name == e.name) {
      tft_.fillScreen(e.color);
      return true;
    }
  }
  return false;
}

bool St7789Lcd::handleCommand(const String& action, const String& arg) {
  if (action == "text") {
    showText(arg);
    return true;
  }
  if (action == "clear") {
    content_ = "";
    if (arg.length()) return fillColor(arg);
    tft_.fillScreen(ST77XX_BLACK);
    return true;
  }
  if (action == "backlight") {
    if (arg == "on") setBacklight(255);
    else if (arg == "off") setBacklight(0);
    else setBacklight((uint8_t)constrain(arg.toInt(), 0, 255));
    return true;
  }
  return false;
}

String St7789Lcd::status() const {
  String s = "bl=" + String(backlight_);
  if (content_.length()) {
    String preview = content_.substring(0, 24);
    s += " text='" + preview + (content_.length() > 24 ? "..." : "") + "'";
  }
  return s;
}
