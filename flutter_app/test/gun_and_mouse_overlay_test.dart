// What the overlays actually send to the core, driven through real gestures.
//
// picture_mapping_test proves the arithmetic; this proves the wiring — that a
// tap on the picture becomes an aimed shot, that a tap on a bar becomes a
// reload rather than a shot at the edge, and that a drag on the mouse overlay
// produces relative motion at all (it produced a hardcoded zero until the
// bridge learned to carry deltas).

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:retro_saturn/ffi/ymir_bindings.dart';
import 'package:retro_saturn/input/picture_mapping.dart';
import 'package:retro_saturn/widgets/shuttle_mouse_overlay.dart';
import 'package:retro_saturn/widgets/virtua_gun_overlay.dart';

import 'fakes/fake_ymir_core.dart';

/// A panel wider than 4:3, so the picture is pillarboxed and a naive mapping
/// (the one this replaced) would be visibly wrong.
const Size kPanel = Size(800, 300);
const int kFbW = 320;
const int kFbH = 224;

Future<void> pumpGun(WidgetTester tester, FakeYmirCore core) async {
  final frameSize = ValueNotifier<Size>(Size(kFbW.toDouble(), kFbH.toDouble()));
  await tester.pumpWidget(
    MaterialApp(
      home: Center(
        child: SizedBox(
          width: kPanel.width,
          height: kPanel.height,
          child: VirtuaGunOverlay(core: core, port: 1, frameSize: frameSize),
        ),
      ),
    ),
  );
}

void main() {
  group('VirtuaGunOverlay', () {
    testWidgets('a tap on the picture aims there and pulls the trigger', (
      tester,
    ) async {
      final core = FakeYmirCore();
      await pumpGun(tester, core);

      final origin = tester.getTopLeft(find.byType(VirtuaGunOverlay));
      final rect = pictureRect(kPanel, kFbW, kFbH);
      final centreOfPicture =
          origin +
          Offset(rect.left + rect.width / 2, rect.top + rect.height / 2);

      await tester.tapAt(centreOfPicture);
      await tester.pump();

      final fired = core.gunStates.where((s) => s.trigger).toList();
      expect(fired, isNotEmpty, reason: 'the tap should have fired');
      final shot = fired.first;
      expect(shot.x, closeTo(kFbW / 2, 2));
      expect(shot.y, closeTo(kFbH / 2, 2));
      expect(shot.reload, isFalse);
    });

    testWidgets(
      'a tap on the letterbox bar reloads instead of shooting the edge',
      (tester) async {
        final core = FakeYmirCore();
        await pumpGun(tester, core);

        final origin = tester.getTopLeft(find.byType(VirtuaGunOverlay));
        // 4px in from the panel's left edge: inside the bar, outside the picture.
        await tester.tapAt(origin + const Offset(4, 150));
        await tester.pump();

        expect(core.gunStates, isNotEmpty);
        final s = core.gunStates.first;
        expect(s.x, kGunOffScreen);
        expect(s.y, kGunOffScreen);
        expect(
          s.reload,
          isTrue,
          reason: 'a trigger pulled off the picture is the reload gesture',
        );
      },
    );

    testWidgets('releasing the touch releases the trigger', (tester) async {
      final core = FakeYmirCore();
      await pumpGun(tester, core);

      final origin = tester.getTopLeft(find.byType(VirtuaGunOverlay));
      final rect = pictureRect(kPanel, kFbW, kFbH);
      final spot = origin + Offset(rect.left + rect.width / 2, rect.center.dy);

      final gesture = await tester.startGesture(spot);
      await tester.pump();
      await gesture.up();
      await tester.pump();

      expect(core.gunStates.last.trigger, isFalse);
    });

    testWidgets('the RELOAD button raises reload while held', (tester) async {
      final core = FakeYmirCore();
      await pumpGun(tester, core);

      final gesture = await tester.startGesture(
        tester.getCenter(find.text('RELOAD')),
      );
      await tester.pump();
      expect(core.gunStates.last.reload, isTrue);

      await gesture.up();
      await tester.pump();
      expect(core.gunStates.last.reload, isFalse);
    });

    testWidgets('the buttons clear the system gesture inset', (tester) async {
      // On the Flip2 (gesture navigation) the system reserves the bottom 54px
      // of the display, and the app draws edge-to-edge behind it. Buttons
      // pinned at bottom: 12 rendered perfectly and could not be pressed at
      // all -- the home-gesture handler took the touch first, so there was
      // nothing to see on screen and nothing in any log. Assert the geometry
      // instead: nothing interactive may sit inside the reported inset.
      const inset = 54.0;
      final core = FakeYmirCore();
      final frameSize = ValueNotifier<Size>(
        Size(kFbW.toDouble(), kFbH.toDouble()),
      );

      await tester.pumpWidget(
        MaterialApp(
          home: MediaQuery(
            data: const MediaQueryData(
              size: kPanel,
              padding: EdgeInsets.only(bottom: inset),
            ),
            child: Center(
              child: SizedBox(
                width: kPanel.width,
                height: kPanel.height,
                child: VirtuaGunOverlay(
                  core: core,
                  port: 1,
                  frameSize: frameSize,
                ),
              ),
            ),
          ),
        ),
      );

      final overlayBottom = tester
          .getRect(find.byType(VirtuaGunOverlay))
          .bottom;
      for (final label in ['START', 'RELOAD']) {
        final box = tester.getRect(find.text(label));
        expect(
          box.bottom,
          lessThanOrEqualTo(overlayBottom - inset),
          reason:
              '$label sits inside the system gesture strip and '
              'cannot be pressed',
        );
      }
    });

    testWidgets('a mouse aims by hovering, with no button held', (
      tester,
    ) async {
      final core = FakeYmirCore();
      await pumpGun(tester, core);

      final origin = tester.getTopLeft(find.byType(VirtuaGunOverlay));
      final rect = pictureRect(kPanel, kFbW, kFbH);
      final spot =
          origin + Offset(rect.left + rect.width * 0.75, rect.center.dy);

      final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
      await mouse.addPointer(location: origin);
      addTearDown(mouse.removePointer);
      await tester.pump();
      await mouse.moveTo(spot);
      await tester.pump();

      expect(core.gunStates, isNotEmpty);
      final aim = core.gunStates.last;
      expect(aim.trigger, isFalse, reason: 'hovering aims, it does not fire');
      expect(aim.x, closeTo(kFbW * 0.75, 4));
    });
  });

  group('ShuttleMouseOverlay', () {
    testWidgets('a drag produces relative motion', (tester) async {
      final core = FakeYmirCore();
      await tester.pumpWidget(
        MaterialApp(
          home: SizedBox(
            width: kPanel.width,
            height: kPanel.height,
            child: ShuttleMouseOverlay(core: core, port: 1),
          ),
        ),
      );

      final centre = tester.getCenter(find.byType(ShuttleMouseOverlay));
      final gesture = await tester.startGesture(centre);
      await gesture.moveBy(const Offset(40, 20));
      await tester.pump();
      await gesture.up();
      await tester.pump();

      expect(
        core.mouseMotion,
        isNotEmpty,
        reason: 'the Saturn mouse used to report a hardcoded zero',
      );
      final dx = core.mouseMotion.fold<int>(0, (a, m) => a + m.dx);
      final dy = core.mouseMotion.fold<int>(0, (a, m) => a + m.dy);
      expect(dx, greaterThan(0));
      expect(dy, greaterThan(0));
      expect(dx, greaterThan(dy), reason: 'it moved further across than down');
    });

    testWidgets('the L button presses and releases the left mouse button', (
      tester,
    ) async {
      final core = FakeYmirCore();
      await tester.pumpWidget(
        MaterialApp(
          home: SizedBox(
            width: kPanel.width,
            height: kPanel.height,
            child: ShuttleMouseOverlay(core: core, port: 1),
          ),
        ),
      );

      final gesture = await tester.startGesture(
        tester.getCenter(find.text('L')),
      );
      await tester.pump();
      expect(
        core.mouseButtons.where(
          (b) => b.button == YmirMouseButton.left && b.pressed,
        ),
        isNotEmpty,
      );

      await gesture.up();
      await tester.pump();
      expect(core.mouseButtons.last.pressed, isFalse);
    });
  });
}
