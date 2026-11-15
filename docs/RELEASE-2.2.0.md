# Retro-Saturn 2.2.0

Release submissions completed on 15 November 2026. Store approval and publication remain controlled by Apple and Google.

| Platform | Build | Submission |
| --- | --- | --- |
| Android | 2.2.0 (6), target API 36 | Production track committed for Google review |
| iOS | 2.2.0 (202611151428) | App Store submission waiting for review; automatic release after approval |
| TestFlight | 202611151428 | Internal testing ready; existing external group waiting for beta review |
| Linux | Native 2.2.0 build | Built and launched for local verification |

The Android bundle was built from `995255b6572a453375075b7b7e536509e0058693` in [signed CI run 43720333920](https://github.com/CrownParkComputing/Retro-Saturn/actions/runs/43720333920). The final iOS archive was built from `7ed15b19c56ce9d8c862ebd15c6612f29e593f9c`; Changes after the Android build update iOS window handling, build configuration and dependencies. iOS uses SDL 3.4.18 with unused camera and raw HID backends disabled. Gamepads use Apple's controller API.

The emulator core is official Ymir commit `ff935379`, dated 7 November 2026, reporting development version 0.4.2. This is a pinned development snapshot, not a claim of a stable 0.4.2 release.

## Changes

- Parent-folder setup detects BIOS, games, artwork and saves folders, including existing naming aliases. Android document-tree selections are adopted immediately, disc staging includes companion tracks, and battery memory and save states sync back to the selected folder.
- The homebrew demo starts from the setup wizard and returns to the same wizard step. Its saves are isolated from the user's normal Saturn memory. A user-supplied Saturn BIOS is required.
- Artwork is loaded from a local artwork folder using matching disc filenames. Online RetroMedia accounts, downloads and artwork services have been removed.
- Library cards use clipped labels without nested title scrollbars. Dragging across cards scrolls the library without selecting a game.
- Backgrounding, switching windows or minimising immediately pauses emulation and closes audio output. Returning leaves gameplay paused until Resume. Moving the mouse outside the window does not pause it.
- iPhone and Android retain landscape layouts; iPad scenes can rotate and resize. The existing Retro-Saturn store identity and icon are preserved.
- Hardware graphics retain transparency, with controller-port labels on the launch page. [Artwork edit modes, prompts and saved masters](../store/art/TRANSPARENCY.md) are recorded separately.

## Verification

All nine native core checks passed in final CI. The focused folder/artwork checks and SDL audio lifecycle regression passed locally. The audio regression verifies that suspension stops callback consumption before polling events, that foregrounding alone does not resume audio, and that mouse-leave and other windows do not stop playback.

Retroid checks confirmed card swiping, removal of title scrollbars and real Saturn disc boot. The document-provider instrumentation check passed restore, pending local progress, same-size replacement, export and deletion cases using isolated fixtures. The user's original preview configuration was restored.

Linux boot and focus-switch checks showed the paused menu at 0 fps after returning. iPhone and iPad simulators booted the wizard/demo flow; the persistent return control returned to the demo step. Native store screenshots were replaced for both device families. No physical iOS-device test is claimed.

The Android release passed lint, bundle validation, existing upload-certificate matching, manifest checks for API 36 and all native-library 16 KB load-alignment checks. Apple processed the final iOS build as VALID; the submitted version passed readiness checks with no errors or warnings.

## Artifact checksums

| Artifact | SHA-256 |
| --- | --- |
| Android AAB | `c08e46e62b0a878c6e416782f800e4e46393552a19488c5544d0f85b9fa999cb` |
| iOS IPA | `9c4a0cd89be86daf6ef320d56e47d2b85e82a91f4ca7477644a51961ed7f52c8` |

The Play target-API notice can clear after Google approves and publishes build 6 to production. An accepted upload does not establish that the update is already live.
