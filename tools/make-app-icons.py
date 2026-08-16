#!/usr/bin/env python3
"""Generate launcher icons from pics/ymir_launcher_icon.png.

Produces:
  flutter_app/assets/icons/ic_launcher.png            (legacy 192)
  flutter_app/assets/icons/ic_launcher_round.png      (legacy round 192)
  flutter_app/android/app/src/main/res/mipmap-*/ic_launcher.png (adaptive)
"""
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SRC = REPO / 'pics' / 'ymir_launcher_icon.png'
ASSETS = REPO / 'flutter_app' / 'assets' / 'icons'
ANDROID_RES = REPO / 'flutter_app' / 'android' / 'app' / 'src' / 'main' / 'res'

if not SRC.exists():
    print(f'Source icon not found at {SRC}', file=sys.stderr)
    sys.exit(1)

ASSETS.mkdir(parents=True, exist_ok=True)
print(f'Using source: {SRC}')
print(f'Target assets: {ASSETS}')

# Use ImageMagick if available; otherwise symlink as fallback.
if subprocess.call(['which', 'magick'], stdout=subprocess.DEVNULL) == 0:
    for size, name in [(192, 'ic_launcher.png'), (192, 'ic_launcher_round.png')]:
        subprocess.check_call([
            'magick', str(SRC), '-resize', f'{size}x{size}', str(ASSETS / name)
        ])
elif subprocess.call(['which', 'convert'], stdout=subprocess.DEVNULL) == 0:
    for size, name in [(192, 'ic_launcher.png'), (192, 'ic_launcher_round.png')]:
        subprocess.check_call([
            'convert', str(SRC), '-resize', f'{size}x{size}', str(ASSETS / name)
        ])
else:
    print('ImageMagick not found — creating symlinks as placeholder.', file=sys.stderr)
    for name in ('ic_launcher.png', 'ic_launcher_round.png'):
        os.symlink(SRC, ASSETS / name)

# Android adaptive icons live under mipmap-*dpi/ic_launcher.png +
# mipmap-*dpi/ic_launcher_round.png. Sizes:
sizes = {'mdpi': 48, 'hdpi': 72, 'xhdpi': 96, 'xxhdpi': 144, 'xxxhdpi': 192}
for dpi, size in sizes.items():
    d = ANDROID_RES / f'mipmap-{dpi}'
    d.mkdir(parents=True, exist_ok=True)
    if subprocess.call(['which', 'magick'], stdout=subprocess.DEVNULL) == 0:
        subprocess.check_call(['magick', str(SRC), '-resize', f'{size}x{size}',
                              str(d / 'ic_launcher.png')])
        subprocess.check_call(['magick', str(SRC), '-resize', f'{size}x{size}',
                              str(d / 'ic_launcher_round.png')])
    elif subprocess.call(['which', 'convert'], stdout=subprocess.DEVNULL) == 0:
        subprocess.check_call(['convert', str(SRC), '-resize', f'{size}x{size}',
                              str(d / 'ic_launcher.png')])
        subprocess.check_call(['convert', str(SRC), '-resize', f'{size}x{size}',
                              str(d / 'ic_launcher_round.png')])
    else:
        os.symlink(SRC, d / 'ic_launcher.png')
        os.symlink(SRC, d / 'ic_launcher_round.png')

print('Done.')