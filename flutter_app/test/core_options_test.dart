// Core options: the catalogue, and what actually reaches the core.
//
// The bounds in the catalogue exist twice on purpose -- here for the UI, and
// again in the bridge, which refuses anything outside them. These tests pin
// the app half: that the two agree on defaults, that a value the core would
// reject never leaves the app, and that a refusal is not recorded as success.

import 'package:flutter_test/flutter_test.dart';
import 'package:retro_saturn/data/core_option.dart';
import 'package:retro_saturn/ffi/ymir_bindings.dart';
import 'package:retro_saturn/services/core_options_service.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'fakes/fake_ymir_core.dart';

/// A core that refuses everything, standing in for a build whose bounds have
/// moved out from under the app.
class RefusingCore extends FakeYmirCore {
  @override
  int setCoreOption(YmirCoreOption option, int value) => -3;
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() async {
    SharedPreferences.setMockInitialValues(<String, Object>{});
    CoreOptionsService.resetForTest();
    await CoreOptionsService.load();
  });

  group('catalogue', () {
    test('every option the core knows about has a row', () {
      // If ymir-core gains an option, the enum grows and this fails until the
      // catalogue is updated -- the screen renders from the catalogue, so a
      // missing row is an option nobody can reach.
      for (final id in YmirCoreOption.values) {
        expect(kCoreOptions.where((o) => o.id == id).length, 1,
            reason: '${id.name} needs exactly one catalogue entry');
      }
    });

    test('defaults match what ymir-core documents', () {
      expect(coreOptionFor(YmirCoreOption.sh2Overclock).defaultValue, 100);
      expect(coreOptionFor(YmirCoreOption.cdReadSpeed).defaultValue, 2);
      expect(coreOptionFor(YmirCoreOption.audioInterpolation).defaultValue, 1);
      expect(coreOptionFor(YmirCoreOption.threadedVdp1).defaultValue, 1);
    });

    test('slider bounds stay inside what the bridge accepts', () {
      final cd = coreOptionFor(YmirCoreOption.cdReadSpeed);
      expect(cd.min, greaterThanOrEqualTo(2));
      expect(cd.max, lessThanOrEqualTo(200));
      final oc = coreOptionFor(YmirCoreOption.sh2Overclock);
      expect(oc.min, greaterThanOrEqualTo(50));
      expect(oc.max, lessThanOrEqualTo(500));
    });

    test('a default is always a value the option can hold', () {
      for (final o in kCoreOptions) {
        expect(o.sanitize(o.defaultValue), o.defaultValue,
            reason: '${o.title} default is out of its own range');
      }
    });

    test('out-of-range values are pulled back in', () {
      final cd = coreOptionFor(YmirCoreOption.cdReadSpeed);
      expect(cd.sanitize(0), cd.min);
      expect(cd.sanitize(9999), cd.max);
      final vs = coreOptionFor(YmirCoreOption.videoStandard);
      expect(vs.sanitize(7), vs.defaultValue,
          reason: 'an unknown choice falls back rather than being passed on');
    });
  });

  group('service', () {
    test('applyAll pushes every option to the core', () async {
      final core = FakeYmirCore();
      CoreOptionsService.applyAll(core);
      expect(core.coreOptions.length, kCoreOptions.length);
      expect(core.coreOptions[YmirCoreOption.cdReadSpeed], 2);
    });

    test('a set is applied and remembered', () async {
      final core = FakeYmirCore();
      final applied =
          await CoreOptionsService.set(core, YmirCoreOption.cdReadSpeed, 8);
      expect(applied, 8);
      expect(core.coreOptions[YmirCoreOption.cdReadSpeed], 8);
      expect(CoreOptionsService.valueOf(YmirCoreOption.cdReadSpeed), 8);

      // Survives a restart.
      CoreOptionsService.resetForTest();
      await CoreOptionsService.load();
      expect(CoreOptionsService.valueOf(YmirCoreOption.cdReadSpeed), 8);
    });

    test('an out-of-range set is sanitized before it reaches the core',
        () async {
      final core = FakeYmirCore();
      await CoreOptionsService.set(core, YmirCoreOption.sh2Overclock, 9000);
      expect(core.coreOptions[YmirCoreOption.sh2Overclock],
          coreOptionFor(YmirCoreOption.sh2Overclock).max);
    });

    test('a refused set is not remembered as applied', () async {
      final core = RefusingCore();
      final before = CoreOptionsService.valueOf(YmirCoreOption.cdReadSpeed);
      final applied =
          await CoreOptionsService.set(core, YmirCoreOption.cdReadSpeed, 16);
      expect(applied, before,
          reason: 'the UI must show what is in force, not what was asked for');
      expect(CoreOptionsService.valueOf(YmirCoreOption.cdReadSpeed), before);
    });

    test('applyAll falls back to defaults when the core refuses', () {
      final core = RefusingCore();
      CoreOptionsService.applyAll(core);
      expect(CoreOptionsService.valueOf(YmirCoreOption.cdReadSpeed),
          coreOptionFor(YmirCoreOption.cdReadSpeed).defaultValue);
    });

    test('anyChanged tracks divergence from the core defaults', () async {
      final core = FakeYmirCore();
      expect(CoreOptionsService.anyChanged, isFalse);
      await CoreOptionsService.set(core, YmirCoreOption.cdReadSpeed, 8);
      expect(CoreOptionsService.anyChanged, isTrue);
      await CoreOptionsService.resetAll(core);
      expect(CoreOptionsService.anyChanged, isFalse);
    });
  });
}
