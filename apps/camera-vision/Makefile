# camera-vision -- ESP32 camera firmware, built and flashed via PlatformIO.
#
#   make build         compile the firmware
#   make flash         build + upload over USB
#   make monitor       open the serial monitor (read the camera's IP here)
#   make dev           flash, then monitor
#   make devices       list attached serial ports
#   make clean         remove build artifacts
#
# Overrides:
#   make flash PORT=/dev/ttyUSB0      pin the serial port
#   make build ENV=esp32-s3           pick a platformio [env]
#
# WiFi creds (WIFI_SSID/WIFI_PASS) are loaded from the repo-root .env and
# baked into the firmware at build time. `pio` is resolved from the repo venv
# if it isn't on PATH. For versioned fleet deploys use tools/iotctl/ instead.

ROOT   := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
FW_DIR := firmware
ENV    ?= sunfounder
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
