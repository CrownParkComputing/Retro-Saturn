#!/usr/bin/env python3
"""Interface artwork for Retro-Saturn, cut from the logo we already ship.

    python3 tool/make_saturn_art.py

THE LOGO IS NOT INVENTED HERE. Retro-Saturn already has one -- the Retro script,
SATURN beneath it, and the blue Saturn swirl -- and it is on the Play listing,
the launcher icon and the feature graphic. This takes that artwork and lays it
out for the top of a window; it does not draw a second logo that would then
disagree with the first.

(The iOS application is a different matter. That one must NOT look like this
family, because the family look is exactly what guideline 4.3 objected to, and
it has its own mark.)

Source: store/play/feature-graphic.png, which holds the mark at the largest
size we have it.
"""

from __future__ import annotations

import os
from PIL import Image

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(HERE, "store", "play", "feature-graphic.png")
ICON = os.path.join(HERE, "store", "play", "icon-512.png")


def content_box(img, threshold=260, pad=12):
    """Where the artwork actually is.

    The feature graphic is mostly dark gradient with the mark bright in the
    middle of it, so brightness bounds the mark. Measured rather than typed in:
    a hand-written crop is wrong the moment the graphic is redrawn, and wrong
    quietly.
    """
    px = img.convert("RGB").load()
    w, h = img.size
    minx, miny, maxx, maxy = w, h, 0, 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            r, g, b = px[x, y]
            if r + g + b > threshold:
                minx = min(minx, x); maxx = max(maxx, x)
                miny = min(miny, y); maxy = max(maxy, y)
    if minx > maxx or miny > maxy:
        return (0, 0, w, h)
    return (max(0, minx - pad), max(0, miny - pad),
            min(w, maxx + pad), min(h, maxy + pad))


def wordmark(height=200):
    src = Image.open(SOURCE).convert("RGB")
    cut = src.crop(content_box(src))
    w = round(cut.width * height / cut.height)
    return cut.resize((w, height), Image.LANCZOS)


ANDROID = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def main():
    if not os.path.exists(SOURCE):
        raise SystemExit("no logo at %s" % SOURCE)

    art = os.path.join(HERE, "frontend", "assets")
    os.makedirs(art, exist_ok=True)

    mark = wordmark()
    mark.save(os.path.join(art, "wordmark.png"))
    print("wordmark: %dx%d  (from the shipped feature graphic)" % mark.size)

    # The launcher icon is already right and already in the tree; it is copied
    # rather than regenerated, so there is one master and not two.
    if os.path.exists(ICON):
        icon = Image.open(ICON).convert("RGBA")
        icon.save(os.path.join(art, "icon-512.png"))
        res = os.path.join(HERE, "flutter_app", "android", "app", "src", "main", "res")
        if os.path.isdir(res):
            for density, px in ANDROID.items():
                folder = os.path.join(res, "mipmap-%s" % density)
                os.makedirs(folder, exist_ok=True)
                icon.resize((px, px), Image.LANCZOS).convert("RGB").save(
                    os.path.join(folder, "ic_launcher.png"))
            print("android launcher icons: %d densities, from icon-512" % len(ANDROID))


if __name__ == "__main__":
    main()
