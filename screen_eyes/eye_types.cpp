#include "eye_types.h"

#include <cstring>
#include <strings.h>

static const char* const kEmotionNames[] = {
  "neutral", "happy", "sad", "angry", "surprised", "sleepy"
};
static const char* const kEffectNames[] = {
  "none", "hearts", "cross", "spiral", "closed"
};

static_assert(sizeof(kEmotionNames) / sizeof(kEmotionNames[0]) == (int)Emotion::Count,
              "emotion names out of sync");
static_assert(sizeof(kEffectNames) / sizeof(kEffectNames[0]) == (int)Effect::Count,
              "effect names out of sync");

const char* emotionName(Emotion e) {
  return e < Emotion::Count ? kEmotionNames[(int)e] : "?";
}

const char* effectName(Effect e) {
  return e < Effect::Count ? kEffectNames[(int)e] : "?";
}

bool parseEmotion(const char* name, Emotion& out) {
  for (int i = 0; i < (int)Emotion::Count; i++) {
    if (strcasecmp(name, kEmotionNames[i]) == 0) {
      out = (Emotion)i;
      return true;
    }
  }
  return false;
}

bool parseEffect(const char* name, Effect& out) {
  for (int i = 0; i < (int)Effect::Count; i++) {
    if (strcasecmp(name, kEffectNames[i]) == 0) {
      out = (Effect)i;
      return true;
    }
  }
  return false;
}
