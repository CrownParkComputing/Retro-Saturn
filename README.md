# Retro-Saturn

A native SDL3 and Dear ImGui frontend for the GPL-licensed [Ymir Saturn core](https://github.com/ymir-emu/Ymir). The core is pinned as a submodule; the app's embedding bridge lives in `native/core`.

## Collection folders

Choose a parent folder in setup or on the Paths page:

```text
Saturn/
  bios/       Your Saturn BIOS
  games/      Disc images and their accompanying tracks
  artwork/    Local PNG or JPEG images
  saves/      Battery RAM and save states
```

Existing names such as `Games`, `discs`, `cd`, `BIOS` and `Saves` are reused. A flat collection of disc images remains supported, without moving its files. Android uses a persisted document-tree grant when a folder has no directly readable path.

Name artwork after its disc, replacing the disc extension: `Daytona USA (USA).chd` uses `artwork/Daytona USA (USA).png` or `.jpg`. Matching ignores case and also accepts a multi-disc game's shared title. Use **Paths → Refresh artwork and library** after adding or replacing images. Artwork stays local; there is no RetroMedia account, catalogue, artwork download or game download client.

The bundled Pixel Poppy Pong homebrew demo runs only from the setup wizard. Its side button, Escape and Android Back return to the same setup step. Demo battery RAM and save states are kept separately from the user's games. A user-supplied Saturn BIOS is required.

## Linux development build

Requires CMake 3.28+, Ninja, a C++20 compiler, SDL3 development files and ALSA development files.

```sh
git submodule update --init --recursive
bash tool/build-linux.sh
build/linux/RetroSaturn --wizard
ctest --test-dir build/core-linux --output-on-failure
ctest --test-dir build/linux --output-on-failure
```

## Android development build

Install Android SDK 36, NDK 28.2.13676358 and JDK 17 or newer. The native build downloads SDL3 source on its first run.

```sh
JOBS=6 bash android/build-core.sh
cd android
./gradlew :app:assembleDebug
```

The app keeps its existing `com.saturn_emu.android` identity and targets API 36. Store release versioning and signing are separate from development previews.

## License

The emulator source code is distributed under the GNU General Public License,
version 3. See [LICENSE](LICENSE) for the full license text.

GPLv3 permits charging money for copies of GPL software, including copies
distributed through an app store, provided the distributor follows the GPL
requirements. That means the Google Play listing and app distribution must keep
copyright notices intact, include the GPL license text, provide the complete
corresponding source for the exact release, and preserve the user's GPL rights to
copy, inspect, modify, and redistribute the software.

More detail for Play Store distribution is in
[PLAY_STORE_LICENSING.md](PLAY_STORE_LICENSING.md).
