// Desktop preview: renders the firmware's eyes on a PC (no hardware needed).
//
// Uses the same EyeRenderer / EyeAnimator / command parser as the firmware,
// with LovyanGFX compiled natively. Writes raw PPM images that compose.py
// turns into a gallery PNG and an animated GIF.
//
//   preview gallery <outdir>   one still image per emotion / effect
//   preview anim <outdir>      a scripted animation, one image per frame

#include <LovyanGFX.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "commands.h"
#include "eye_animator.h"
#include "eye_renderer.h"

namespace {

constexpr int kSize = 240;
constexpr int kBand = kSize / 2;

LGFX_Sprite gSprite;
EyeRenderer gRenderer;

// Renders in two bands, like the firmware does.
void appendEye(std::vector<uint8_t>& rgb, int stride, int x0, const EyeFrame& f) {
  gRenderer.prepare(f);
  for (int band = 0; band < kSize; band += kBand) {
  gRenderer.drawBand(gSprite, band);
  for (int y = 0; y < kBand; y++) {
    for (int x = 0; x < kSize; x++) {
      uint16_t c = gSprite.readPixel(x, y);
      uint8_t* p = &rgb[((band + y) * stride + x0 + x) * 3];
      p[0] = ((c >> 11) & 0x1F) * 255 / 31;
      p[1] = ((c >> 5) & 0x3F) * 255 / 63;
      p[2] = (c & 0x1F) * 255 / 31;
    }
  }
  }
}

// Writes the left and right eye side by side (480x240).
bool writePair(const std::string& path, const EyeAnimator& eyes) {
  const int W = kSize * 2;
  std::vector<uint8_t> rgb(W * kSize * 3);
  appendEye(rgb, W, 0, eyes.frame(EyeAnimator::kLeft));
  appendEye(rgb, W, kSize, eyes.frame(EyeAnimator::kRight));
  FILE* fp = fopen(path.c_str(), "wb");
  if (!fp) return false;
  fprintf(fp, "P6\n%d %d\n255\n", W, kSize);
  fwrite(rgb.data(), 1, rgb.size(), fp);
  fclose(fp);
  return true;
}

void run(EyeAnimator& eyes, const char* cmd) {
  char reply[128];
  handleCommand(eyes, cmd, reply, sizeof(reply));
  if (reply[0] != 'O') fprintf(stderr, "%s -> %s\n", cmd, reply);
}

int gallery(const std::string& dir) {
  struct Shot { const char* name; const char* cmds[3]; };
  const Shot shots[] = {
    {"neutral",   {"emo neutral", "look 0.0 0.0"}},
    {"look_side", {"emo neutral", "look 0.8 -0.2"}},
    {"happy",     {"emo happy", "look 0 0"}},
    {"sad",       {"emo sad", "look -0.2 0"}},
    {"angry",     {"emo angry", "look 0 0"}},
    {"surprised", {"emo surprised", "look 0 0"}},
    {"sleepy",    {"emo sleepy", "look 0 0"}},
    {"hearts",    {"fx hearts", "look 0 0"}},
    {"cross",     {"fx cross", "look 0 0"}},
    {"spiral",    {"fx spiral", "look 0 0"}},
    {"closed",    {"fx closed"}},
    {"fire",      {"fx fire", "look 0 0"}},
    {"fire_angry", {"fx fire", "emo angry", "look 0.4 0"}},
    {"colors",    {"color 3080E0", "lid C88A64", "look -0.5 0.3"}},
  };
  for (const Shot& s : shots) {
    EyeAnimator eyes(42);
    eyes.setAutoBlink(false);
    eyes.setAutoGaze(false);
    for (const char* c : s.cmds) if (c) run(eyes, c);
    for (uint32_t t = 0; t <= 2000; t += 20) {  // let transitions (and fire) settle
      eyes.update(t);
      gRenderer.prepare(eyes.frame(EyeAnimator::kLeft));
      gRenderer.prepare(eyes.frame(EyeAnimator::kRight));
    }
    if (!writePair(dir + "/" + s.name + ".ppm", eyes)) return 1;
    printf("%s\n", s.name);
  }
  return 0;
}

int anim(const std::string& dir) {
  struct Cue { uint32_t atMs; const char* cmd; };
  const Cue cues[] = {
    {0, "emo neutral"},
    {1800, "look 0.8 0 900"},
    {2700, "look -0.8 0 900"},
    {3600, "center"},
    {4000, "emo happy"},
    {5200, "wink r"},
    {6200, "emo angry"},
    {7400, "emo sad"},
    {8600, "emo surprised"},
    {9600, "blink"},
    {10400, "fx hearts"},
    {11800, "fx spiral"},
    {13200, "fx none"},
    {13200, "emo sleepy"},
    {15000, "fx closed"},
    {16000, "fx fire"},
    {16000, "emo neutral"},
    {17500, "look 0.6 0 1200"},
    {18700, "emo angry"},
    {20000, "fx none"},
  };
  const uint32_t kEnd = 21000, kStep = 50;  // 20 fps
  EyeAnimator eyes(7);
  size_t next = 0;
  int n = 0;
  char name[64];
  for (uint32_t t = 0; t <= kEnd; t += kStep) {
    while (next < sizeof(cues) / sizeof(cues[0]) && cues[next].atMs <= t) run(eyes, cues[next++].cmd);
    eyes.update(t);
    snprintf(name, sizeof(name), "/frame_%04d.ppm", n++);
    if (!writePair(dir + name, eyes)) return 1;
  }
  printf("%d frames\n", n);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s gallery|anim <outdir>\n", argv[0]);
    return 2;
  }
  gSprite.setColorDepth(16);
  if (!gSprite.createSprite(kSize, kBand)) return 1;
  if (!gRenderer.begin(kSize)) return 1;
  std::string mode = argv[1];
  if (mode == "gallery") return gallery(argv[2]);
  if (mode == "anim") return anim(argv[2]);
  return 2;
}
