#include "commands.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace {

constexpr int kMaxLine = 96;
constexpr int kMaxArgs = 4;

bool parseOnOff(const char* s, bool& out) {
  if (!s) return false;
  if (!strcasecmp(s, "on") || !strcmp(s, "1") || !strcasecmp(s, "true")) { out = true; return true; }
  if (!strcasecmp(s, "off") || !strcmp(s, "0") || !strcasecmp(s, "false")) { out = false; return true; }
  return false;
}

bool parseFloat(const char* s, float& out) {
  if (!s) return false;
  char* end = nullptr;
  out = strtof(s, &end);
  return end != s && *end == '\0';
}

bool parseHexColor(const char* s, uint16_t& out) {
  if (!s) return false;
  if (*s == '#') s++;
  if (strlen(s) != 6) return false;
  char* end = nullptr;
  unsigned long v = strtoul(s, &end, 16);
  if (*end != '\0') return false;
  uint8_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
  out = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  return true;
}

void hexFromColor(uint16_t c, char* buf) {
  uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
  uint8_t g = ((c >> 5) & 0x3F) * 255 / 63;
  uint8_t b = (c & 0x1F) * 255 / 31;
  snprintf(buf, 7, "%02X%02X%02X", r, g, b);
}

}  // namespace

bool handleCommand(EyeAnimator& eyes, const char* line, char* reply, size_t replyLen) {
  char buf[kMaxLine];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char* argv[kMaxArgs + 1] = {};
  int argc = 0;
  char* save = nullptr;
  for (char* tok = strtok_r(buf, " \t\r\n", &save); tok && argc <= kMaxArgs;
       tok = strtok_r(nullptr, " \t\r\n", &save)) {
    argv[argc++] = tok;
  }
  if (argc == 0) {
    snprintf(reply, replyLen, "ERR empty");
    return false;
  }

  const char* cmd = argv[0];
  const char* a1 = argc > 1 ? argv[1] : nullptr;
  const char* a2 = argc > 2 ? argv[2] : nullptr;
  const char* a3 = argc > 3 ? argv[3] : nullptr;

  if (!strcasecmp(cmd, "emo")) {
    Emotion e;
    if (!a1 || !parseEmotion(a1, e)) {
      snprintf(reply, replyLen, "ERR usage: emo neutral|happy|sad|angry|surprised|sleepy");
      return false;
    }
    eyes.setEmotion(e);
    snprintf(reply, replyLen, "OK emo %s", emotionName(e));
    return true;
  }

  if (!strcasecmp(cmd, "fx")) {
    Effect e;
    if (!a1 || !parseEffect(a1, e)) {
      snprintf(reply, replyLen, "ERR usage: fx none|hearts|cross|spiral|closed|fire");
      return false;
    }
    eyes.setEffect(e);
    snprintf(reply, replyLen, "OK fx %s", effectName(e));
    return true;
  }

  if (!strcasecmp(cmd, "look")) {
    float x, y, hold = 3000;
    if (!parseFloat(a1, x) || !parseFloat(a2, y) || (a3 && !parseFloat(a3, hold)) || hold < 0) {
      snprintf(reply, replyLen, "ERR usage: look <x> <y> [holdMs]");
      return false;
    }
    x = x < -1 ? -1 : (x > 1 ? 1 : x);
    y = y < -1 ? -1 : (y > 1 ? 1 : y);
    eyes.lookAt(x, y, (uint32_t)hold);
    snprintf(reply, replyLen, "OK look %.2f %.2f", x, y);
    return true;
  }

  if (!strcasecmp(cmd, "center")) {
    eyes.lookAt(0, 0);
    snprintf(reply, replyLen, "OK center");
    return true;
  }

  if (!strcasecmp(cmd, "auto") || !strcasecmp(cmd, "autoblink")) {
    bool on;
    if (!parseOnOff(a1, on)) {
      snprintf(reply, replyLen, "ERR usage: %s on|off", cmd);
      return false;
    }
    if (!strcasecmp(cmd, "auto")) eyes.setAutoGaze(on);
    else eyes.setAutoBlink(on);
    snprintf(reply, replyLen, "OK %s %s", cmd, on ? "on" : "off");
    return true;
  }

  if (!strcasecmp(cmd, "blink")) {
    eyes.blink();
    snprintf(reply, replyLen, "OK blink");
    return true;
  }

  if (!strcasecmp(cmd, "wink")) {
    int eye = -1;
    if (a1 && (!strcasecmp(a1, "l") || !strcasecmp(a1, "left"))) eye = EyeAnimator::kLeft;
    if (a1 && (!strcasecmp(a1, "r") || !strcasecmp(a1, "right"))) eye = EyeAnimator::kRight;
    if (eye < 0) {
      snprintf(reply, replyLen, "ERR usage: wink l|r");
      return false;
    }
    eyes.wink(eye);
    snprintf(reply, replyLen, "OK wink %s", eye == EyeAnimator::kLeft ? "l" : "r");
    return true;
  }

  if (!strcasecmp(cmd, "color") || !strcasecmp(cmd, "lid")) {
    uint16_t c;
    if (!parseHexColor(a1, c)) {
      snprintf(reply, replyLen, "ERR usage: %s RRGGBB", cmd);
      return false;
    }
    if (!strcasecmp(cmd, "color")) eyes.setIrisColor(c);
    else eyes.setLidColor(c);
    char hex[7];
    hexFromColor(c, hex);
    snprintf(reply, replyLen, "OK %s %s", cmd, hex);
    return true;
  }

  if (!strcasecmp(cmd, "status")) {
    char hex[7], lidHex[7];
    hexFromColor(eyes.irisColor(), hex);
    hexFromColor(eyes.lidColor(), lidHex);
    snprintf(reply, replyLen, "OK emo=%s fx=%s color=%s lid=%s auto=%s autoblink=%s",
             emotionName(eyes.emotion()), effectName(eyes.effect()), hex, lidHex,
             eyes.autoGaze() ? "on" : "off", eyes.autoBlink() ? "on" : "off");
    return true;
  }

  if (!strcasecmp(cmd, "help")) {
    snprintf(reply, replyLen,
             "OK commands: emo, fx, look, center, auto, autoblink, blink, wink, color, lid, status");
    return true;
  }

  snprintf(reply, replyLen, "ERR unknown command '%s' (try: help)", cmd);
  return false;
}
