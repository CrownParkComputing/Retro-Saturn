#!/usr/bin/env python3
"""The Retro-Saturn wordmark and launcher icons.

    python3 tool/make_saturn_art.py

The mark is a disc, because that is what the machine is: a ringed planet drawn
as a CD seen at an angle, which is the same shape twice and the reason the
console was called what it was. Amber on deep blue-grey -- the palette the
interface already uses, so the logo and the app it opens are recognisably one
thing rather than a logo applied to an app.

It shares nothing with the DOS family's artwork. That family is deliberately
one set, and being one set is what guideline 4.3 objected to.
"""

from __future__ import annotations

import math
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FONT_CANDIDATES = [
    "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
]
FONT = next((f for f in FONT_CANDIDATES if os.path.exists(f)), None)
if FONT is None:
    raise SystemExit("no bold sans found; looked for:\n  " + "\n  ".join(FONT_CANDIDATES))

SIZE = 1024

INK = (14, 16, 23)
PANEL = (24, 27, 37)
AMBER = (242, 163, 40)
AMBER_HI = (255, 206, 120)
AMBER_LO = (150, 96, 16)
TEXT = (230, 235, 245)


def background(size):
    """Deep blue-grey, lighter at the top, like the interface's own ground."""
    img = Image.new("RGB", (1, size))
    px = img.load()
    for y in range(size):
        t = y / max(1, size - 1)
        px[0, y] = tuple(round(a + (b - a) * t) for a, b in zip(PANEL, INK))
    return img.resize((size, size), Image.BILINEAR).convert("RGBA")


def disc(size):
    """A CD at an angle: an ellipse, a hole, and a ring around it.

    Drawn at four times the final size and shrunk, because a circle a pixel
    wide drawn directly has a staircase on it that no amount of colour choice
    hides."""
    s = size * 4
    layer = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)

    cx, cy = s / 2, s / 2
    rx, ry = s * 0.42, s * 0.26          # the angle: wider than it is tall

    # The disc itself, as concentric ellipses from dim at the rim to bright in
    # the middle -- a disc catches the light in bands, not evenly.
    steps = 64
    for i in range(steps):
        t = i / (steps - 1)
        k = 1.0 - t * 0.72
        col = tuple(round(a + (b - a) * (math.sin(t * math.pi) ** 2))
                    for a, b in zip(AMBER_LO, AMBER_HI))
        d.ellipse((cx - rx * k, cy - ry * k, cx + rx * k, cy + ry * k),
                  outline=col + (255,), width=max(1, int(s * 0.006)))

    # The hole.
    hx, hy = rx * 0.17, ry * 0.17
    d.ellipse((cx - hx, cy - hy, cx + hx, cy + hy), fill=(0, 0, 0, 0))
    d.ellipse((cx - hx, cy - hy, cx + hx, cy + hy),
              outline=AMBER + (255,), width=max(1, int(s * 0.004)))

    # The ring, the part that makes it Saturn rather than only a CD.
    for w, a in ((0.010, 255), (0.020, 90)):
        d.ellipse((cx - rx * 1.30, cy - ry * 1.30, cx + rx * 1.30, cy + ry * 1.30),
                  outline=AMBER + (a,), width=max(1, int(s * w)))

    layer = layer.resize((size, size), Image.LANCZOS)
    glow = layer.filter(ImageFilter.GaussianBlur(size * 0.012))
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(glow)
    out.alpha_composite(layer)
    return out


def icon():
    img = background(SIZE)
    art = disc(int(SIZE * 0.86))
    img.alpha_composite(art, ((SIZE - art.width) // 2, (SIZE - art.height) // 2))
    return img


def wordmark(height=180):
    """Wide and short, for the top of a window: the disc, then the name.

    The WIDTH is measured from the type, not guessed. Sizing this by eye is
    how the first version cut "RETRO SATURN" off after "SATU" -- the canvas was
    640 wide and the text needed more, and nothing in the code said so."""
    h = height
    mark = disc(int(h * 1.02))
    big = ImageFont.truetype(FONT, int(h * 0.40))
    small = ImageFont.truetype(FONT, int(h * 0.19))

    probe = ImageDraw.Draw(Image.new("RGBA", (4, 4)))
    gap = int(h * 0.08)
    w_retro = probe.textlength("RETRO", font=big)
    w_saturn = probe.textlength("SATURN", font=big)
    w_sub = probe.textlength("SEGA SATURN EMULATION", font=small)
    text_w = max(w_retro + gap + w_saturn, w_sub)

    pad = int(h * 0.10)
    width = int(mark.width + int(h * 0.05) + text_w + pad)

    img = Image.new("RGBA", (width, h), (0, 0, 0, 0))
    img.alpha_composite(mark, (0, (h - mark.height) // 2))

    x = mark.width + int(h * 0.05)
    d = ImageDraw.Draw(img)
    d.text((x, int(h * 0.20)), "RETRO", font=big, fill=TEXT + (255,))
    d.text((x + int(w_retro) + gap, int(h * 0.20)), "SATURN", font=big,
           fill=AMBER + (255,))
    d.text((x, int(h * 0.64)), "SEGA SATURN EMULATION", font=small,
           fill=(140, 150, 170, 255))
    return img


ANDROID = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def main():
    art = os.path.join(HERE, "frontend", "assets")
    os.makedirs(art, exist_ok=True)

    mark = wordmark()
    mark.save(os.path.join(art, "wordmark.png"))
    print("wordmark: %dx%d" % mark.size)

    master = icon()
    master.convert("RGB").save(os.path.join(art, "icon-1024.png"))

    res = os.path.join(HERE, "flutter_app", "android", "app", "src", "main", "res")
    if os.path.isdir(res):
        for density, px in ANDROID.items():
            folder = os.path.join(res, "mipmap-%s" % density)
            os.makedirs(folder, exist_ok=True)
            master.resize((px, px), Image.LANCZOS).convert("RGB").save(
                os.path.join(folder, "ic_launcher.png"))
        print("android launcher icons: %d densities" % len(ANDROID))
    else:
        print("no android res/ here -- wrote the 1024 master only")


if __name__ == "__main__":
    main()
