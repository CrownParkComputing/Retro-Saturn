# APP_STORE_RELEASE — Cutting a release to Google Play + iOS TestFlight

## Android — Google Play via AAB

```bash
# Bump version in flutter_app/pubspec.yaml (currently 0.1.0+1)
native/ymir_core/android/build.sh
cd flutter_app
flutter build appbundle --release
# Upload flutter_app/build/app/outputs/bundle/release/app-release.aab
# to the Play Console internal testing track.
```

For signed releases, set up a keystore and add `key.properties` next
to `flutter_app/android/`:

```properties
storePassword=...
keyPassword=...
keyAlias=...
storeFile=/abs/path/to/upload-keystore.jks
```

Then `flutter build appbundle --release` signs with that keystore.

## iOS — TestFlight

```bash
# Bump CFBundleShortVersionString / CFBundleVersion in
# flutter_app/ios/Runner/Info.plist.
cd flutter_app
flutter build ipa --release --export-method app-store
# Upload via Xcode → Organizer → Distribute App → App Store Connect,
# or via altool/xcrun altool.
```

For first submission to App Store Connect you need an Apple Developer
account with a paid membership, an App Store Connect API key, and a
distribution provisioning profile that matches the bundle id
(`com.crownpark.ymir`).

## iOS — Ad-hoc (sideload to registered device without TestFlight)

```bash
flutter build ipa --release --export-method ad-hoc
# Then deploy via tools/device-push.sh --target ios --run,
# which delegates to the ViceMultiplatform device-push.sh pattern
# (MobAI HTTP API at 127.0.0.1:8686 + usbmuxd + JIT-debugger attach).
```

## Play Store + App Store metadata

Existing assets from the prior Android release still apply:

- `pics/ymir_launcher_icon.png` → `flutter_app/assets/icons/` via
  `tools/make-app-icons.py`
- `pics/ymir_main.png` → Play Store feature graphic
- `GOOGLE_PLAY_RELEASE.md` and `PLAY_STORE_LICENSING.md` (preserved)
  cover the per-account signing + privacy policy + data safety form
  steps.

## Changelog + version

Update `CHANGELOG.md` and bump `version:` in `flutter_app/pubspec.yaml`
before tagging. The CI workflow's release job runs only on
`refs/tags/v*` and attaches the artifacts from android/linux/ios jobs.

## Tag + push

```bash
git tag v0.1.0-android
git push origin v0.1.0-android
```

The CI release job attaches `app-release.apk`, `app-release.aab`,
`Runner.app.zip` and the Linux bundle tarball to the GitHub release
automatically.