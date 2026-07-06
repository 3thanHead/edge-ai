#pragma once
// Board pin map -- ESP32-S3-DevKitC-1 (WROOM-1) on a passive GPIO extension
// board. Everything is user-wired on a breadboard; keep all pin choices here.
//
// Pin choice matters on the S3. AVOID: 0/3/45/46 (strapping), 19/20 (USB),
// 43/44 (UART console), 26-32 (flash), 33-37 (octal PSRAM), 48 (onboard RGB).
// SAFE general-purpose pins broken out on this board:
//   4, 5, 6, 7, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47

#define BOARD_NAME "esp32s3-devkit"

#ifndef FW_VERSION
#define FW_VERSION "0.1.0"
#endif

// -- Demo actuators (wired) ---------------------------------------------------
static constexpr int PIN_LED_1       = 4;   // red LED  -> resistor -> GND rail
static constexpr int PIN_LED_2       = 5;   // green LED -> resistor -> GND rail
static constexpr int PIN_ONBOARD_RGB = 48;  // DevKit's addressable RGB LED
static constexpr int PIN_SERVO       = 16;  // SG90 signal (orange); 5V + GND rails

// The servo's real travel. Compass/heading commands (0-360) are scaled into
// this range, so N/E/S/W work on a 180-degree SG90 (just compressed 2:1).
// Swap in a 270/360-degree positional servo -> update this one constant.
static constexpr int SERVO_RANGE_DEG = 180;

// -- LCD (kit's 2.0" ST7789 240x320 SPI) -- removed (Phase 3) -----------------
// The ST7789 face was pulled from the demo build; pins 38-42 + BL 21 are free.
// -- Audio (kit's codec module: mic + speaker) -- Phase 3, no driver yet ------
