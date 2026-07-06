# iot-assistant

On-device firmware for a DIY **smart-home / IoT assistant** running on an
**ESP32-S3**. The end goal is an on-device **LLM agent** that actuates IoT
components as tools — lights, relays, sensors, an EMO-style animated face —
driven by voice, wired to our own home-lab LLM cluster instead of a third-party
cloud. Built **our way** on PlatformIO + Arduino + a generic C++ component
model, with [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) as the
architectural reference.

**Status: demo build (fw 0.3).** WiFi + HTTP API + MQTT are live; the LEDs
blink in rates/patterns, the servo speaks degrees and compass points, and the
EMO-style face renders on the ST7789. The cluster-side brain lives in
[apps/agents](../agents/) — its `led` and `face` agents drive this device.

## Hardware

- **Board:** ESP32-S3-WROOM-1 dev board (DevKitC-1 style) on a *passive* GPIO
  extension board. Everything is breadboard-wired — no fixed shield, so pins are
  your choice. Flash + serial run over the S3's native USB.
- **Demo wiring** (kit parts kept: LCD, audio codec + mic, speaker, servo, 2 LEDs):

| Part | GPIO | Console name |
|---|---|---|
| red LED (= no/false) | 4 | `led_1` |
| blue LED (= yes/true) | 5 | `led_2` |
| both as a unit | — | `leds` |
| onboard RGB | 48 | `onboard` |
| SG90 servo | 16 | `servo` |
| ST7789 LCD (SPI) | SCK 40 · MOSI 41 · CS 42 · DC 39 · RST 38 · BL 21 | `face` |
| audio codec (mic + speaker) | *unwired — Phase 3* | — |

All pin choices live in [`board_config.h`](firmware/include/board_config.h).

## Flashing from WSL

The dev board enumerates on Windows as a COM port (USB-UART bridge). To reach it
from WSL, forward it with [usbipd-win](https://github.com/dorssel/usbipd-win):

```powershell
# Windows PowerShell (Admin) -- one-time install if needed:
winget install usbipd
# Find the board (look for "USB-Serial"/CP210x or "USB JTAG/serial"):
usbipd list
usbipd bind   --busid <BUSID>      # one-time
usbipd attach --wsl --busid <BUSID>
```

```sh
# In WSL:
ls /dev/ttyUSB* /dev/ttyACM*        # the board should now appear
cd apps/iot-assistant
make devices                        # confirm PlatformIO sees the port
make flash                          # build + upload
make monitor                        # watch @115200
```

Pin the port if autodetect picks the wrong one: `make flash PORT=/dev/ttyUSB0`.
(CP210x bridges show up as `ttyUSB*`; the S3's native-USB port as `ttyACM*`.)
If the port is permission-denied, add yourself to `dialout` or use `sudo`.

On a successful flash the serial log prints the chip specs and **both LEDs
blink** (LED 1 @250 ms, LED 2 @500 ms).

## Control surfaces

Three surfaces, one command shape — `<component> <action> [arg]`
(full contract in [docs/api.md](docs/api.md)):

1. **Serial console** (dev): type the commands below at the monitor.
2. **HTTP API** (`:80`): `GET /health`, `GET /api/components`,
   `POST /api/command?name=&action=&arg=` — acked request/response.
3. **MQTT** (broker = `MQTT_HOST` from the root .env, the agents stack's
   mosquitto): commands in on `iot/iot-assistant/cmd` (same text shape), acks
   on `.../resp`, retained state JSON on `.../state` (on change + every 10 s),
   LWT `.../availability` online/offline.

```
list | get <name> | help
led_1  on | off | toggle | brightness <0-255> | blink [ms] | solid
led_1  pattern <sos|heartbeat|strobe> | seq <on,off,...ms> | pulse [ms]
leds   alternate [ms] | together [ms] | pattern <name> | answer <yes|no> | off
onboard color <r,g,b> | blink [ms] | off
servo  angle <deg> | heading <0-360> | compass <N|NE|E|SSW|...>
face   happy|sad|angry|sleepy|surprised|neutral | blink | look <dir> | wake | sleep
```

The servo's compass headings scale into `SERVO_RANGE_DEG` (180 for the SG90,
so E=90° maps to 45° physical — swap in a 360° positional servo and change one
constant). `leds answer` implements the yes/no convention: blue heartbeats for
yes, red for no.

The `name → action` shape is deliberate: it's the same interface the LLM agents
actuate through (each component is a tool), so serial → agent → voice never
disturbs the component layer.

## Roadmap

- **Phase 1 — components.** ✅ Generic component model driving pins.
- **Phase 2 — connectivity + agents.** ✅ WiFi + HTTP + MQTT; the cluster's
  agents ([apps/agents](../agents/)) call components as tools, driven from chat.
- **Phase 3 — voice.** Mic (I2S) → WS stream to the agents API
  (`/ws/audio/ingest`, already serving) → STT → agent → TTS → speaker. Needs the
  kit codec identified + wired. xiaozhi-esp32 is the audio-pipeline reference.

## Code layout

```
app/                        # standalone/dev LLM agent CLI (the always-on brain is apps/agents)
  main.py                   # python app/main.py "blink led 1 fast" -- tool-calls the device API
  device.py                 # stdlib client for the device's HTTP API
Dockerfile                  # containerized agent CLI (docker compose run --rm agent "...")
docker-compose.yml          # one-shot `agent` service; reads this app's .env
Makefile                    # firmware build/flash/monitor shortcuts (PlatformIO)
requirements.txt            # stdlib-only today; kept for the standard app layout
docs/api.md                 # the device's HTTP / MQTT / serial contract
firmware/
  platformio.ini            # env, build flags, build_src_filter (parks unused components)
  include/board_config.h    # THE board pin map (the only board-specific file)
  src/
    main.cpp                # setup(): specs + init blink; loop(): tick
    Board.{h,cpp}           # instantiates the wired components
    SerialConsole.{h,cpp}   # line-based control surface (stand-in for AI tools)
    core/
      Component.h           # abstract base: begin/loop/handleCommand/status
      ComponentRegistry.*   # owns components, fans out lifecycle calls
    components/             # Led + DigitalOutput are live; the rest are parked
      DigitalOutput.*  Led.*  Relay.h  Fan.*  RgbLed.*  Button.*
      NeoPixelStrip.*  ServoMotor.*  AnalogSensor.*  Dht11Sensor.*  Face.*
```

**Enabling a parked component:** wire it, add its pin to `board_config.h`,
declare it in `Board`, register it in the constructor, and remove its `-<...>`
line from `build_src_filter` in `platformio.ini` (plus its library, if any).
