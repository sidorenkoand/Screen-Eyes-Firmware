#pragma once

#include <LovyanGFX.hpp>
#include "eye_types.h"

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Draws one anime-style eye. The round display shows the inscribed circle of
// the square screen, so the whole screen acts as the eye opening.
//
// Draw order: sclera -> iris/effect -> eyelids (skin, lash line, crease).
//
// The screen is drawn in horizontal bands (a full 240x240 frame does not fit
// in one free RAM block on ESP32-C3):
//
//   renderer.prepare(frame);                 // once per eye per frame
//   for each band: renderer.drawBand(sprite, bandTop); push sprite
//
// Speed (ESP32-C3 has no FPU): the iris (outline, gradient, streaks) is
// rendered once into a cache and copied each frame; eyelids use fixed-point
// math with per-column tables computed in begin().
class EyeRenderer {
public:
  static constexpr int kMaxSize = 240;

  uint16_t scleraColor  = rgb565(0xFF, 0xF3, 0xE2);  // warm white
  uint16_t lashColor    = rgb565(0x45, 0x12, 0x0A);  // dark brown lash line
  uint16_t heartColor   = rgb565(0xF0, 0x2A, 0x5A);

  // Allocates the iris cache (~32 KB) and builds lookup tables. Call once.
  bool begin(int screenSize = kMaxSize);

  // Computes per-eye data for this frame; rebuilds the iris cache if the color changed.
  void prepare(const EyeFrame& f);

  // Draws the band of the screen starting at row yOffset (band height = sprite height).
  void drawBand(LGFX_Sprite& band, int yOffset) const;

private:
  struct Column {
    int16_t ui;         // horizontal position, +4096 = inner corner (towards the other eye)
    int16_t bow;        // 1 - u^2, Q12
    int16_t lashW;      // upper lash line thickness, 1/16 px
    int16_t creaseOff;  // crease distance above the lash line, 1/16 px
    int16_t creaseW;    // crease half thickness, 1/16 px (0 = no crease)
    int16_t lowW;       // lower lash line thickness, 1/16 px
  };

  void buildIrisCache(uint16_t baseColor);
  void drawIris(LGFX_Sprite& s, int oy16) const;
  void drawLids(LGFX_Sprite& s, int oy16) const;
  void drawHeart(LGFX_Sprite& s, float cx, float cy, float size) const;
  void drawCross(LGFX_Sprite& s, float cx, float cy, float size) const;
  void buildSpiral();
  void drawSpiral(LGFX_Sprite& s, int oy) const;
  void drawClosed(LGFX_Sprite& s, float oy) const;

  int size_ = kMaxSize;
  Column cols_[2][kMaxSize];  // [isRightEye][x]

  // Iris cache: outline + gradient + streaks over the sclera color.
  LGFX_Sprite irisCache_;
  int irisCacheSize_ = 0;
  uint16_t irisCacheColor_ = 0;
  bool irisCacheValid_ = false;

  // Spiral effect: drawn by stamping a pre-rendered anti-aliased dot along
  // the curve (16 masks for 1/4-pixel offsets), points computed once per eye.
  static constexpr int kDotMax = 12;
  static constexpr int kSpiralMaxPoints = 700;
  uint8_t dotMask_[16][kDotMax * kDotMax];
  int dotSize_ = 0;
  int16_t spiralX4_[kSpiralMaxPoints];  // 1/4 px, screen coordinates
  int16_t spiralY4_[kSpiralMaxPoints];
  int spiralCount_ = 0;

  // Per-frame state (set by prepare).
  EyeFrame f_;
  int32_t up16_[kMaxSize];  // upper lid edge per column, 1/16 px, screen coordinates
  int32_t lo16_[kMaxSize];  // lower lid edge per column
  bool showCrease_ = true;
  float ix_ = 0, iy_ = 0, ir_ = 0;  // iris center and radius, screen coordinates
  int irisX_ = 0, irisY_ = 0;       // iris center rounded to whole pixels
};
