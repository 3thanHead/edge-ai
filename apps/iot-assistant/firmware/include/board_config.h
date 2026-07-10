#pragma once
// Board pin map -- ESP32-S3-DevKitC-1 (WROOM-1) on a passive GPIO extension
// board. Everything is user-wired on a breadboard; keep ALL pin choices here.
//
// S3 pin rules honored by this map:
//   - 46 is input-only: it can't drive a clock, so MCLK sits on 42.
//   - 3 is a strapping pin, used here for the codec's I2C SCL -- safe in
//     practice (I2C is open-drain with pull-ups) but confirm on first boot.
//   - 19/20 (USB), 26-32 (flash), 33-37 (octal PSRAM), 43/44 (UART console)
//     are untouched.
//   - 48 drives the red LED; on most DevKitC-1 boards the onboard WS2812 RGB
//     hangs off 48 too. A plain digital level isn't a valid WS2812 frame so
//     the RGB should stay dark, but if it glitches, that's why.

#define BOARD_NAME "esp32s3-devkit"

#ifndef FW_VERSION
#define FW_VERSION "0.4.0"
#endif

// -- Status LEDs (each through a 200R resistor to the GND rail) ---------------
static constexpr int PIN_LED_GREEN  = 21;
static constexpr int PIN_LED_YELLOW = 47;
static constexpr int PIN_LED_RED    = 48;  // shared with onboard RGB, see above

// -- LAFVIN 2.0" LCD -- ST7789 240x320, 4-wire SPI (VCC on 3V3, NOT 5V) -------
static constexpr int PIN_LCD_SCLK = 4;   // module pin "CLK"
static constexpr int PIN_LCD_CS   = 5;
static constexpr int PIN_LCD_DC   = 6;
static constexpr int PIN_LCD_MOSI = 7;   // module pin "SDA"
static constexpr int PIN_LCD_RST  = 15;  // module pin "RES"
static constexpr int PIN_LCD_BL   = 16;  // module pin "BLK", PWM-dimmable

// -- Hosymond 0.96" I2C displays (SSD1306-class), one per hardware I2C bus ----
static constexpr int PIN_OLED1_SDA = 17;  // -> Wire
static constexpr int PIN_OLED1_SCL = 18;
static constexpr int PIN_OLED2_SDA = 1;   // -> Wire1
static constexpr int PIN_OLED2_SCL = 2;

// -- LAFVIN audio codec module: I2S data + I2C control + amp enable -----------
// Both hardware I2C controllers are taken by the OLEDs, so the codec's
// low-traffic control bus is bit-banged (see components/AudioCodec).
//
// GOTCHA (found by live rxpeek debugging, 2026-07-09): the module's DIN/DOUT
// silkscreen is HOST-perspective, not codec-perspective. The wire on the pin
// labeled "DOUT" (GPIO12) is the codec's data INPUT (speaker samples go out
// on it), and "DIN" (GPIO11) carries the mics' data back. Constants below are
// named from the ESP32's point of view so this can't bite twice.
static constexpr int PIN_AUDIO_BCLK  = 9;
static constexpr int PIN_AUDIO_WS    = 10;
static constexpr int PIN_AUDIO_TX    = 12;  // ESP32 -> codec (module label "DOUT")
static constexpr int PIN_AUDIO_RX    = 11;  // codec -> ESP32 (module label "DIN")
static constexpr int PIN_AUDIO_PA_EN = 13;  // speaker power-amp enable
static constexpr int PIN_AUDIO_MCLK  = 42;  // NOT 46 -- input-only
static constexpr int PIN_AUDIO_SDA   = 8;   // codec control I2C (bit-banged)
static constexpr int PIN_AUDIO_SCL   = 3;   // strapping pin; open-drain is safe

// -- LEDC (PWM) channel allocation -- keep unique across the board ------------
static constexpr int LEDC_CH_LED_GREEN  = 0;
static constexpr int LEDC_CH_LED_YELLOW = 1;
static constexpr int LEDC_CH_LED_RED    = 2;
static constexpr int LEDC_CH_LCD_BL     = 3;
