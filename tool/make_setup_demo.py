#!/usr/bin/env python3
"""Repackage the existing Pixel Poppy Pong disc with compressed CD audio.

Usage: python3 tool/make_setup_demo.py path/to/original/PPPong.bin
The Saturn data track stays byte-for-byte identical after removing raw-sector
headers/ECC. Audio keeps its track order and uses Ymir's Vorbis CUE support.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("--output", type=Path, default=Path("frontend/demo"))
args = parser.parse_args()
source = args.source
cue = source.with_suffix(".cue").read_text()
tracks = []
for line in cue.splitlines():
    if match := re.match(r"\s*TRACK\s+(\d+)\s+(\S+)", line):
        tracks.append({"number": int(match[1]), "mode": match[2]})
    elif match := re.match(r"\s*INDEX\s+01\s+(\d+):(\d+):(\d+)", line):
        tracks[-1]["sector"] = (int(match[1]) * 60 + int(match[2])) * 75 + int(match[3])
    elif match := re.match(r"\s*PREGAP\s+(\S+)", line):
        tracks[-1]["pregap"] = match[1]
assert tracks[0]["mode"] == "MODE1/2352" and tracks[0]["sector"] == 0
assert len(tracks) > 1 and all(t["mode"] == "AUDIO" for t in tracks[1:])
data = source.read_bytes()
assert len(data) % 2352 == 0
args.output.mkdir(parents=True, exist_ok=True)
sectors = tracks[1]["sector"]
iso = bytearray()
for sector in range(sectors):
    raw = data[sector * 2352:(sector + 1) * 2352]
    assert raw[:12] == b"\x00" + b"\xff" * 10 + b"\x00" and raw[15] == 1
    iso.extend(raw[16:2064])
assert iso.startswith(b"SEGA SEGASATURN")
(args.output / "PPPong-data.iso").write_bytes(iso)
output_cue = ['FILE "PPPong-data.iso" BINARY', '  TRACK 01 MODE1/2048', '    INDEX 01 00:00:00']
for index, track in enumerate(tracks[1:], 1):
    start = track["sector"] * 2352
    end = tracks[index + 1]["sector"] * 2352 if index + 1 < len(tracks) else len(data)
    filename = f'PPPong-{track["number"]:02}.ogg'
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                    "-f", "s16le", "-ar", "44100", "-ac", "2", "-i", "pipe:0",
                    "-c:a", "libvorbis", "-q:a", "3", "-threads", "1",
                    str(args.output / filename)], input=data[start:end], check=True)
    output_cue += [f'FILE "{filename}" OGG', f'  TRACK {track["number"]:02} AUDIO']
    if "pregap" in track:
        output_cue.append('    PREGAP ' + track["pregap"])
    output_cue.append('    INDEX 01 00:00:00')
(args.output / "PPPong.cue").write_text("\n".join(output_cue) + "\n")
manifest = {
    "title": "Pixel Poppy Pong", "author": "nando",
    "source": "https://segaxtreme.net/resources/pixel-poppy-pong.282/",
    "original_sha256": hashlib.sha256(data).hexdigest(),
    "data_track_sha256": hashlib.sha256(iso).hexdigest(),
    "original_bytes": len(data), "data_sectors": sectors,
    "audio_tracks": len(tracks) - 1,
    "packaged_bytes": sum(p.stat().st_size for p in args.output.iterdir() if p.suffix in [".iso", ".cue", ".ogg"]),
}
(args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(json.dumps(manifest, indent=2))
