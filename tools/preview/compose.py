"""Compose preview PPM images into a gallery PNG and an animated GIF.

Usage:
  python3 compose.py gallery <ppm_dir> <out.png>
  python3 compose.py anim <ppm_dir> <out.gif>

Each PPM holds two eyes side by side (480x240). The round display is
simulated by masking each eye to a circle on a dark bezel.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

EYE = 240
GAP = 24
BEZEL = (40, 40, 44)
BG = (24, 24, 28)


def round_pair(img):
    """Return the eye pair with each eye masked to a circle."""
    w = EYE * 2 + GAP
    out = Image.new("RGB", (w, EYE), BG)
    mask = Image.new("L", (EYE, EYE), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, EYE - 1, EYE - 1), fill=255)
    for i in range(2):
        eye = img.crop((i * EYE, 0, (i + 1) * EYE, EYE))
        x = i * (EYE + GAP)
        ImageDraw.Draw(out).ellipse((x - 6, -6, x + EYE + 5, EYE + 5), fill=BEZEL)
        out.paste(eye, (x, 0), mask)
    return out


def gallery(src, dst):
    files = sorted(Path(src).glob("*.ppm"), key=lambda p: p.stat().st_mtime)
    cols = 2
    pad = 20
    label_h = 30
    pair_w = EYE * 2 + GAP
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (pair_w + pad) + pad, rows * (EYE + label_h + pad) + pad), BG)
    draw = ImageDraw.Draw(sheet)
    for i, f in enumerate(files):
        r, c = divmod(i, cols)
        x = pad + c * (pair_w + pad)
        y = pad + r * (EYE + label_h + pad)
        draw.text((x, y), f.stem, fill=(220, 220, 220), font_size=20)
        sheet.paste(round_pair(Image.open(f)), (x, y + label_h))
    sheet.save(dst)


def anim(src, dst):
    files = sorted(Path(src).glob("frame_*.ppm"))
    frames = [round_pair(Image.open(f)).resize(((EYE * 2 + GAP) // 2, EYE // 2), Image.LANCZOS)
              for f in files]
    frames[0].save(dst, save_all=True, append_images=frames[1:], duration=50, loop=0, optimize=True)


if __name__ == "__main__":
    mode, src, dst = sys.argv[1:4]
    {"gallery": gallery, "anim": anim}[mode](src, dst)
