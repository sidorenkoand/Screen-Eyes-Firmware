#include "eye_animator.h"

#include <cmath>

namespace {

struct EmotionPreset {
  LidShape lid;
  float pupil;
  float gazeBiasY;  // added to the vertical gaze (sad and sleepy look down a bit)
};

//                            upper  tilt   curve  lower  lowCurve  pupil  biasY
const EmotionPreset kPresets[] = {
  /* Neutral   */ {{ 0.62f,  0.00f, 0.55f, 0.66f, -0.35f}, 0.50f,  0.00f},
  /* Happy     */ {{ 0.62f,  0.00f, 0.60f, 0.32f,  0.80f}, 0.60f, -0.15f},
  /* Sad       */ {{ 0.45f, -0.60f, 0.15f, 0.66f, -0.35f}, 0.55f,  0.20f},
  /* Angry     */ {{ 0.36f,  0.80f, 0.00f, 0.58f, -0.20f}, 0.35f,  0.00f},
  /* Surprised */ {{ 0.90f,  0.00f, 0.50f, 0.85f, -0.30f}, 0.20f,  0.00f},
  /* Sleepy    */ {{ 0.12f, -0.10f, 0.25f, 0.62f, -0.35f}, 0.45f,  0.15f},
};
static_assert(sizeof(kPresets) / sizeof(kPresets[0]) == (int)Emotion::Count,
              "emotion presets out of sync");

// Default colors (RGB565): crimson-pink iris, warm skin tone.
constexpr uint16_t kDefaultIrisColor = ((0xE0 & 0xF8) << 8) | ((0x40 & 0xFC) << 3) | (0x6A >> 3);  // #E0406A
constexpr uint16_t kDefaultLidColor  = ((0xFD & 0xF8) << 8) | ((0xCC & 0xFC) << 3) | (0xA2 >> 3);  // #FDCCA2

// Gaze and lid smoothing time constants, ms.
constexpr float kGazeTau  = 35.0f;   // saccades are fast
constexpr float kLidTau   = 110.0f;
constexpr float kPupilTau = 300.0f;

inline float approach(float cur, float target, float dtMs, float tauMs) {
  return target + (cur - target) * expf(-dtMs / tauMs);
}

inline float smoothstep(float t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  return t * t * (3 - 2 * t);
}

}  // namespace

EyeAnimator::EyeAnimator(uint32_t seed)
    : rng_(seed ? seed : 1),
      irisColor_(kDefaultIrisColor),
      lidColor_(kDefaultLidColor) {
  lid_ = kPresets[(int)Emotion::Neutral].lid;
  pupil_ = kPresets[(int)Emotion::Neutral].pupil;
  frames_[kRight].isRightEye = true;
}

// --- Commands --------------------------------------------------------------

void EyeAnimator::setEmotion(Emotion e) {
  if (e < Emotion::Count) emotion_ = e;
}

void EyeAnimator::setEffect(Effect e) {
  if (e < Effect::Count) effect_ = e;
}

void EyeAnimator::setIrisColor(uint16_t rgb565) { irisColor_ = rgb565; }

void EyeAnimator::setLidColor(uint16_t rgb565) { lidColor_ = rgb565; }

void EyeAnimator::lookAt(float x, float y, uint32_t holdMs) {
  targetGazeX_ = x < -1 ? -1 : (x > 1 ? 1 : x);
  targetGazeY_ = y < -1 ? -1 : (y > 1 ? 1 : y);
  manualGazeUntil_ = lastMs_ + holdMs;
  manualGazeActive_ = true;
}

void EyeAnimator::setAutoGaze(bool on) {
  autoGaze_ = on;
  if (on) nextSaccadeMs_ = lastMs_;
}

void EyeAnimator::setAutoBlink(bool on) {
  autoBlink_ = on;
  if (on) scheduleNextBlink(lastMs_);
}

void EyeAnimator::blink() { pendingBlink_ = true; }

void EyeAnimator::wink(int eye) {
  if (eye == kLeft || eye == kRight) pendingWink_ = eye;
}

// --- Internals -------------------------------------------------------------

float EyeAnimator::random01() {
  // xorshift32
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return (rng_ & 0xFFFFFF) / (float)0x1000000;
}

void EyeAnimator::startBlink(int eye, uint32_t nowMs, bool slow) {
  Blink& b = blinks_[eye];
  b.active = true;
  b.startMs = nowMs;
  if (slow) {
    b.closeMs = 120;
    b.holdMs = 250;
    b.openMs = 200;
  } else if (emotion_ == Emotion::Sleepy) {
    b.closeMs = 150;
    b.holdMs = 120;
    b.openMs = 350;
  } else {
    b.closeMs = 60;
    b.holdMs = 30;
    b.openMs = 100;
  }
}

float EyeAnimator::blinkOpenness(const Blink& b, uint32_t nowMs) {
  if (!b.active) return 1;
  uint32_t t = nowMs - b.startMs;
  if (t < b.closeMs) return 1 - smoothstep((float)t / b.closeMs);
  t -= b.closeMs;
  if (t < b.holdMs) return 0;
  t -= b.holdMs;
  if (t < b.openMs) return smoothstep((float)t / b.openMs);
  return 1;
}

void EyeAnimator::scheduleNextBlink(uint32_t nowMs) {
  float minS = 2.0f, maxS = 6.0f;
  if (emotion_ == Emotion::Sleepy) { minS = 1.5f; maxS = 4.0f; }
  if (emotion_ == Emotion::Surprised) { minS = 4.0f; maxS = 8.0f; }
  nextBlinkMs_ = nowMs + (uint32_t)(randomRange(minS, maxS) * 1000);
}

void EyeAnimator::scheduleNextSaccade(uint32_t nowMs) {
  nextSaccadeMs_ = nowMs + (uint32_t)randomRange(800, 3500);
}

// --- Per frame -------------------------------------------------------------

void EyeAnimator::update(uint32_t nowMs) {
  if (!started_) {
    started_ = true;
    lastMs_ = nowMs;
    scheduleNextBlink(nowMs);
    scheduleNextSaccade(nowMs);
  }
  float dt = (float)(nowMs - lastMs_);
  if (dt > 100) dt = 100;  // avoid jumps after a stall
  lastMs_ = nowMs;

  bool anyBlink = blinks_[kLeft].active || blinks_[kRight].active;

  // Commanded blink / wink.
  if (pendingBlink_) {
    pendingBlink_ = false;
    startBlink(kLeft, nowMs, false);
    startBlink(kRight, nowMs, false);
    scheduleNextBlink(nowMs);
  }
  if (pendingWink_ >= 0) {
    startBlink(pendingWink_, nowMs, true);
    pendingWink_ = -1;
    scheduleNextBlink(nowMs);
  }

  // Automatic blinking, sometimes a double blink.
  if (autoBlink_ && !anyBlink && effect_ != Effect::Closed) {
    if (doubleBlinkAtMs_ && nowMs >= doubleBlinkAtMs_) {
      doubleBlinkAtMs_ = 0;
      startBlink(kLeft, nowMs, false);
      startBlink(kRight, nowMs, false);
    } else if (nowMs >= nextBlinkMs_) {
      startBlink(kLeft, nowMs, false);
      startBlink(kRight, nowMs, false);
      if (random01() < 0.15f) doubleBlinkAtMs_ = nowMs + 260;
      scheduleNextBlink(nowMs);
    }
  }

  // Gaze: manual target holds for a while, then idle saccades take over.
  if (manualGazeActive_ && (int32_t)(nowMs - manualGazeUntil_) >= 0) {
    manualGazeActive_ = false;
    scheduleNextSaccade(nowMs);
  }
  if (!manualGazeActive_ && autoGaze_ && nowMs >= nextSaccadeMs_) {
    if (random01() < 0.3f) {
      targetGazeX_ = 0;
      targetGazeY_ = 0;
    } else {
      float a = randomRange(0, 6.2831853f);
      float r = sqrtf(random01()) * 0.85f;
      targetGazeX_ = r * cosf(a);
      targetGazeY_ = r * sinf(a) * 0.7f;  // vertical movements are smaller
    }
    scheduleNextSaccade(nowMs);
    // People often blink when they shift their gaze.
    if (autoBlink_ && !anyBlink && random01() < 0.2f) {
      startBlink(kLeft, nowMs, false);
      startBlink(kRight, nowMs, false);
      scheduleNextBlink(nowMs);
    }
  }

  // Smooth towards targets.
  const EmotionPreset& p = kPresets[(int)emotion_];
  float tgy = targetGazeY_ + p.gazeBiasY;
  if (tgy > 1) tgy = 1;
  gazeX_ = approach(gazeX_, targetGazeX_, dt, kGazeTau);
  gazeY_ = approach(gazeY_, tgy, dt, kGazeTau);

  lid_.upper      = approach(lid_.upper,      p.lid.upper,      dt, kLidTau);
  lid_.upperTilt  = approach(lid_.upperTilt,  p.lid.upperTilt,  dt, kLidTau);
  lid_.upperCurve = approach(lid_.upperCurve, p.lid.upperCurve, dt, kLidTau);
  lid_.lower      = approach(lid_.lower,      p.lid.lower,      dt, kLidTau);
  lid_.lowerCurve = approach(lid_.lowerCurve, p.lid.lowerCurve, dt, kLidTau);

  float pupilTarget = p.pupil + 0.05f * sinf(nowMs * 0.0013f);  // slow "breathing"
  pupil_ = approach(pupil_, pupilTarget, dt, kPupilTau);

  for (int i = 0; i < 2; i++) {
    float open = blinkOpenness(blinks_[i], nowMs);
    if (blinks_[i].active && open >= 1 && nowMs - blinks_[i].startMs > 10) {
      blinks_[i].active = false;
    }
    EyeFrame& f = frames_[i];
    f.gazeX = gazeX_;
    f.gazeY = gazeY_;
    f.pupil = pupil_;
    f.openness = open;
    f.lid = lid_;
    f.effect = effect_;
    f.irisColor = irisColor_;
    f.lidColor = lidColor_;
    f.timeMs = nowMs;
    f.isRightEye = (i == kRight);
  }
}
