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

# The hardware shots. Masters live in store/art/ and are cut down here; the
# originals are ~1.8MB each and there is no reason to ship that to draw a
# thumbnail with.
HARDWARE = {
    "console.png":       640,   # the drive panel: this is the machine itself
    "control-pad.png":   320,   # the ports strip, one per socket
    "analog-pad.png":    320,
    "arcade-racer.png":  320,
    "mission-stick.png": 320,
    "virtua-gun.png":    320,
    "shuttle-mouse.png": 320,
}

PANEL_BG = (14, 16, 23)       # matches SDL_SetRenderDrawColor in saturn_app.cpp


def on_panel(img):
    """Lift the backdrop to the panel colour so the tile has no visible edge.

    These are product shots on a black vignette. Dropped straight onto the
    interface they read as photographs pasted on, with a hard rectangle where
    the black meets the panel. Taking a component-wise maximum against the
    panel colour only touches pixels darker than the panel -- which is the
    vignette and nothing else, since the hardware's own shadows are lit -- so
    the corners dissolve into the page and the product is untouched.
    """
    px = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y][:3]
            px[x, y] = (max(r, PANEL_BG[0]), max(g, PANEL_BG[1]), max(b, PANEL_BG[2]))
    return img


def flatten(img):
    """One opaque RGB image, whatever the master arrived as.

    Some of these come with real alpha and some come with the transparency
    CHECKERBOARD painted into them -- an editor's backdrop saved as picture
    data. Both mean "there is nothing here", and both have to end up as panel
    colour or the tile shows a grey chessboard on the interface.
    """
    if "A" in img.getbands():
        flat = Image.new("RGB", img.size, PANEL_BG)
        flat.paste(img.convert("RGBA"), mask=img.convert("RGBA").split()[-1])
        return flat
    return img.convert("RGB")


def dekey(img, light=180, grey=24):
    """Remove a painted-in checkerboard by flooding from the edges.

    Keying every light, colourless pixel would also take the white lettering
    on the hardware -- MISSION STICK, SEGA SATURN, the axis diagrams -- and
    punch holes through the middle of the product. The backdrop is the part
    that is CONNECTED TO THE EDGE of the frame; the lettering is enclosed by
    black plastic and is never reached. So this floods inward from the border
    and stops where the hardware starts.

    A master with a dark backdrop has no light pixels at its corners, the
    flood begins nowhere and this does nothing, which is why it is safe to
    run over all of them.
    """
    from collections import deque

    w, h = img.size
    px = img.load()

    def is_backdrop(x, y):
        r, g, b = px[x, y]
        return min(r, g, b) > light and max(r, g, b) - min(r, g, b) < grey

    seen = bytearray(w * h)
    q = deque()
    for x in range(w):
        for y in (0, h - 1):
            if not seen[y * w + x] and is_backdrop(x, y):
                seen[y * w + x] = 1; q.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if not seen[y * w + x] and is_backdrop(x, y):
                seen[y * w + x] = 1; q.append((x, y))

    n = 0
    while q:
        x, y = q.popleft()
        px[x, y] = PANEL_BG
        n += 1
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx] \
                    and is_backdrop(nx, ny):
                seen[ny * w + nx] = 1
                q.append((nx, ny))
    return img, n


def hardware(art):
    for name, width in HARDWARE.items():
        src = os.path.join(HERE, "store", "art", name)
        if not os.path.exists(src):
            print("hardware: no %s, skipped" % name)
            continue
        im = flatten(Image.open(src))
        im, keyed = dekey(im)
        if keyed:
            print("hardware: %s had a painted-in backdrop, %d px keyed out"
                  % (name, keyed))
        im = im.crop(content_box(im, threshold=150, pad=24))
        h = round(im.height * width / im.width)
        im = on_panel(im.resize((width, h), Image.LANCZOS))
        im.save(os.path.join(art, name))
        print("hardware: %s %dx%d" % (name, width, h))


def main():
    if not os.path.exists(SOURCE):
        raise SystemExit("no logo at %s" % SOURCE)

    art = os.path.join(HERE, "frontend", "assets")
    os.makedirs(art, exist_ok=True)

    mark = wordmark()
    mark.save(os.path.join(art, "wordmark.png"))
    print("wordmark: %dx%d  (from the shipped feature graphic)" % mark.size)

    hardware(art)

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
