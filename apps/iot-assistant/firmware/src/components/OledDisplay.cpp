#include "components/OledDisplay.h"

static constexpr uint8_t kI2cAddr = 0x3C;

OledDisplay::OledDisplay(const char* name, TwoWire& wire, int sdaPin, int sclPin)
    : Component(name),
      wire_(wire),
      sdaPin_(sdaPin),
      sclPin_(sclPin),
      display_(128, 64, &wire, /*rstPin=*/-1) {}

void OledDisplay::begin() {
  wire_.begin(sdaPin_, sclPin_, 400000);
  present_ = display_.begin(SSD1306_SWITCHCAPVCC, kI2cAddr);
  if (!present_) {
    Serial.printf("[%s] no SSD1306 at 0x%02X (SDA=%d SCL=%d)\n",
                  name_, kI2cAddr, sdaPin_, sclPin_);
    return;
  }
  showText(String(name_) + " ready");  // tell the two panels apart on boot
}

void OledDisplay::showText(const String& msg) {
  content_ = msg;
  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);
  display_.setTextSize(1);  // 21 cols x 8 rows
  display_.setTextWrap(true);
  display_.setCursor(0, 0);
  String line = msg;
  line.replace("|", "\n");
  display_.print(line);
  display_.display();
}

bool OledDisplay::handleCommand(const String& action, const String& arg) {
  if (!present_) return false;

  if (action == "text") {
    showText(arg);
    return true;
  }
  if (action == "clear") {
    content_ = "";
    display_.clearDisplay();
    display_.display();
    return true;
  }
  if (action == "fill") {
    if (arg != "white" && arg != "black") return false;
    content_ = "";
    display_.fillScreen(arg == "white" ? SSD1306_WHITE : SSD1306_BLACK);
    display_.display();
    return true;
  }
  if (action == "invert") {
    display_.invertDisplay(arg == "on" || arg == "true" || arg == "1");
    return true;
  }
  if (action == "contrast") {
    display_.ssd1306_command(SSD1306_SETCONTRAST);
    display_.ssd1306_command((uint8_t)constrain(arg.toInt(), 0, 255));
    return true;
  }
  return false;
}

String OledDisplay::status() const {
  if (!present_) return "not detected";
  if (!content_.length()) return "blank";
  String preview = content_.substring(0, 24);
  return "text='" + preview + (content_.length() > 24 ? "...'" : "'");
}
