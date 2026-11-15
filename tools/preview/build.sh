#!/usr/bin/env bash
# Builds the desktop preview and renders gallery.png + anim.gif into ./out.
# Requires: g++, python3 with Pillow, LovyanGFX installed via Arduino IDE.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
FW="${FW_DIR:-$HERE/../../screen_eyes}"
LGFX="${LGFX_DIR:-$HOME/projects/libraries/LovyanGFX/src}"
BUILD="$HERE/build"
OUT="${OUT_DIR:-$HERE/out}"
mkdir -p "$BUILD/lgfx" "$BUILD/gallery" "$BUILD/anim" "$OUT"

# LovyanGFX is compiled once and cached.
if [ ! -f "$BUILD/lgfx/liblgfx.a" ]; then
  echo "Building LovyanGFX (one time)..."
  i=0
  while IFS= read -r f; do
    i=$((i + 1))
    gcc -O1 -c -I"$LGFX" "$f" -o "$BUILD/lgfx/c${i}_$(basename "$f").o"
  done < <(find "$LGFX/lgfx" -name '*.c')
  for f in "$LGFX"/lgfx/v1/*.cpp; do
    g++ -std=c++17 -O2 -DLGFX_LINUX_FB -c -I"$LGFX" "$f" -o "$BUILD/lgfx/$(basename "$f").o"
  done
  ar rcs "$BUILD/lgfx/liblgfx.a" "$BUILD"/lgfx/*.o
fi

g++ -std=c++17 -O2 -Wall -DLGFX_LINUX_FB -I"$LGFX" -I"$FW" \
  "$HERE/preview.cpp" "$FW"/eye_renderer.cpp "$FW"/eye_animator.cpp "$FW"/eye_types.cpp "$FW"/commands.cpp \
  "$BUILD/lgfx/liblgfx.a" -lpthread -o "$BUILD/preview"

rm -f "$BUILD"/gallery/*.ppm "$BUILD"/anim/*.ppm
"$BUILD/preview" gallery "$BUILD/gallery" > /dev/null
"$BUILD/preview" anim "$BUILD/anim"
python3 -I "$HERE/compose.py" gallery "$BUILD/gallery" "$OUT/gallery.png"
python3 -I "$HERE/compose.py" anim "$BUILD/anim" "$OUT/anim.gif"
echo "Done: $OUT/gallery.png, $OUT/anim.gif"
