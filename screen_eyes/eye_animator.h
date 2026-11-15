#pragma once

#include "eye_types.h"

// Turns high-level commands (emotion, effect, look at, blink...) into
// per-frame EyeFrame values for both eyes: smooth transitions, automatic
// blinking and idle gaze movement (saccades).
//
// Time is passed in explicitly (milliseconds) so the same code runs on the
// ESP32 and in the desktop preview.
class EyeAnimator {
public:
  static constexpr int kLeft = 0;
  static constexpr int kRight = 1;

  explicit EyeAnimator(uint32_t seed = 1);

  // --- Commands ---
  void setEmotion(Emotion e);
  void setEffect(Effect e);
  void setIrisColor(uint16_t rgb565);
  void setLidColor(uint16_t rgb565);
  void lookAt(float x, float y, uint32_t holdMs = 3000);  // -1..1; auto gaze resumes after holdMs
  void setAutoGaze(bool on);
  void setAutoBlink(bool on);
  void blink();
  void wink(int eye);

  Emotion emotion() const { return emotion_; }
  Effect effect() const { return effect_; }
  uint16_t irisColor() const { return irisColor_; }
  uint16_t lidColor() const { return lidColor_; }
  bool autoGaze() const { return autoGaze_; }
  bool autoBlink() const { return autoBlink_; }

  // --- Per frame ---
  void update(uint32_t nowMs);
  const EyeFrame& frame(int eye) const { return frames_[eye]; }

private:
  struct Blink {
    bool active = false;
    uint32_t startMs = 0;
    uint32_t closeMs = 0;  // closing duration
    uint32_t holdMs = 0;   // fully closed duration
    uint32_t openMs = 0;   // opening duration
  };

  float random01();
  float randomRange(float lo, float hi) { return lo + (hi - lo) * random01(); }
  void startBlink(int eye, uint32_t nowMs, bool slow);
  float blinkOpenness(const Blink& b, uint32_t nowMs);
  void scheduleNextBlink(uint32_t nowMs);
  void scheduleNextSaccade(uint32_t nowMs);

  uint32_t rng_;

  Emotion emotion_ = Emotion::Neutral;
  Effect effect_ = Effect::None;
  uint16_t irisColor_;
  uint16_t lidColor_;
  bool autoGaze_ = true;
  bool autoBlink_ = true;

  // Targets and smoothed current values.
  float targetGazeX_ = 0, targetGazeY_ = 0;
  float gazeX_ = 0, gazeY_ = 0;
  LidShape lid_{};
  float pupil_ = 0.5f;

  uint32_t lastMs_ = 0;
  bool started_ = false;
  uint32_t manualGazeUntil_ = 0;
  uint32_t nextSaccadeMs_ = 0;
  uint32_t nextBlinkMs_ = 0;
  bool manualGazeActive_ = false;
  bool pendingBlink_ = false;
  int pendingWink_ = -1;
  uint32_t doubleBlinkAtMs_ = 0;

  Blink blinks_[2];
  EyeFrame frames_[2];
};
