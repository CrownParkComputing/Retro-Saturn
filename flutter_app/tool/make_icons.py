#!/usr/bin/env python3
"""Builds the launcher icon and every size Android and iOS ask for.

The same three-part icon as the Amiga and C64 front ends - the Retro script cut
from the Retro Recompilation logo, the machine's name under it, and the
machine's own mark below that - so the whole family sits together on a home
screen and reads as one set. What differs is the mark: an Amiga has its boot
tick, a C64 the screen it wakes up on, a Saturn its swirl.

    python3 tool/make_icons.py

Run from the flutter_app directory. Overwrites the mipmaps and the iOS icon
set.
"""

from __future__ import annotations

import math
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGO = os.path.join(HERE, "assets", "images", "retro_recomp_logo.png")
FONT = "/usr/share/fonts/liberation/LiberationSans-Bold.ttf"

SIZE = 1024

BG_TOP = (8, 12, 26)
BG_BOTTOM = (3, 4, 9)

# The wordmark's blue, top to bottom: white highlight into deep blue.
CHROME = [
    (232, 244, 255),
    (150, 205, 250),
    (56, 120, 220),
    (26, 60, 160),
    (120, 180, 240),
]

# The swirl, in the app's own accent blue (SaturnTheme.selectedBorder) rather
# than a colour invented for the icon, so the launcher and the icon agree.
SWIRL = [
    (214, 232, 255),
    (109, 170, 255),
    (61, 139, 255),
    (28, 86, 190),
]
SWIRL_DARK = (10, 22, 48)


def vertical_gradient(size, colours):
    width, height = size
    grad = Image.new("RGB", (1, height))
    pixels = grad.load()
    steps = len(colours) - 1
    for y in range(height):
        position = y / max(1, height - 1) * steps
        index = min(int(position), steps - 1)
        blend = position - index
        start, end = colours[index], colours[index + 1]
        pixels[0, y] = tuple(
            int(start[c] + (end[c] - start[c]) * blend) for c in range(3)
        )
    return grad.resize((width, height))


def background():
    canvas = vertical_gradient((SIZE, SIZE), [BG_TOP, BG_BOTTOM]).convert("RGBA")
    glow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(glow)
    draw.ellipse((60, 250, SIZE - 60, SIZE - 120), fill=(30, 80, 200, 110))
    draw.ellipse((200, 520, SIZE - 200, SIZE - 60), fill=(60, 140, 235, 95))
    glow = glow.filter(ImageFilter.GaussianBlur(120))
    return Image.alpha_composite(canvas, glow)


def retro_script(width):
    """The Retro script, cut out of the logo rather than redrawn."""
    logo = Image.open(LOGO).convert("RGBA")
    script = logo.crop((168, 0, 578, 92))
    # The bracket rules either side of the script poke into the crop. Every
    # pixel of the script is warm, so anything bluer than it is red is a rule.
    pixels = script.load()
    for y in range(script.height):
        for x in range(script.width):
            r, g, b, a = pixels[x, y]
            if a and b > r:
                pixels[x, y] = (r, g, b, 0)
    height = round(script.height * width / script.width)
    return script.resize((width, height), Image.LANCZOS)


def chrome_text(text, width, height):
    """[text] in the wordmark's blue, with the dark outline it has."""
    size = 10
    font = ImageFont.truetype(FONT, size)
    while True:
        probe = ImageFont.truetype(FONT, size + 4)
        box = probe.getbbox(text)
        if box[2] - box[0] > width or box[3] - box[1] > height:
            break
        size += 4
        font = probe

    box = font.getbbox(text)
    pad = 18
    layer = Image.new("RGBA", (box[2] - box[0] + pad * 2, box[3] - box[1] + pad * 2))
    ImageDraw.Draw(layer).text(
        (pad - box[0], pad - box[1]), text, font=font, fill=(255, 255, 255, 255)
    )

    mask = layer.split()[3]
    fill = vertical_gradient(layer.size, CHROME).convert("RGBA")
    fill.putalpha(mask)

    outline = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    outline.paste((12, 20, 48, 255), (0, 0), mask.filter(ImageFilter.MaxFilter(9)))
    return Image.alpha_composite(outline, fill)


def _spiral_ribbon(cx, cy, turns, r0, r1, w0, w1, phase):
    """Points round a tapering spiral arm, out along one edge and back the other.

    An Archimedean spiral rather than a traced curve, for the same reason the
    Amiga tick is redrawn from its points: it is defined by numbers that can be
    nudged, not by a bitmap that can only be resampled.
    """
    steps = 240
    outer, inner = [], []
    for i in range(steps + 1):
        t = i / steps
        angle = phase + t * turns * 2 * math.pi
        radius = r0 + (r1 - r0) * t
        # Taper the arm as it unwinds, so it reads as a swirl and not a hoop.
        width = (w0 + (w1 - w0) * t) / 2
        nx, ny = math.cos(angle), math.sin(angle)
        outer.append((cx + nx * (radius + width), cy + ny * (radius + width)))
        inner.append((cx + nx * (radius - width), cy + ny * (radius - width)))
    return outer + inner[::-1]


def swirl(width):
    """The Saturn's swirl: a ring, broken by an arm winding out of the centre."""
    height = width
    scale = 4
    w = width * scale
    layer = Image.new("RGBA", (w, w), (0, 0, 0, 0))

    cx = cy = w / 2
    shape = Image.new("L", (w, w), 0)
    draw = ImageDraw.Draw(shape)

    # The ring: most of a circle, with a bite taken out where the arm escapes.
    ring_r = w * 0.40
    ring_w = w * 0.105
    draw.arc(
        (cx - ring_r, cy - ring_r, cx + ring_r, cy + ring_r),
        start=-58,
        end=212,
        fill=255,
        width=round(ring_w),
    )

    # The arm, unwinding from the middle out to where the ring stops.
    draw.polygon(
        _spiral_ribbon(
            cx,
            cy,
            turns=0.62,
            r0=w * 0.045,
            r1=ring_r,
            w0=w * 0.135,
            w1=ring_w,
            phase=math.radians(150),
        ),
        fill=255,
    )

    fill = vertical_gradient((w, w), SWIRL).convert("RGBA")
    fill.putalpha(shape)

    # A dark rim under it all, the same trick the wordmark uses, so the mark
    # keeps its edge against the glow behind it.
    rim = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    rim.paste(SWIRL_DARK + (255,), (0, 0), shape.filter(ImageFilter.MaxFilter(9)))
    stamped = Image.alpha_composite(rim, fill)
    return stamped.resize((width, height), Image.LANCZOS)


def artwork(width):
    layer = Image.new("RGBA", (width, width), (0, 0, 0, 0))

    script = retro_script(round(width * 0.80))
    name = chrome_text("SATURN", round(width * 0.72), round(width * 0.19))
    mark = swirl(round(width * 0.44))

    stack = script.height + name.height + mark.height + round(width * 0.05)
    top = max(0, (width - stack) // 2)

    layer.alpha_composite(script, ((width - script.width) // 2, top))
    top += script.height + round(width * 0.015)
    layer.alpha_composite(name, ((width - name.width) // 2, top))
    top += name.height + round(width * 0.030)
    layer.alpha_composite(mark, ((width - mark.width) // 2, top))
    return layer


def master():
    canvas = background()
    art = artwork(round(SIZE * 0.86))
    canvas.alpha_composite(art, ((SIZE - art.width) // 2, (SIZE - art.width) // 2))
    return canvas


def foreground():
    """Everything inside the middle two-thirds, where the launcher's mask
    cannot eat it."""
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    art = artwork(round(SIZE * 0.62))
    layer.alpha_composite(art, ((SIZE - art.width) // 2, (SIZE - art.width) // 2))
    return layer


def rounded(image, radius_fraction=0.22):
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, image.width - 1, image.height - 1),
        radius=round(image.width * radius_fraction),
        fill=255,
    )
    out = image.copy()
    out.putalpha(mask)
    return out


def main():
    icon = master()
    fore = foreground()
    back = background()

    res = os.path.join(HERE, "android", "app", "src", "main", "res")
    legacy = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
    layers = {"mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}
    for density, px in legacy.items():
        folder = os.path.join(res, f"mipmap-{density}")
        os.makedirs(folder, exist_ok=True)
        icon.resize((px, px), Image.LANCZOS).save(
            os.path.join(folder, "ic_launcher.png")
        )
        rounded(icon.resize((px, px), Image.LANCZOS), 0.5).save(
            os.path.join(folder, "ic_launcher_round.png")
        )
        fore.resize((layers[density],) * 2, Image.LANCZOS).save(
            os.path.join(folder, "ic_launcher_foreground.png")
        )
        back.convert("RGB").resize((layers[density],) * 2, Image.LANCZOS).save(
            os.path.join(folder, "ic_launcher_background.png")
        )

    ios = os.path.join(HERE, "ios", "Runner", "Assets.xcassets", "AppIcon.appiconset")
    sizes = {
        "Icon-App-20x20@1x.png": 20,
        "Icon-App-20x20@2x.png": 40,
        "Icon-App-20x20@3x.png": 60,
        "Icon-App-29x29@1x.png": 29,
        "Icon-App-29x29@2x.png": 58,
        "Icon-App-29x29@3x.png": 87,
        "Icon-App-40x40@1x.png": 40,
        "Icon-App-40x40@2x.png": 80,
        "Icon-App-40x40@3x.png": 120,
        "Icon-App-60x60@2x.png": 120,
        "Icon-App-60x60@3x.png": 180,
        "Icon-App-76x76@1x.png": 76,
        "Icon-App-76x76@2x.png": 152,
        "Icon-App-83.5x83.5@2x.png": 167,
        "Icon-App-1024x1024@1x.png": 1024,
    }
    if os.path.isdir(ios):
        for name, px in sizes.items():
            icon.convert("RGB").resize((px, px), Image.LANCZOS).save(
                os.path.join(ios, name)
            )

    # The repo keeps a copy of the launcher icon in pics/ for the store listing
    # and the README; regenerate it here rather than letting it drift.
    pics = os.path.join(os.path.dirname(HERE), "pics")
    if os.path.isdir(pics):
        icon.convert("RGB").resize((512, 512), Image.LANCZOS).save(
            os.path.join(pics, "ymir_play_store_icon_512.png")
        )
    icon.convert("RGB").resize((512, 512), Image.LANCZOS).save(
        os.path.join(HERE, "android", "app", "src", "ic_launcher-playstore.png")
    )

    # The adaptive icon composes a colour resource, not the gradient layer
    # written above, so derive that colour from the same BG_TOP the icon is
    # built on. Left to drift, the masked foreground floats on a plate that
    # does not match the icon beside it.
    plate = "#%02X%02X%02X" % BG_TOP
    for bucket in ("values", "values-night"):
        path = os.path.join(res, bucket, "ic_launcher_background.xml")
        if os.path.isfile(path):
            with open(path, "w") as handle:
                handle.write(
                    '<?xml version="1.0" encoding="utf-8"?>\n'
                    "<resources>\n"
                    '    <color name="ic_launcher_background">'
                    + plate
                    + "</color>\n"
                    "</resources>\n"
                )

    print("icons written")


if __name__ == "__main__":
    main()
