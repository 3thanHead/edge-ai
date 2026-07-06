#include "components/Face.h"

#include "board_config.h"

// Landscape geometry (setRotation(1) turns the 240x320 panel into 320x240).
static constexpr int kScreenW = 320;
static constexpr int kScreenH = 240;
static constexpr uint16_t kEyeColor = TFT_CYAN;
static constexpr int kLeftEyeX = 108;
static constexpr int kRightEyeX = 212;
static constexpr int kEyeY = 120;
static constexpr int kGazeShift = 26;

Face::Face(const char* name) : Component(name), canvas_(&tft_) {}

void Face::begin() {
  tft_.init();
  tft_.setRotation(1);
  tft_.fillScreen(TFT_BLACK);

  // Drive the backlight on (TFT_eSPI also toggles TFT_BL, belt-and-braces).
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  // A full-screen 16-bit sprite is 150 KB -- it lands in PSRAM on the N16R8.
  // If it can't be allocated we fall back to drawing straight to the panel.
  canvas_.setColorDepth(16);
  useSprite_ = (canvas_.createSprite(kScreenW, kScreenH) != nullptr);

  nextAutoBlink_ = millis() + 2000;
  render();
}

void Face::loop() {
  uint32_t now = millis();
  if (now - lastFrame_ < 33) return;  // cap ~30 fps
  lastFrame_ = now;

  bool dirty = false;

  if (awake_ && !blinking_ && now >= nextAutoBlink_) {
    triggerBlink();
  }

  if (blinking_) {
    openness_ += blinkDir_ * 0.34f;   // ~3 frames each way
    if (openness_ <= 0.0f) {
      openness_ = 0.0f;
      blinkDir_ = +1;                 // reverse: start opening
    } else if (openness_ >= 1.0f) {
      openness_ = 1.0f;
      blinking_ = false;
      blinkDir_ = -1;
      nextAutoBlink_ = now + 2000 + (uint32_t)random(0, 3000);
    }
    dirty = true;
  }

  if (dirty) render();
}

void Face::render() {
  if (useSprite_) {
    renderTo(canvas_);
    canvas_.pushSprite(0, 0);
  } else {
    renderTo(tft_);
  }
}

template <class G>
void Face::renderTo(G& gfx) {
  gfx.fillScreen(TFT_BLACK);

  int gx = 0, gy = 0;
  switch (gaze_) {
    case LEFT:  gx = -kGazeShift; break;
    case RIGHT: gx = kGazeShift;  break;
    case UP:    gy = -kGazeShift; break;
    case DOWN:  gy = kGazeShift;  break;
    case CENTER: default: break;
  }

  drawEye(gfx, kLeftEyeX + gx, kEyeY + gy, kEyeColor, /*left=*/true);
  drawEye(gfx, kRightEyeX + gx, kEyeY + gy, kEyeColor, /*left=*/false);
}

template <class G>
void Face::drawEye(G& gfx, int cx, int cy, uint16_t color, bool left) {
  int w = 80;
  int baseH = 96;
  int r = 24;

  if (mood_ == SURPRISED) { w = 88; baseH = 104; r = 44; }
  if (mood_ == SLEEPY)    { baseH = 58; }

  int h = max(6, (int)(baseH * openness_));
  int x = cx - w / 2;
  int y = cy - h / 2;

  gfx.fillRoundRect(x, y, w, h, min(r, h / 2), color);

  // Mood overlays: carve black shapes out of the base eye.
  switch (mood_) {
    case HAPPY:
      // A big black disc below the eye leaves an upward crescent (a smile).
      gfx.fillCircle(cx, cy + h / 2 + h / 3, w, TFT_BLACK);
      break;
    case ANGRY:
      // Inner-top wedge angled down toward the nose.
      if (left)
        gfx.fillTriangle(cx + w / 2, y - 1, x - 1, y - 1, cx + w / 2, y + h / 2, TFT_BLACK);
      else
        gfx.fillTriangle(x, y - 1, cx + w / 2 + 1, y - 1, x, y + h / 2, TFT_BLACK);
      break;
    case SAD:
      // Outer-top wedge (opposite slant to angry).
      if (left)
        gfx.fillTriangle(x, y - 1, cx + w / 2 + 1, y - 1, x, y + h / 2, TFT_BLACK);
      else
        gfx.fillTriangle(cx + w / 2, y - 1, x - 1, y - 1, cx + w / 2, y + h / 2, TFT_BLACK);
      break;
    case SLEEPY:
      // Heavy upper lid.
      gfx.fillRect(x, y, w, h / 3, TFT_BLACK);
      break;
    case NEUTRAL:
    case SURPRISED:
    default:
      break;
  }
}

void Face::setMood(Mood m) { mood_ = m; render(); }
void Face::setGaze(Gaze g) { gaze_ = g; render(); }

void Face::triggerBlink() {
  if (!awake_) return;
  blinking_ = true;
  blinkDir_ = -1;
}

void Face::wake() {
  awake_ = true;
  blinking_ = false;
  openness_ = 1.0f;
  nextAutoBlink_ = millis() + 2000;
  render();
}

void Face::sleep() {
  awake_ = false;
  blinking_ = false;
  openness_ = 0.0f;
  render();
}

bool Face::handleCommand(const String& action, const String& arg) {
  if (action == "neutral")   { setMood(NEUTRAL);   return true; }
  if (action == "happy")     { setMood(HAPPY);     return true; }
  if (action == "sad")       { setMood(SAD);       return true; }
  if (action == "angry")     { setMood(ANGRY);     return true; }
  if (action == "sleepy")    { setMood(SLEEPY);    return true; }
  if (action == "surprised") { setMood(SURPRISED); return true; }
  if (action == "blink")     { triggerBlink();     return true; }
  if (action == "wake")      { wake();             return true; }
  if (action == "sleep")     { sleep();            return true; }
  if (action == "look") {
    if (arg == "left")        setGaze(LEFT);
    else if (arg == "right")  setGaze(RIGHT);
    else if (arg == "up")     setGaze(UP);
    else if (arg == "down")   setGaze(DOWN);
    else                      setGaze(CENTER);
    return true;
  }
  return false;
}

const char* Face::moodName() const {
  switch (mood_) {
    case HAPPY:     return "happy";
    case SAD:       return "sad";
    case ANGRY:     return "angry";
    case SLEEPY:    return "sleepy";
    case SURPRISED: return "surprised";
    case NEUTRAL:   default: return "neutral";
  }
}

String Face::status() const {
  return String(moodName()) + (awake_ ? "" : " asleep");
}
