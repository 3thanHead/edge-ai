#include "Board.h"
#include "board_config.h"

Board::Board()
    : led1_("led_1", PIN_LED_1, /*ledcChannel=*/0),
      led2_("led_2", PIN_LED_2, /*ledcChannel=*/1),
      leds_("leds", led1_, led2_),
      onboard_("onboard", PIN_ONBOARD_RGB),
      servo_("servo", PIN_SERVO, SERVO_RANGE_DEG) {
  registry_.add(&led1_);
  registry_.add(&led2_);
  registry_.add(&leds_);
  registry_.add(&onboard_);
  registry_.add(&servo_);
}

void Board::begin() { registry_.beginAll(); }
void Board::loop() { registry_.loopAll(); }
