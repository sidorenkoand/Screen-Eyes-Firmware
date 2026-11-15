#pragma once

#include <cstdint>

// Shared types for the eye renderer, animator and command parser.
// Kept free of Arduino headers so the desktop preview tool can reuse them.

enum class Emotion : uint8_t {
  Neutral,
  Happy,
  Sad,
  Angry,
  Surprised,
  Sleepy,
  Count
};

enum class Effect : uint8_t {
  None,     // regular iris and pupil
  Hearts,   // pulsing hearts instead of irises
  Cross,    // "X X" eyes
  Spiral,   // rotating spirals (dizzy)
  Closed,   // closed eyes drawn as a curved line
  Fire,     // burning eye with a vertical slit pupil; no eyelids, no blinking
  Count
};

// Eyelid geometry. All values are normalized to the eye radius.
struct LidShape {
  float upper;       // upper lid opening: 0 = at center line, 1 = at top edge
  float upperTilt;   // >0 inner corner lower (angry), <0 outer corner lower (sad)
  float upperCurve;  // >0 lid edge arches upward in the middle
  float lower;       // lower lid opening: 0 = at center line, 1 = at bottom edge
  float lowerCurve;  // >0 lid edge bulges upward in the middle (smiling eyes)
};

// Everything the renderer needs to draw one eye for one frame.
struct EyeFrame {
  float gazeX = 0;       // -1 (left) .. 1 (right), as seen by the viewer
  float gazeY = 0;       // -1 (up) .. 1 (down)
  float pupil = 0.5f;    // pupil size: 0 = tiny, 1 = huge
  float openness = 1;    // blink factor: 0 = closed, 1 = open
  LidShape lid{};
  Effect effect = Effect::None;
  uint16_t irisColor = 0;   // RGB565, base iris color (gradient is derived from it)
  uint16_t lidColor = 0;    // RGB565, eyelid / skin color
  uint32_t timeMs = 0;      // for animated effects
  bool isRightEye = false;  // mirrors lid shapes (inner corner faces the other eye)
};

const char* emotionName(Emotion e);
const char* effectName(Effect e);
bool parseEmotion(const char* name, Emotion& out);
bool parseEffect(const char* name, Effect& out);
