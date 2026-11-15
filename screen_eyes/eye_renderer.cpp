#include "eye_renderer.h"

#include <cmath>
#include <cstring>

// Performance notes (ESP32-C3: 160 MHz RISC-V, no FPU, so every float
// operation is a library call):
// - Pixels are written straight into the sprite buffer (Canvas below).
// - Per-frame drawing uses fixed-point coordinates: 1/16 pixel ("x16").
// - Float math is fine for one-off work (iris cache, tables) and for the
//   rarely used effects.
//
// Coordinates are continuous: pixel (x, y) covers [x, x+1) x [y, y+1).

namespace {

constexpr float kPi = 3.14159265f;

// Geometry, relative to the eye radius.
constexpr float kIrisRadius   = 0.60f;  // iris size
constexpr float kGazeRange    = 0.34f;  // how far the iris travels from the center
constexpr float kCloseLine    = 0.12f;  // where the lids meet when closed (below center)
constexpr float kTiltAmount   = 0.35f;
constexpr float kUpperCurveK  = 0.25f;
constexpr float kLowerCurveK  = 0.35f;
constexpr float kLidFollowUp  = 0.12f;  // upper lid follows vertical gaze
constexpr float kLidFollowLow = 0.08f;  // lower lid follows vertical gaze

// Lid details, relative to the eye radius.
constexpr float kLashBase     = 0.060f;  // upper lash line thickness
constexpr float kLashOuter    = 0.035f;  // extra thickness towards the outer corner
constexpr float kLowerLash    = 0.018f;  // lower lash line thickness
constexpr float kCreaseGap    = 0.16f;   // distance from lash line to lid crease

// Spiral effect, relative to the iris radius.
constexpr float kSpiralSize   = 1.05f;   // outer radius
constexpr float kSpiralWidth  = 0.050f;  // stroke half width
constexpr float kSpiralTurns  = 3.0f;
constexpr float kSpiralStep   = 2.0f;    // distance between stamped dots, px

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Float -> fixed point, 1/16 px.
inline int32_t x16(float v) { return (int32_t)lroundf(v * 16); }

// Blend two RGB565 colors; a = 0 -> c0, a = 256 -> c1.
inline uint16_t blend565i(uint16_t c0, uint16_t c1, int a) {
  uint32_t r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
  uint32_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  uint32_t r = (r0 * (256 - a) + r1 * a) >> 8;
  uint32_t g = (g0 * (256 - a) + g1 * a) >> 8;
  uint32_t b = (b0 * (256 - a) + b1 * a) >> 8;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

inline int alpha256(float a) { return (int)(clampf(a, 0, 1) * 256 + 0.5f); }

inline uint16_t blend565(uint16_t c0, uint16_t c1, float a) { return blend565i(c0, c1, alpha256(a)); }
inline uint16_t darken565(uint16_t c, float k) { return blend565(c, 0x0000, k); }
inline uint16_t lighten565(uint16_t c, float k) { return blend565(c, 0xFFFF, k); }

inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

uint32_t isqrt(uint32_t n) {
  uint32_t res = 0, bit = 1u << 30;
  while (bit > n) bit >>= 2;
  while (bit) {
    if (n >= res + bit) {
      n -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  return res;
}

// Floor / ceil division of fixed-point x16 values by 16 (arithmetic shift = floor).
inline int floor16(int32_t v) { return v >> 4; }
inline int ceil16(int32_t v) { return (v + 15) >> 4; }

// Direct access to a 16-bit LovyanGFX sprite buffer (stored byte-swapped).
class Canvas {
public:
  explicit Canvas(LGFX_Sprite& s)
      : w(s.width()), h(s.height()), buf_(static_cast<uint16_t*>(s.getBuffer())) {}

  const int w, h;

  void fill(uint16_t c) {
    uint16_t v = swap16(c);
    uint32_t v2 = v | ((uint32_t)v << 16);
    int n = w * h;
    uint32_t* p = reinterpret_cast<uint32_t*>(buf_);
    for (int i = 0; i < n / 2; i++) p[i] = v2;
    if (n & 1) buf_[n - 1] = v;
  }

  // a: 0..256
  void blend(int x, int y, uint16_t c, int a) {
    if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h || a <= 0) return;
    uint16_t& p = buf_[y * w + x];
    p = a >= 256 ? swap16(c) : swap16(blend565i(swap16(p), c, a));
  }

  // Solid horizontal span [x0, x1] (inclusive), clipped.
  void hspan(int y, int x0, int x1, uint16_t c) {
    if ((unsigned)y >= (unsigned)h) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= w) x1 = w - 1;
    uint16_t v = swap16(c);
    uint16_t* p = buf_ + y * w;
    for (int x = x0; x <= x1; x++) p[x] = v;
  }

  // Copy a block of raw (already byte-swapped) pixels, clipped.
  void blit(const uint16_t* src, int sw, int sh, int x0, int y0) {
    int sx = 0, sy = 0, cw = sw, ch = sh;
    if (x0 < 0) { sx = -x0; cw += x0; x0 = 0; }
    if (y0 < 0) { sy = -y0; ch += y0; y0 = 0; }
    if (x0 + cw > w) cw = w - x0;
    if (y0 + ch > h) ch = h - y0;
    if (cw <= 0 || ch <= 0) return;
    for (int y = 0; y < ch; y++) {
      memcpy(buf_ + (y0 + y) * w + x0, src + (sy + y) * sw + sx, cw * sizeof(uint16_t));
    }
  }

  // Blend a size x size alpha mask (0..255) at (x0, y0), clipped.
  void stamp(int x0, int y0, const uint8_t* mask, int size, uint16_t c) {
    int i0 = x0 < 0 ? -x0 : 0, i1 = x0 + size > w ? w - x0 : size;
    int j0 = y0 < 0 ? -y0 : 0, j1 = y0 + size > h ? h - y0 : size;
    const uint16_t solid = swap16(c);
    for (int j = j0; j < j1; j++) {
      const uint8_t* m = mask + j * size;
      uint16_t* p = buf_ + (y0 + j) * w + x0;
      for (int i = i0; i < i1; i++) {
        int a = m[i];
        if (a == 255) p[i] = solid;
        else if (a) p[i] = swap16(blend565i(swap16(p[i]), c, a + (a >> 7)));
      }
    }
  }

  // --- Fixed point (1/16 px) ---

  // Vertical span covering [y0, y1) in column x, anti-aliased ends.
  void vspan16(int x, int32_t y0, int32_t y1, uint16_t c) {
    if ((unsigned)x >= (unsigned)w) return;
    const int32_t H16 = h * 16;
    if (y0 < 0) y0 = 0;
    if (y1 > H16) y1 = H16;
    if (y1 <= y0) return;
    int a = y0 >> 4, b = y1 >> 4;
    if (a == b) {
      blend(x, a, c, (y1 - y0) << 4);
      return;
    }
    blend(x, a, c, ((a + 1) * 16 - y0) << 4);
    uint16_t v = swap16(c);
    uint16_t* p = buf_ + (a + 1) * w + x;
    for (int y = a + 1; y < b; y++, p += w) *p = v;
    if (b < h) blend(x, b, c, (y1 - b * 16) << 4);
  }

  // Filled anti-aliased circle. Edge coverage is approximated from the
  // squared distance, so there is no square root per pixel.
  void circle16(int32_t cx, int32_t cy, int32_t r, uint16_t c) {
    if (r <= 0) return;
    const int32_t ro = r + 8, ri = r - 8;  // +-0.5 px
    const int32_t ro2 = ro * ro;
    const int32_t ri2 = ri > 0 ? ri * ri : 0;
    const int32_t inv = (256 << 16) / (ro2 - ri2);
    int y0 = floor16(cy - ro), y1 = ceil16(cy + ro);
    if (y0 < 0) y0 = 0;
    if (y1 > h - 1) y1 = h - 1;
    for (int y = y0; y <= y1; y++) {
      int32_t dy = y * 16 + 8 - cy;
      int32_t dy2 = dy * dy;
      if (dy2 >= ro2) continue;
      int32_t xo = (int32_t)isqrt((uint32_t)(ro2 - dy2));
      int left = floor16(cx - xo - 8), right = ceil16(cx + xo - 8);
      int fullL, fullR;
      if (dy2 < ri2) {
        int32_t xi = (int32_t)isqrt((uint32_t)(ri2 - dy2));
        fullL = ceil16(cx - xi - 8);
        fullR = floor16(cx + xi - 8);
      } else {
        fullL = floor16(cx) + 1;
        fullR = fullL - 1;
      }
      if (fullR >= fullL) hspan(y, fullL, fullR, c);
      int edgeR = fullR >= fullL ? fullR + 1 : fullL;
      for (int x = left; x < fullL && x <= right; x++) edge16(x, y, cx, dy2, ro2, ri2, inv, c);
      for (int x = edgeR; x <= right; x++) edge16(x, y, cx, dy2, ro2, ri2, inv, c);
    }
  }

  // --- Float (one-off drawing: iris cache, effects) ---

  void vspanAA(int x, float y0, float y1, uint16_t c) { vspan16(x, x16(y0), x16(y1), c); }

  void hspanAA(int y, float x0, float x1, uint16_t c) {
    if ((unsigned)y >= (unsigned)h) return;
    x0 = clampf(x0, 0, (float)w);
    x1 = clampf(x1, 0, (float)w);
    if (x1 <= x0) return;
    int a = (int)floorf(x0), b = (int)floorf(x1);
    if (a == b) {
      blend(a, y, c, alpha256(x1 - x0));
      return;
    }
    blend(a, y, c, alpha256(a + 1 - x0));
    hspan(y, a + 1, b - 1, c);
    if (b < w) blend(b, y, c, alpha256(x1 - b));
  }

  void circleAA(float cx, float cy, float r, uint16_t c) { circle16(x16(cx), x16(cy), x16(r), c); }

  // Anti-aliased thick line with round caps; hw = half width.
  void lineAA(float x0, float y0, float x1, float y1, float hw, uint16_t c) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0.01f) {
      float ux = dx / len, uy = dy / len;
      if (fabsf(dx) >= fabsf(dy)) {
        // Mostly horizontal: vertical spans per column.
        float m = dy / dx, hv = hw * len / fabsf(dx);
        // The thick line's corners reach hw*|uy| beyond the end points.
        float ext = hw * fabsf(uy);
        int xa = (int)floorf(fminf(x0, x1) - ext), xb = (int)ceilf(fmaxf(x0, x1) + ext);
        for (int x = xa; x <= xb; x++) {
          float xc = x + 0.5f;
          float yl = y0 + m * (xc - x0);
          float a = yl - hv, b = yl + hv;
          // Keep the part between the two ends (caps cover the rest).
          if (fabsf(uy) > 1e-4f) {
            float yA = y0 - (xc - x0) * ux / uy;
            float yB = y0 + (len - (xc - x0) * ux) / uy;
            a = fmaxf(a, fminf(yA, yB));
            b = fminf(b, fmaxf(yA, yB));
          } else {
            float t = (xc - x0) * ux;
            if (t < 0 || t > len) continue;
          }
          vspanAA(x, a, b, c);
        }
      } else {
        // Mostly vertical: horizontal spans per row.
        float m = dx / dy, hh = hw * len / fabsf(dy);
        float ext = hw * fabsf(ux);
        int ya = (int)floorf(fminf(y0, y1) - ext), yb = (int)ceilf(fmaxf(y0, y1) + ext);
        for (int y = ya; y <= yb; y++) {
          float yc = y + 0.5f;
          float xl = x0 + m * (yc - y0);
          float a = xl - hh, b = xl + hh;
          if (fabsf(ux) > 1e-4f) {
            float xA = x0 - (yc - y0) * uy / ux;
            float xB = x0 + (len - (yc - y0) * uy) / ux;
            a = fmaxf(a, fminf(xA, xB));
            b = fminf(b, fmaxf(xA, xB));
          } else {
            float t = (yc - y0) * uy;
            if (t < 0 || t > len) continue;
          }
          hspanAA(y, a, b, c);
        }
      }
    }
    circleAA(x0, y0, hw, c);
    circleAA(x1, y1, hw, c);
  }

private:
  void edge16(int x, int y, int32_t cx, int32_t dy2, int32_t ro2, int32_t ri2, int32_t inv,
              uint16_t c) {
    int32_t dx = x * 16 + 8 - cx;
    int32_t d2 = dx * dx + dy2;
    if (d2 >= ro2) return;
    int a = d2 <= ri2 ? 256 : (int)(((int64_t)(ro2 - d2) * inv) >> 16);
    blend(x, y, c, a);
  }

  uint16_t* buf_;
};

}  // namespace

// --- Setup -----------------------------------------------------------------

bool EyeRenderer::begin(int screenSize) {
  size_ = screenSize > kMaxSize ? kMaxSize : screenSize;
  const float c = size_ * 0.5f;
  const float R = size_ * 0.5f;

  for (int side = 0; side < 2; side++) {
    for (int x = 0; x < size_; x++) {
      float u = (x + 0.5f - c) / R;
      float ui = side ? -u : u;
      float bow = 1 - u * u;
      Column& col = cols_[side][x];
      col.ui = (int16_t)lroundf(ui * 4096);
      col.bow = (int16_t)lroundf(bow * 4096);
      col.lashW = (int16_t)x16(R * (kLashBase + kLashOuter * clampf(-ui, 0, 1) + 0.015f * bow));
      col.creaseOff = (int16_t)x16(R * kCreaseGap * (0.7f + 0.3f * bow));
      col.creaseW = fabsf(u) < 0.78f ? (int16_t)x16(R * 0.014f * (0.4f + 0.6f * bow)) : 0;
      col.lowW = (int16_t)x16(R * kLowerLash * (0.5f + 0.5f * bow));
    }
  }

  // Spiral dot masks. Dots overlap along the stroke, which makes the
  // anti-aliased edge a bit more opaque; the radius is reduced to compensate.
  const float hw = R * kIrisRadius * kSpiralSize * kSpiralWidth - 0.3f;
  dotSize_ = 2 * (int)ceilf(hw + 1) + 1;
  if (dotSize_ > kDotMax) dotSize_ = kDotMax;
  const int half = dotSize_ / 2;
  for (int sy = 0; sy < 4; sy++) {
    for (int sx = 0; sx < 4; sx++) {
      uint8_t* m = dotMask_[sy * 4 + sx];
      for (int j = 0; j < dotSize_; j++) {
        for (int i = 0; i < dotSize_; i++) {
          float dx = i + 0.5f - (half + sx * 0.25f);
          float dy = j + 0.5f - (half + sy * 0.25f);
          float a = clampf(hw + 0.5f - sqrtf(dx * dx + dy * dy), 0, 1);
          m[j * dotSize_ + i] = (uint8_t)(a * 255 + 0.5f);
        }
      }
    }
  }

  // Iris cache: big enough for the iris disc plus its anti-aliased edge.
  irisCacheSize_ = 2 * (int)ceilf(R * kIrisRadius) + 4;
  irisCache_.setColorDepth(16);
  irisCache_.setPsram(false);
  irisCacheValid_ = false;
  return irisCache_.createSprite(irisCacheSize_, irisCacheSize_) != nullptr;
}

void EyeRenderer::buildIrisCache(uint16_t base) {
  Canvas cv(irisCache_);
  const float c = irisCacheSize_ * 0.5f;  // center on a pixel boundary
  const float r = size_ * 0.5f * kIrisRadius;

  const uint16_t outline = darken565(base, 0.80f);
  const uint16_t dark    = darken565(base, 0.68f);
  const uint16_t bright  = lighten565(base, 0.12f);
  const uint16_t streak  = lighten565(base, 0.45f);

  cv.fill(scleraColor);

  // Outline (anti-aliased disc), then gradient rows inside it (the outline hides their edges).
  cv.circleAA(c, c, r, outline);
  const float ri = r - 3.5f;
  for (int y = 0; y < cv.h; y++) {
    float dy = y + 0.5f - c;
    if (dy * dy >= ri * ri) continue;
    float half = sqrtf(ri * ri - dy * dy);
    float t = (dy + ri) / (2 * ri);  // 0 at top, 1 at bottom
    uint16_t col = dark;
    if (t >= 0.42f) {
      float k = clampf((t - 0.42f) / 0.45f, 0, 1);
      col = blend565(dark, bright, k * k * (3 - 2 * k));
    }
    cv.hspan(y, (int)lroundf(c - half), (int)lroundf(c + half) - 1, col);
  }

  // Light radial streaks in the lower half. They start inside the smallest
  // pupil, which is drawn on top every frame.
  const float r0 = r * 0.30f;
  const int nStreaks = 15;
  for (int i = 0; i < nStreaks; i++) {
    float a = kPi * (0.10f + 0.80f * i / (nStreaks - 1));
    float r1 = ri * ((i & 1) ? 0.80f : 0.92f);
    float ca = cosf(a), sa = sinf(a);
    cv.lineAA(c + ca * r0, c + sa * r0, c + ca * r1, c + sa * r1, 0.8f, streak);
  }

  irisCacheColor_ = base;
  irisCacheValid_ = true;
}

// --- Per frame -------------------------------------------------------------

void EyeRenderer::prepare(const EyeFrame& f) {
  f_ = f;
  const float c = size_ * 0.5f;
  const float R = size_ * 0.5f;
  const float gx = clampf(f.gazeX, -1, 1);
  const float gy = clampf(f.gazeY, -1, 1);
  const LidShape& L = f.lid;

  // Lid edges: up(x) = a + b * ui(x) - d * bow(x), lo(x) = la - ld * bow(x).
  const int32_t a16  = x16(c - R * L.upper + R * kLidFollowUp * gy);
  const int32_t b16  = x16(R * kTiltAmount * L.upperTilt);
  const int32_t d16  = x16(R * kUpperCurveK * L.upperCurve);
  const int32_t la16 = x16(c + R * L.lower + R * kLidFollowLow * gy);
  const int32_t ld16 = x16(R * kLowerCurveK * L.lowerCurve);
  const int32_t close16 = x16(c + R * kCloseLine);
  const int32_t k256 = alpha256(f.openness);
  const Column* cols = cols_[f.isRightEye ? 1 : 0];

  for (int x = 0; x < size_; x++) {
    int32_t up = a16 + ((b16 * cols[x].ui) >> 12) - ((d16 * cols[x].bow) >> 12);
    int32_t lo = la16 - ((ld16 * cols[x].bow) >> 12);
    // Blink: both lids move towards the closing line.
    up16_[x] = close16 + (((up - close16) * k256) >> 8);
    lo16_[x] = close16 + (((lo - close16) * k256) >> 8);
  }
  showCrease_ = f.openness > 0.6f;

  ix_ = c + gx * R * kGazeRange;
  iy_ = c + gy * R * kGazeRange;
  ir_ = R * kIrisRadius;
  irisX_ = (int)lroundf(ix_);
  irisY_ = (int)lroundf(iy_);

  if (f.effect == Effect::None && (!irisCacheValid_ || irisCacheColor_ != f.irisColor)) {
    buildIrisCache(f.irisColor);
  }
  if (f.effect == Effect::Spiral) buildSpiral();
}

void EyeRenderer::buildSpiral() {
  // Archimedean spiral r = k * theta, rotating over time. Points are spaced
  // evenly along the curve; the angle advances by small rotations instead of
  // calling sin/cos for every point.
  const float size = ir_ * kSpiralSize;
  const float thetaMax = kSpiralTurns * 2 * kPi;
  const float k = size / thetaMax;
  const float angle = f_.timeMs * 0.006f;
  float c = cosf(angle), s = sinf(angle);
  float theta = 0;
  int n = 0;
  while (theta <= thetaMax && n < kSpiralMaxPoints) {
    float r = k * theta;
    spiralX4_[n] = (int16_t)lroundf((ix_ + r * c) * 4);
    spiralY4_[n] = (int16_t)lroundf((iy_ + r * s) * 4);
    n++;
    float d = kSpiralStep / (r > 2 ? r : 2);
    if (d > 0.4f) d = 0.4f;
    float cd = 1 - d * d * 0.5f;  // cos(d), sin(d) for small d
    float nc = c * cd - s * d;
    float ns = s * cd + c * d;
    c = nc;
    s = ns;
    if ((n & 31) == 0) {  // keep (c, s) on the unit circle
      float inv = 1 / sqrtf(c * c + s * s);
      c *= inv;
      s *= inv;
    }
    theta += d;
  }
  spiralCount_ = n;
}

void EyeRenderer::drawBand(LGFX_Sprite& s, int yOffset) const {
  const float oy = (float)yOffset;

  if (f_.effect == Effect::Closed) {
    drawClosed(s, oy);
    return;
  }

  Canvas(s).fill(scleraColor);

  // Skip the iris / effect when it is entirely outside this band.
  const float iy = iy_ - oy;
  if (iy + ir_ * 1.3f >= 0 && iy - ir_ * 1.3f <= s.height()) {
    switch (f_.effect) {
      case Effect::Hearts: {
        float pulse = 1.0f + 0.08f * sinf(f_.timeMs * (2 * kPi / 600.0f));
        drawHeart(s, ix_, iy, ir_ * 0.9f * pulse);
        break;
      }
      case Effect::Cross:
        drawCross(s, ix_, iy, ir_);
        break;
      case Effect::Spiral:
        drawSpiral(s, yOffset);
        break;
      default:
        drawIris(s, yOffset * 16);
        break;
    }
  }

  drawLids(s, yOffset * 16);
}

void EyeRenderer::drawIris(LGFX_Sprite& s, int oy16) const {
  Canvas cv(s);
  const int half = irisCacheSize_ / 2;
  cv.blit(static_cast<const uint16_t*>(irisCache_.getBuffer()), irisCacheSize_, irisCacheSize_,
          irisX_ - half, irisY_ - half - (oy16 >> 4));

  // Pupil (size changes every frame) and catchlights, centered like the cached iris.
  const uint16_t base = f_.irisColor;
  const int32_t cx = irisX_ * 16, cy = irisY_ * 16 - oy16;
  const int32_t r16 = x16(ir_);
  const int32_t pr = x16(ir_ * (0.30f + 0.22f * clampf(f_.pupil, 0, 1)));
  cv.circle16(cx, cy, pr, darken565(base, 0.85f));
  cv.circle16(cx, cy, r16 * 15 / 100, 0xFFFF);
  cv.circle16(cx + r16 * 42 / 100, cy - r16 * 40 / 100, r16 * 7 / 100, lighten565(base, 0.85f));
}

void EyeRenderer::drawHeart(LGFX_Sprite& s, float cx, float cy, float size) const {
  Canvas cv(s);
  // Two circles on top + a triangle pointing down.
  float lobeR = size * 0.52f;
  float lobeY = cy - size * 0.22f;
  s.fillTriangle((int)(cx - size * 0.98f), (int)(lobeY + size * 0.12f),
                 (int)(cx + size * 0.98f), (int)(lobeY + size * 0.12f),
                 (int)cx, (int)(cy + size * 1.0f), heartColor);
  cv.circleAA(cx - size * 0.48f, lobeY, lobeR, heartColor);
  cv.circleAA(cx + size * 0.48f, lobeY, lobeR, heartColor);
  cv.circleAA(cx - size * 0.48f, lobeY - size * 0.12f, size * 0.14f, 0xFFFF);  // shine
}

void EyeRenderer::drawCross(LGFX_Sprite& s, float cx, float cy, float size) const {
  Canvas cv(s);
  float d = size * 0.75f;
  float w = size * 0.16f;
  cv.lineAA(cx - d, cy - d, cx + d, cy + d, w, lashColor);
  cv.lineAA(cx - d, cy + d, cx + d, cy - d, w, lashColor);
}

void EyeRenderer::drawSpiral(LGFX_Sprite& s, int oy) const {
  Canvas cv(s);
  const int half4 = (dotSize_ / 2) * 4;
  const int oy4 = oy * 4;
  const int minY4 = -dotSize_ * 4, maxY4 = (cv.h + dotSize_) * 4;
  for (int p = 0; p < spiralCount_; p++) {
    const int y4 = spiralY4_[p] - oy4;
    if (y4 < minY4 || y4 > maxY4) continue;  // outside this band
    const int ox = spiralX4_[p] - half4, oyy = y4 - half4;
    const int x0 = ox >> 2, y0 = oyy >> 2;    // floor
    cv.stamp(x0, y0, dotMask_[(oyy & 3) * 4 + (ox & 3)], dotSize_, lashColor);
  }
}

void EyeRenderer::drawClosed(LGFX_Sprite& s, float oy) const {
  Canvas cv(s);
  const float c = size_ * 0.5f;
  const float R = size_ * 0.5f;
  cv.fill(f_.lidColor);
  // A relaxed lash curve, lower in the middle, slightly thicker at the outer end.
  const int steps = 20;
  float px = 0, py = 0;
  for (int i = 0; i <= steps; i++) {
    float u = -0.72f + 1.44f * i / steps;
    float ui = f_.isRightEye ? -u : u;  // +1 = inner corner
    float x = c + u * R;
    float y = c + R * (0.02f + 0.20f * (1 - u * u / 0.52f)) - oy;
    float w = R * (0.035f + 0.02f * clampf(-ui, 0, 1));
    if (i > 0) cv.lineAA(px, py, x, y, w, lashColor);
    px = x;
    py = y;
  }
}

void EyeRenderer::drawLids(LGFX_Sprite& s, int oy16) const {
  Canvas cv(s);
  const int32_t H16 = cv.h * 16;
  const uint16_t skin      = f_.lidColor;
  const uint16_t crease    = darken565(skin, 0.28f);
  const uint16_t lowerLash = blend565(skin, lashColor, 0.55f);
  const Column* cols = cols_[f_.isRightEye ? 1 : 0];

  for (int x = 0; x < cv.w; x++) {
    const Column& col = cols[x];
    const int32_t up = up16_[x] - oy16;  // band coordinates
    const int32_t lo = lo16_[x] - oy16;

    if (up >= lo) {
      // Closed (or lids overlap): skin with the lash line where they meet.
      int32_t mid = (up + lo) / 2;
      cv.vspan16(x, 0, H16, skin);
      cv.vspan16(x, mid - col.lashW * 6 / 10, mid + col.lashW * 4 / 10, lashColor);
      continue;
    }

    // Upper lid: skin, crease line, lash line.
    const int32_t lashTop = up - col.lashW;
    cv.vspan16(x, 0, lashTop, skin);
    cv.vspan16(x, lashTop, up, lashColor);
    if (showCrease_ && col.creaseW > 0) {
      int32_t cy = lashTop - col.creaseOff;
      if (cy + oy16 > 16) cv.vspan16(x, cy - col.creaseW, cy + col.creaseW, crease);
    }

    // Lower lid: thin lash line, then skin.
    cv.vspan16(x, lo, lo + col.lowW, lowerLash);
    cv.vspan16(x, lo + col.lowW, H16, skin);
  }
}
