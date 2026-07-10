# iot-assistant -- ESP32-S3 firmware, built and flashed via PlatformIO.
#
#   make build         compile the firmware
#   make flash         build + upload over USB (enter download mode first:
#                      hold BOOT, tap RESET, release BOOT -- usbip doesn't
#                      forward the auto-reset)
#   make monitor       open the serial monitor
#   make dev           flash, then monitor
#   make devices       list attached serial ports
#   make clean         remove build artifacts
#
# Overrides:
#   make flash PORT=/dev/ttyACM0      pin the serial port
#   make build ENV=esp32s3-devkit     pick a platformio [env]
#
# WiFi creds (WIFI_SSID/WIFI_PASS) are loaded from the repo-root .env and
# baked into the firmware at build time. `pio` is resolved from the repo venv
# if it isn't on PATH.

ROOT   := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
FW_DIR := firmware
ENV    ?= esp32s3-devkit
PIO    ?= $(shell command -v pio 2>/dev/null || echo $(ROOT)/.venv/bin/pio)

# Load repo-root .env (KEY=VALUE lines) into the environment for pio.
LOAD_ENV := set -a; [ -f $(ROOT)/.env ] && . $(ROOT)/.env; set +a;

PORT_UPLOAD  := $(if $(PORT),--upload-port $(PORT),)
PORT_MONITOR := $(if $(PORT),--port $(PORT),)

.PHONY: build flash upload monitor dev devices clean

build:
	$(LOAD_ENV) $(PIO) run -d $(FW_DIR) -e $(ENV)

flash upload:
	$(LOAD_ENV) $(PIO) run -d $(FW_DIR) -e $(ENV) -t upload $(PORT_UPLOAD)

monitor:
	$(PIO) device monitor -d $(FW_DIR) -e $(ENV) $(PORT_MONITOR)

dev: flash monitor

devices:
	$(PIO) device list

clean:
	$(PIO) run -d $(FW_DIR) -e $(ENV) -t clean
