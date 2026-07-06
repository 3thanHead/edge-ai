#pragma once
#include "core/Component.h"
#include <TFT_eSPI.h>

// The robot face: an EMO-style pair of animated eyes on the on-board ST7789
// TFT. Owns the display. Moods change the eye shape; blinking and gaze are
// non-blocking and driven from loop(). It's an actuator like any other -- the
// LLM agent sets the mood to match the conversation through handleCommand:
//
//   face <mood>       neutral | happy | sad | angry | sleepy | surprised
//   face blink
//   face look <dir>   left | right | up | down | center
//   face wake | sleep
class Face : public Component {
 public:
  enum Mood { NEUTRAL, HAPPY, SAD, ANGRY, SLEEPY, SURPRISED };
  enum Gaze { CENTER, LEFT, RIGHT, UP, DOWN };

  explicit Face(const char* name);

  void begin() override;
  void loop() override;
  bool handleCommand(const String& action, const String& arg) override;
  String status() const override;

  void setMood(Mood m);
  void setGaze(Gaze g);
  void triggerBlink();
  void wake();
  void sleep();

 private:
  TFT_eSPI tft_;
  TFT_eSprite canvas_;   // off-screen buffer for flicker-free frames
  bool useSprite_ = false;

  Mood mood_ = NEUTRAL;
  Gaze gaze_ = CENTER;
  bool awake_ = true;

  // Blink / open-close animation state.
  bool blinking_ = false;
  float openness_ = 1.0f;   // 1 = fully open, 0 = closed
  int blinkDir_ = -1;       // -1 closing, +1 opening
  uint32_t nextAutoBlink_ = 0;
  uint32_t lastFrame_ = 0;

  void render();
  template <class G> void renderTo(G& gfx);
  template <class G> void drawEye(G& gfx, int cx, int cy, uint16_t color, bool left);
  const char* moodName() const;
};
