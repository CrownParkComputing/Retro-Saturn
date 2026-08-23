# Homebrew test demo — Pixel Poppy Pong (PPPong)

This folder ships a **free, non-commercial Sega Saturn homebrew** disc image
so App Store reviewers (and users) can boot the emulator and verify it runs
**without any copyrighted/licensed Sega game or BIOS**.

Source: SegaXtreme homebrew competition
        https://segaxtreme.net/resources/pixel-poppy-pong.282/

Files:
- `PPPong.cue`  — CD cue sheet (MODE1 track 1 + audio tracks)
- `PPPong.bin`  — 86 MB Saturn disc image (header: "SEGA SEGASATURN Powered by SRL")

License / status:
- Freely distributed homebrew. NOT a Sega-licensed or commercial title.
- The "SEGA ENTERPRISES, LTD." strings in the image are standard Saturn SDK
  bootstrap boilerplate present on every Saturn disc (homebrew included) — they
  are NOT a sign of commercial/licensed game content.

Usage:
- This demo does NOT require a BIOS to be useful as a smoke test of the
  renderer/input; pair it with a user-supplied BIOS (dumped from owned
  hardware) to actually boot. The setup wizard's "Continue without a BIOS"
  path lets reviewers reach the workbench first.

NOTE: The Sega Saturn BIOS (`saturn_bios.bin` / IPL) is NEVER bundled here.
It is copyrighted and must be supplied by the user from their own hardware.
