#include "components/AudioCodec.h"

#include <driver/i2s.h>
#include <driver/pcnt.h>
#include <soc/gpio_periph.h>
#include <soc/io_mux_reg.h>

#include "board_config.h"

static constexpr int kSampleRate = 16000;
static constexpr i2s_port_t kPort = I2S_NUM_0;

AudioCodec::AudioCodec(const char* name) : Component(name) {}

// -- bit-banged I2C (open-drain via pinMode flips; ~100 kHz) -------------------
// Release = INPUT_PULLUP (line floats high), drive = OUTPUT low. The module
// has its own pull-ups; the weak internal ones just help the idle state.

static inline void sdaRelease() { pinMode(PIN_AUDIO_SDA, INPUT_PULLUP); }
static inline void sdaLow()     { pinMode(PIN_AUDIO_SDA, OUTPUT); digitalWrite(PIN_AUDIO_SDA, LOW); }
static inline void sclRelease() { pinMode(PIN_AUDIO_SCL, INPUT_PULLUP); }
static inline void sclLow()     { pinMode(PIN_AUDIO_SCL, OUTPUT); digitalWrite(PIN_AUDIO_SCL, LOW); }
static inline void i2cDelay()   { delayMicroseconds(5); }

void AudioCodec::i2cIdle() {
  sdaRelease();
  sclRelease();
  i2cDelay();
}

void AudioCodec::i2cStart() {
  i2cIdle();
  sdaLow();
  i2cDelay();
  sclLow();
  i2cDelay();
}

void AudioCodec::i2cStop() {
  sdaLow();
  i2cDelay();
  sclRelease();
  i2cDelay();
  sdaRelease();
  i2cDelay();
}

bool AudioCodec::i2cWriteByte(uint8_t b) {
  for (int i = 7; i >= 0; --i) {
    (b & (1 << i)) ? sdaRelease() : sdaLow();
    i2cDelay();
    sclRelease();
    i2cDelay();
    sclLow();
  }
  sdaRelease();  // let the target drive ACK
  i2cDelay();
  sclRelease();
  i2cDelay();
  bool ack = digitalRead(PIN_AUDIO_SDA) == LOW;
  sclLow();
  i2cDelay();
  return ack;
}

uint8_t AudioCodec::i2cReadByte(bool ack) {
  uint8_t v = 0;
  sdaRelease();
  for (int i = 7; i >= 0; --i) {
    i2cDelay();
    sclRelease();
    i2cDelay();
    if (digitalRead(PIN_AUDIO_SDA)) v |= (1 << i);
    sclLow();
  }
  ack ? sdaLow() : sdaRelease();  // ACK to keep reading, NACK to end
  i2cDelay();
  sclRelease();
  i2cDelay();
  sclLow();
  sdaRelease();
  i2cDelay();
  return v;
}

bool AudioCodec::i2cProbe(uint8_t addr) {
  i2cStart();
  bool ack = i2cWriteByte((uint8_t)(addr << 1));  // write address
  i2cStop();
  return ack;
}

bool AudioCodec::regWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  i2cStart();
  bool ok = i2cWriteByte((uint8_t)(addr << 1)) && i2cWriteByte(reg) &&
            i2cWriteByte(val);
  i2cStop();
  return ok;
}

int AudioCodec::regRead(uint8_t addr, uint8_t reg) {
  i2cStart();
  bool ok = i2cWriteByte((uint8_t)(addr << 1)) && i2cWriteByte(reg);
  if (!ok) {
    i2cStop();
    return -1;
  }
  i2cStart();  // repeated start
  if (!i2cWriteByte((uint8_t)((addr << 1) | 1))) {
    i2cStop();
    return -1;
  }
  uint8_t v = i2cReadByte(/*ack=*/false);
  i2cStop();
  return v;
}

String AudioCodec::scanBus() {
  String hits;
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    if (!i2cProbe(addr)) continue;
    if (hits.length()) hits += " ";
    hits += "0x" + String(addr, HEX);
    if (addr == 0x18) es8311Addr_ = addr;
    if (addr >= 0x40 && addr <= 0x43) es7210Addr_ = addr;
  }
  return hits;
}

// -- chip bring-up --------------------------------------------------------------
// Register values resolved from esp-adf's es8311.c / es7210.c coeff tables for
// this exact clocking: slave chips, MCLK from pad = 256*fs = 4.096 MHz,
// LRCK 16 kHz, 16-bit standard I2S.

bool AudioCodec::es8311Init() {
  const uint8_t a = (uint8_t)es8311Addr_;
  int id = regRead(a, 0xFD);
  if (id != 0x83) {
    Serial.printf("[%s] chip@0x18 id=0x%02X (expected ES8311 0x83)\n", name_, id);
    if (id < 0) return false;  // no ACK on read -- give up, else try anyway
  }
  bool ok = regWrite(a, 0x44, 0x08);  // I2C noise immunity; first write can
  ok &= regWrite(a, 0x44, 0x08);      // fail on this chip, so write twice
  ok &= regWrite(a, 0x01, 0x30);
  ok &= regWrite(a, 0x02, 0x00);      // pre_div=1, pre_multi=x1
  ok &= regWrite(a, 0x03, 0x10);      // ADC OSR
  ok &= regWrite(a, 0x16, 0x24);
  ok &= regWrite(a, 0x04, 0x20);      // DAC OSR
  ok &= regWrite(a, 0x05, 0x00);      // adc_div=1, dac_div=1
  ok &= regWrite(a, 0x0B, 0x00);
  ok &= regWrite(a, 0x0C, 0x00);
  ok &= regWrite(a, 0x10, 0x1F);
  ok &= regWrite(a, 0x11, 0x7F);
  ok &= regWrite(a, 0x00, 0x80);      // CSM power on, slave mode (bit6=0)
  ok &= regWrite(a, 0x01, 0x3F);      // all clocks on, MCLK from pad
  ok &= regWrite(a, 0x07, 0x00);      // LRCK divider high (256 -> 0x00FF)
  ok &= regWrite(a, 0x08, 0xFF);      // LRCK divider low
  ok &= regWrite(a, 0x06, 0x03);      // BCLK div 4, not inverted
  ok &= regWrite(a, 0x13, 0x10);
  ok &= regWrite(a, 0x1B, 0x0A);
  ok &= regWrite(a, 0x1C, 0x6A);
  ok &= regWrite(a, 0x09, 0x0C);      // DAC port: I2S std, 16-bit, running
  ok &= regWrite(a, 0x0A, 0x0C);      // ADC port: I2S std, 16-bit, running
  ok &= regWrite(a, 0x17, 0xBF);      // ADC volume 0 dB
  ok &= regWrite(a, 0x0E, 0x02);      // power up analog
  ok &= regWrite(a, 0x12, 0x00);      // enable DAC
  ok &= regWrite(a, 0x14, 0x1A);      // analog PGA, DMIC off
  ok &= regWrite(a, 0x0D, 0x01);      // power up digital
  ok &= regWrite(a, 0x15, 0x40);      // ADC ramp rate
  ok &= regWrite(a, 0x37, 0x08);      // DAC ramp rate
  ok &= regWrite(a, 0x45, 0x00);
  ok &= regWrite(a, 0x44, 0x58);      // internal ref ADCL + DACR
  setDacVolume(volume_);
  return ok;
}

bool AudioCodec::es7210Init() {
  const uint8_t a = (uint8_t)es7210Addr_;
  bool ok = regWrite(a, 0x00, 0xFF);  // full reset
  ok &= regWrite(a, 0x00, 0x41);
  ok &= regWrite(a, 0x01, 0x3F);      // clocks on
  ok &= regWrite(a, 0x09, 0x30);      // chip state cycle
  ok &= regWrite(a, 0x0A, 0x30);      // power-up state cycle
  ok &= regWrite(a, 0x23, 0x2A);      // ADC12 HPF quick setup
  ok &= regWrite(a, 0x22, 0x0A);
  ok &= regWrite(a, 0x20, 0x0A);      // ADC34 HPF
  ok &= regWrite(a, 0x21, 0x2A);
  // Slave mode + channel-mode nibble 0x10. The esp-adf driver leaves the
  // high nibble at 0, which on this chip revision comes up in a 16-slot
  // TDM-ish arrangement -- output was 2 live slots + 14 zeros (audio at
  // fs/8). Found by a live register sweep; 0x10 = plain 2-channel output.
  ok &= regWrite(a, 0x08, 0x10);
  ok &= regWrite(a, 0x40, 0x43);      // analog on, VMID 5K start
  ok &= regWrite(a, 0x41, 0x70);      // MIC1/2 bias 2.87 V
  ok &= regWrite(a, 0x42, 0x70);      // MIC3/4 bias
  ok &= regWrite(a, 0x07, 0x20);      // OSR
  ok &= regWrite(a, 0x02, 0xC1);      // adc_div=1 + doubler + dll (4.096 MHz)
  ok &= regWrite(a, 0x04, 0x01);      // LRCK divider = 256 (0x100)
  ok &= regWrite(a, 0x05, 0x00);
  ok &= regWrite(a, 0x11, 0x60);      // I2S std, 16-bit
  ok &= regWrite(a, 0x12, 0x00);      // no TDM (2 mics -> plain stereo L/R)
  ok &= regWrite(a, 0x4B, 0x00);      // MIC1/2 bias + ADC + PGA power on
  ok &= regWrite(a, 0x4C, 0xFF);      // MIC3/4 stay off
  setMicGain(micGain_);               // REG43/44: PGA on + gain
  ok &= regWrite(a, 0x47, 0x08);      // MIC1 power
  ok &= regWrite(a, 0x48, 0x08);      // MIC2 power
  ok &= regWrite(a, 0x49, 0x08);
  ok &= regWrite(a, 0x4A, 0x08);
  ok &= regWrite(a, 0x06, 0x00);      // power-down off
  ok &= regWrite(a, 0x01, 0x34);      // enable ADC1/2 clocks
  return ok;
}

void AudioCodec::setDacVolume(uint8_t volume) {
  // 0-100 -> 0x00-0xBF; 0xBF = 0 dB on the ES8311 (values above add gain).
  if (es8311Addr_ >= 0)
    regWrite((uint8_t)es8311Addr_, 0x32, (uint8_t)((volume * 0xBF) / 100));
}

void AudioCodec::setMicGain(uint8_t gain) {
  if (es7210Addr_ < 0) return;
  uint8_t v = 0x10 | (gain & 0x0F);  // PGA enabled + gain, 3 dB per step
  regWrite((uint8_t)es7210Addr_, 0x43, v);  // MIC1
  regWrite((uint8_t)es7210Addr_, 0x44, v);  // MIC2
}

// -- I2S ----------------------------------------------------------------------

void AudioCodec::i2sInit() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  cfg.sample_rate = kSampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;  // S3 has no APLL; PLL fractional divider is fine
  cfg.tx_desc_auto_clear = true;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;  // 4.096 MHz @ 16 kHz

  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) {
    Serial.printf("[%s] i2s driver install failed\n", name_);
    return;
  }
  if (!applyPins()) {
    Serial.printf("[%s] i2s pin config failed\n", name_);
    return;
  }
  i2s_zero_dma_buffer(kPort);
  i2sReady_ = true;
}

bool AudioCodec::applyPins() {
  i2s_pin_config_t pins = {};
  pins.mck_io_num = PIN_AUDIO_MCLK;
  pins.bck_io_num = PIN_AUDIO_BCLK;
  pins.ws_io_num = PIN_AUDIO_WS;
  // Pins are ESP-perspective in board_config (see the DIN/DOUT gotcha there).
  // swapio (swapped_) stays available as a live wiring sanity check.
  pins.data_out_num = swapped_ ? PIN_AUDIO_RX : PIN_AUDIO_TX;
  pins.data_in_num = swapped_ ? PIN_AUDIO_TX : PIN_AUDIO_RX;
  return i2s_set_pin(kPort, &pins) == ESP_OK;
}

void AudioCodec::playTone(float hz, uint32_t ms) {
  if (!i2sReady_) return;
  ms = min(ms, (uint32_t)2000);  // blocking write; keep the loop responsive
  hz = constrain(hz, 40.0f, 8000.0f);

  bool gateAmp = !amp_;
  if (gateAmp) {
    digitalWrite(PIN_AUDIO_PA_EN, HIGH);
    delay(10);  // amp settle
  }

  const float amplitude = 32767.0f * 0.8f;
  const float step = 2.0f * PI * hz / kSampleRate;
  float phase = 0.0f;
  int16_t buf[512];  // 256 stereo frames
  uint32_t framesLeft = (uint32_t)((uint64_t)kSampleRate * ms / 1000);
  while (framesLeft > 0) {
    uint32_t frames = min(framesLeft, (uint32_t)256);
    for (uint32_t i = 0; i < frames; ++i) {
      int16_t s = (int16_t)(amplitude * sinf(phase));
      phase += step;
      if (phase > 2.0f * PI) phase -= 2.0f * PI;
      buf[2 * i] = s;      // left
      buf[2 * i + 1] = s;  // right
    }
    size_t written = 0;
    i2s_write(kPort, buf, frames * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    framesLeft -= frames;
  }
  i2s_zero_dma_buffer(kPort);

  if (gateAmp) digitalWrite(PIN_AUDIO_PA_EN, LOW);
}

// -- component ----------------------------------------------------------------

void AudioCodec::begin() {
  pinMode(PIN_AUDIO_PA_EN, OUTPUT);
  digitalWrite(PIN_AUDIO_PA_EN, LOW);  // boot silent, like everything else

  i2cIdle();
  found_ = scanBus();
  Serial.printf("[%s] control-bus scan: %s\n", name_,
                found_.length() ? found_.c_str() : "no ACK (check wiring)");

  i2sInit();  // clocks must run before the slave codecs lock on

  if (es8311Addr_ >= 0) {
    es8311Ok_ = es8311Init();
    Serial.printf("[%s] es8311@0x%02X init %s\n", name_, es8311Addr_,
                  es8311Ok_ ? "ok" : "FAILED");
  }
  if (es7210Addr_ >= 0) {
    es7210Ok_ = es7210Init();
    Serial.printf("[%s] es7210@0x%02X init %s (mics 1+2, gain %ddB)\n", name_,
                  es7210Addr_, es7210Ok_ ? "ok" : "FAILED", micGain_ * 3);
  }
}

bool AudioCodec::handleCommand(const String& action, const String& arg) {
  if (action == "beep") {
    playTone(880.0f, 150);
    return true;
  }
  if (action == "tone") {
    // "<hz>" or "<hz>,<ms>"
    int comma = arg.indexOf(',');
    float hz = (comma < 0 ? arg : arg.substring(0, comma)).toFloat();
    if (hz <= 0) return false;
    uint32_t ms = comma < 0 ? 300 : (uint32_t)arg.substring(comma + 1).toInt();
    playTone(hz, ms ? ms : 300);
    return true;
  }
  if (action == "volume") {
    volume_ = (uint8_t)constrain(arg.toInt(), 0, 100);
    setDacVolume(volume_);
    return true;
  }
  if (action == "micgain") {
    micGain_ = (uint8_t)constrain(arg.toInt(), 0, 14);
    setMicGain(micGain_);
    return true;
  }
  if (action == "amp") {
    amp_ = (arg == "on" || arg == "true" || arg == "1");
    digitalWrite(PIN_AUDIO_PA_EN, amp_ ? HIGH : LOW);
    return true;
  }
  if (action == "scan") {
    found_ = scanBus();
    Serial.printf("[%s] control-bus scan: %s\n", name_,
                  found_.length() ? found_.c_str() : "no ACK");
    return true;
  }

  // -- remote debug: interrogate the codec over the HTTP command ack ----------
  if (action == "regr") {  // "18,FD" -> read reg 0xFD of chip 0x18
    int comma = arg.indexOf(',');
    if (comma < 0) return false;
    uint8_t addr = (uint8_t)strtol(arg.substring(0, comma).c_str(), nullptr, 16);
    uint8_t reg = (uint8_t)strtol(arg.substring(comma + 1).c_str(), nullptr, 16);
    int v = regRead(addr, reg);
    debug_ = "regr " + String(addr, HEX) + "," + String(reg, HEX) + "=" +
             (v < 0 ? String("NACK") : "0x" + String(v, HEX));
    Serial.printf("[%s] %s\n", name_, debug_.c_str());
    return true;
  }
  if (action == "regw") {  // "41,11,60" -> write 0x60 to reg 0x11 of chip 0x41
    int c1 = arg.indexOf(','), c2 = arg.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) return false;
    uint8_t addr = (uint8_t)strtol(arg.substring(0, c1).c_str(), nullptr, 16);
    uint8_t reg = (uint8_t)strtol(arg.substring(c1 + 1, c2).c_str(), nullptr, 16);
    uint8_t val = (uint8_t)strtol(arg.substring(c2 + 1).c_str(), nullptr, 16);
    bool ok = regWrite(addr, reg, val);
    debug_ = "regw " + String(addr, HEX) + "," + String(reg, HEX) + "<=0x" +
             String(val, HEX) + (ok ? " ok" : " NACK");
    Serial.printf("[%s] %s\n", name_, debug_.c_str());
    return true;
  }
  if (action == "clkfreq") {
    // Measure the actual frequency on one of our own I2S clock pins: route
    // the pad into the pulse counter, restore the I2S output routing (PCNT
    // config re-muxes the pad to plain input), re-enable the pad's input
    // path, and count rising edges for 5 ms. Audio pins only.
    int gpio = arg.toInt();
    if (gpio != PIN_AUDIO_MCLK && gpio != PIN_AUDIO_BCLK &&
        gpio != PIN_AUDIO_WS && gpio != PIN_AUDIO_TX)
      return false;
    pcnt_config_t pc = {};
    pc.pulse_gpio_num = gpio;
    pc.ctrl_gpio_num = PCNT_PIN_NOT_USED;
    pc.unit = PCNT_UNIT_0;
    pc.channel = PCNT_CHANNEL_0;
    pc.pos_mode = PCNT_COUNT_INC;
    pc.neg_mode = PCNT_COUNT_DIS;
    pc.lctrl_mode = PCNT_MODE_KEEP;
    pc.hctrl_mode = PCNT_MODE_KEEP;
    pc.counter_h_lim = 32767;
    pc.counter_l_lim = 0;
    pcnt_unit_config(&pc);
    pcnt_set_filter_value(PCNT_UNIT_0, 0);
    pcnt_filter_disable(PCNT_UNIT_0);
    applyPins();  // put the I2S signal back on the pad
    PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[gpio]);
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);
    delayMicroseconds(5000);
    pcnt_counter_pause(PCNT_UNIT_0);
    int16_t count = 0;
    pcnt_get_counter_value(PCNT_UNIT_0, &count);
    debug_ = "gpio" + String(gpio) + " = " + String(count * 200) + " Hz";
    Serial.printf("[%s] clkfreq %s\n", name_, debug_.c_str());
    return true;
  }
  if (action == "swapio") {
    swapped_ = !swapped_;
    bool ok = applyPins();
    debug_ = String("swapio: tx=gpio") +
             (swapped_ ? PIN_AUDIO_RX : PIN_AUDIO_TX) + " rx=gpio" +
             (swapped_ ? PIN_AUDIO_TX : PIN_AUDIO_RX) + (ok ? "" : " ERR");
    Serial.printf("[%s] %s\n", name_, debug_.c_str());
    return true;
  }
  if (action == "rxpeek") {
    // Grab raw stereo frames straight off I2S RX (pre-downmix) and report
    // per-channel peaks + the first pairs verbatim. Briefly competes with the
    // mic streamer for frames, which is fine for a debug snapshot.
    int16_t raw[256];
    size_t got = 0;
    i2s_read(kPort, raw, sizeof(raw), &got, pdMS_TO_TICKS(200));
    size_t frames = got / (2 * sizeof(int16_t));
    int16_t lp = 0, rp = 0;
    for (size_t f = 0; f < frames; ++f) {
      int16_t l = abs(raw[2 * f]), r = abs(raw[2 * f + 1]);
      if (l > lp) lp = l;
      if (r > rp) rp = r;
    }
    debug_ = "rx " + String(frames) + "f Lpk=" + String(lp) +
             " Rpk=" + String(rp) + " [";
    for (size_t f = 0; f < min(frames, (size_t)4); ++f) {
      debug_ += String((uint16_t)raw[2 * f], HEX) + "/" +
                String((uint16_t)raw[2 * f + 1], HEX) + " ";
    }
    debug_ += "]";
    Serial.printf("[%s] %s\n", name_, debug_.c_str());
    return true;
  }
  return false;
}

String AudioCodec::status() const {
  String s;
  if (es8311Addr_ >= 0) s += String("es8311:") + (es8311Ok_ ? "ok" : "FAIL");
  if (es7210Addr_ >= 0)
    s += String(s.length() ? " " : "") + "es7210:" + (es7210Ok_ ? "ok" : "FAIL");
  if (!s.length()) s = found_.length() ? "unknown@" + found_ : "no codec";
  s += i2sReady_ ? "" : " (i2s down)";
  s += " amp=" + String(amp_ ? "on" : "off") + " vol=" + String(volume_) +
       " micgain=" + String(micGain_);
  if (debug_.length()) s += " | " + debug_;
  return s;
}
