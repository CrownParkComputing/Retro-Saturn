#!/usr/bin/env python3
"""Package Retro-Saturn interface artwork without flattening transparency.

    python3 tool/make_saturn_art.py --interface-only

The app trims transparent padding and makes thumbnails when loading these
masters. Copying the PNGs intact preserves their alpha and product details.
Without --interface-only, the existing launcher-icon sync also runs.
"""

from __future__ import annotations

import argparse
import os
import shutil
from PIL import Image

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON = os.path.join(HERE, "store", "play", "icon-512.png")
ANDROID = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
INTERFACE = {
    "wordmark.png": "wordmark-transparent.png",
    "console.png": "console.png",
    "control-pad.png": "control-pad.png",
    "analog-pad.png": "analog-pad.png",
    "arcade-racer.png": "arcade-racer.png",
    "mission-stick.png": "mission-stick-transparent.png",
    "virtua-gun.png": "virtua-gun.png",
    "shuttle-mouse.png": "shuttle-mouse.png",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interface-only", action="store_true")
    args = parser.parse_args()
    art = os.path.join(HERE, "frontend", "assets")
    os.makedirs(art, exist_ok=True)
    for name, master in INTERFACE.items():
        src = os.path.join(HERE, "store", "art", master)
        with Image.open(src) as image:
            if "A" not in image.getbands() or image.getchannel("A").getextrema()[0] != 0:
                raise SystemExit("interface master must have real transparency: " + src)
        shutil.copyfile(src, os.path.join(art, name))
        print("interface: " + name + " (original alpha preserved)")
    if args.interface_only:
        return

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
