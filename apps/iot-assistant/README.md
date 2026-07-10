# iot-assistant

On-device firmware for a DIY **smart-home / IoT assistant** running on an
**ESP32-S3**. The end goal is an on-device **LLM agent** that actuates IoT
components as tools — lights, displays, voice — driven from our own home-lab
LLM cluster instead of a third-party cloud. Built **our way** on PlatformIO +
Arduino + a generic C++ component model, with
[xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) as the architectural
reference.

**Status: rebuilt hardware set (fw 0.4).** WiFi + HTTP API + MQTT are live.
The board now carries three status LEDs, the 2.0" ST7789 color face, two
0.96" I2C displays, and the LAFVIN audio codec module (I2S bus + control bus
up; the codec chip itself is not yet identified — `audio scan` reports what
ACKs on its control bus). The device boots with everything **off/blank**;
once WiFi lands, the ST7789 shows the device name, firmware version and IP.
The cluster-side brain lives in [apps/agents](../agents/) — its `led` agent
drives this device.

## Hardware

- **Board:** ESP32-S3-WROOM-1 dev board (DevKitC-1 style) on a *passive* GPIO
  extension board. Everything is breadboard-wired — no fixed shield, so pins
  are your choice. Flash + serial run over the S3's native USB. All peripherals
  are 3.3V logic (the ST7789's VCC goes to **3V3**, not 5V).

| Part | GPIO | Console name |
|---|---|---|
| green LED (= yes/true) | 21 | `led_green` |
| yellow LED (= maybe) | 47 | `led_yellow` |
| red LED (= no/false) | 48 | `led_red` |
| all three as a unit | — | `leds` |
| LAFVIN 2.0" LCD, ST7789 240x320 (SPI) | CLK 4 · CS 5 · DC 6 · MOSI 7 · RST 15 · BL 16 | `lcd` |
| Hosymond 0.96" I2C display #1 | SDA 17 · SCL 18 | `oled_1` |
| Hosymond 0.96" I2C display #2 | SDA 1 · SCL 2 | `oled_2` |
| LAFVIN audio codec (I2S + control I2C) | BCLK 9 · WS 10 · TX 12 · RX 11 · MCLK 42 · PA_EN 13 · SDA 8 · SCL 3 | `audio` |

All pin choices live in [`board_config.h`](firmware/include/board_config.h).
Notes baked into that map:

- **GPIO46 avoided** for MCLK — it's input-only on the S3 and can't drive a
  clock; MCLK sits on 42.
- **GPIO3** (codec SCL) is a strapping pin — safe in practice since I2C is
  open-drain with pull-ups, but confirm on first boot.
- **GPIO48** (red LED) is also the onboard WS2812 RGB's data pin on most
  DevKitC-1 boards. A plain digital level isn't a valid WS2812 frame so the
  RGB should stay dark; if it glitches colors, that's the shared pin.
- **I2C buses:** the two 0.96" displays take both hardware I2C controllers
  (`Wire`/`Wire1`); the codec's low-traffic control bus is bit-banged.
- **Audio module DIN/DOUT labels are host-perspective** (found the hard way):
  the pin silkscreened "DOUT" (GPIO12) is the codec's data *input* and "DIN"
  (GPIO11) carries the mic data out. `board_config.h` names them
  `PIN_AUDIO_TX`/`PIN_AUDIO_RX` from the ESP's point of view; `audio swapio`
  flips them live if a future rewire needs sanity-checking.

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

On a successful flash the serial log prints the chip specs, each `oled_*`
greets with its own name (so you can tell the two panels apart), and the
`audio` line reports its control-bus scan.

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
led_*  on | off | toggle | brightness <0-255> | blink [ms] | solid
led_*  pattern <sos|heartbeat|strobe> | seq <on,off,...ms> | pulse [ms]
leds   alternate [ms] | together [ms] | pattern <name> | answer <yes|no|maybe> | off
lcd    text <msg, '|'=newline> | clear [color] | backlight <on|off|0-255>
oled_* text <msg, '|'=newline> | clear | fill <white|black> | invert <on|off> | contrast <0-255>
audio  beep | tone <hz>[,ms] | volume <0-100> | micgain <0-14> | amp <on|off> | scan
mic    on | off
```

`leds answer` implements the verdict convention: green heartbeats for yes,
red for no, yellow for maybe — the other two go dark. The audio module was
identified on-device via the control-bus scan: an **ES8311** (0x18, speaker
DAC) + **ES7210** (0x41, the dual-mic ADC) — the classic xiaozhi-style 2-in-1
module. Both are initialized at boot as I2S slaves (16 kHz / 16-bit, MCLK =
256·fs; register sequences ported from esp-adf's audio_hal). `audio
beep`/`tone` gate PA_EN around playback automatically; `micgain` sets the
ES7210 PGA in 3 dB steps (default 30 dB).

`mic` streams the codec's mic capture (downmixed to **16 kHz mono int16 PCM,
64 ms frames**) to the agents hub at `ws://<MQTT_HOST>:8810/ws/audio/ingest`,
on by default whenever the link is up. The agents app runs VAD + Whisper over
that stream and fans out transcripts; the chat UI's **🎙 mic** button shows
the live waveform, level meter, and speech captions (click a caption to put
it in the composer).

Hardware gotcha found the hard way: this module's ES7210 boots with the
channel nibble of its mode register (0x08) in a 16-slot TDM-ish state that
the stock esp-adf driver never clears — the mic data came out as 2 samples +
14 zeros (audio at fs/8). `AudioCodec` writes REG08=0x10 (plain 2-channel);
the `audio` debug actions (`regr`/`regw`/`rxpeek`/`clkfreq`/`swapio`) that
found it are kept for future bring-up sessions.

The `name → action` shape is deliberate: it's the same interface the LLM agents
actuate through (each component is a tool), so serial → agent → voice never
disturbs the component layer.

## Roadmap

- **Phase 1 — components.** ✅ Generic component model driving pins.
- **Phase 2 — connectivity + agents.** ✅ WiFi + HTTP + MQTT; the cluster's
  agents ([apps/agents](../agents/)) call components as tools, driven from chat.
- **Phase 3 — faces + voice.** Displays wired (this build). Mic → WS stream to
  the agents hub (`/ws/audio/ingest`) is live (`mic` component), visualized in
  the chat UI. Next: identify the codec chip (`audio scan` → datasheet →
  register init in `AudioCodec`) so the mic carries real audio, then STT →
  agent → TTS → speaker. xiaozhi-esp32 is the audio-pipeline reference.

## Code layout

```
app/                        # standalone/dev LLM agent CLI (the always-on brain is apps/agents)
  main.py                   # python app/main.py "blink the green led fast" -- tool-calls the device API
  device.py                 # stdlib client for the device's HTTP API
Dockerfile                  # containerized agent CLI (docker compose run --rm agent "...")
docker-compose.yml          # one-shot `agent` service; reads this app's .env
Makefile                    # firmware build/flash/monitor shortcuts (PlatformIO)
requirements.txt            # stdlib-only today; kept for the standard app layout
docs/api.md                 # the device's HTTP / MQTT / serial contract
firmware/
  platformio.ini            # env, build flags, lib deps
  include/board_config.h    # THE board pin map (the only board-specific file)
  src/
    main.cpp                # setup(): specs, boots everything off; loop(): tick + IP splash
    Board.{h,cpp}           # instantiates the wired components
    SerialConsole.{h,cpp}   # line-based control surface (stand-in for AI tools)
    core/
      Component.h           # abstract base: begin/loop/handleCommand/status
      ComponentRegistry.*   # owns components, fans out lifecycle calls
    net/
      WifiLink.*  HttpApi.*  MqttLink.*         # connectivity + control surfaces
      AudioStream.*                             # `mic`: I2S RX -> agents hub WS
    components/             # the wired set:
      Led.*  LedGroup.*  DigitalOutput.*        # the three status LEDs
      St7789Lcd.*                               # 2.0" color face (SPI)
      OledDisplay.*                             # the two 0.96" I2C panels
      AudioCodec.*                              # I2S + bit-banged control I2C + PA_EN
```

**Adding a component:** wire it, add its pins to `board_config.h`, declare it
in `Board`, register it in the constructor (plus its library in
`platformio.ini`, if any).
