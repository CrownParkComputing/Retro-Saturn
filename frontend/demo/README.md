# Pixel Poppy Pong

Saturn homebrew by nando, originally distributed through the SegaXtreme competition:
https://segaxtreme.net/resources/pixel-poppy-pong.282/

This setup-only demo is separate from the GPL emulator code. The supplied project includes it as freely distributed homebrew; no Sega BIOS or commercial Saturn games are bundled.

The original data-track payload is unchanged. The thirteen CD audio tracks are packaged as Ogg Vorbis to reduce the bundled size. `manifest.json` records the original image and extracted data hashes. `tool/make_setup_demo.py` reproduces this package from the original BIN/CUE.

Booting the demo requires a user-supplied Saturn BIOS. Use the Return to wizard control to continue setup after testing.
