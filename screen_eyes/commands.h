#pragma once

#include <cstddef>
#include "eye_animator.h"

// Text command protocol, shared by Serial and (later) Bluetooth.
// One command per line, words separated by spaces, case-insensitive:
//
//   emo <neutral|happy|sad|angry|surprised|sleepy>
//   fx <none|hearts|cross|spiral|closed>
//   look <x> <y> [holdMs]   x, y in -1..1 (x: left..right, y: up..down)
//   center                  look straight ahead
//   auto <on|off>           idle gaze movement
//   autoblink <on|off>      automatic blinking
//   blink
//   wink <l|r>
//   color <RRGGBB>          iris color, hex
//   lid <RRGGBB>            eyelid (skin) color, hex
//   status
//   help
//
// Every command produces a one-line reply starting with "OK" or "ERR".
bool handleCommand(EyeAnimator& eyes, const char* line, char* reply, size_t replyLen);
