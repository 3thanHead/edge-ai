#include "Board.h"
#include "board_config.h"

Board::Board()
    : ledGreen_("led_green", PIN_LED_GREEN, LEDC_CH_LED_GREEN),
      ledYellow_("led_yellow", PIN_LED_YELLOW, LEDC_CH_LED_YELLOW),
      ledRed_("led_red", PIN_LED_RED, LEDC_CH_LED_RED),
      leds_("leds", ledGreen_, ledYellow_, ledRed_),
      lcd_("lcd"),
      oled1_("oled_1", Wire, PIN_OLED1_SDA, PIN_OLED1_SCL),
      oled2_("oled_2", Wire1, PIN_OLED2_SDA, PIN_OLED2_SCL),
      audio_("audio") {
  registry_.add(&ledGreen_);
  registry_.add(&ledYellow_);
  registry_.add(&ledRed_);
  registry_.add(&leds_);
  registry_.add(&lcd_);
  registry_.add(&oled1_);
  registry_.add(&oled2_);
  registry_.add(&audio_);
}

void Board::begin() { registry_.beginAll(); }
void Board::loop() { registry_.loopAll(); }
