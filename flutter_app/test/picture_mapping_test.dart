// Proves the pointer-to-framebuffer mapping the Virtua Gun aims through.
//
// The overlay used to scale touches against its own full size while the
// picture is drawn with BoxFit.contain. On a 16:9 phone showing a 4:3 Saturn
// picture that is wrong by the width of the pillarbox at every point except
// the exact centre, and wrong by more the further from centre you aim -- which
// is not something you can eyeball on a device, because a light-gun game with
// a slightly wrong aim just feels like a game you are bad at.

import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';
import 'package:retro_saturn/input/picture_mapping.dart';

void main() {
  // A 4:3 Saturn picture on a 16:9 panel: pillarboxed, bars left and right.
  const panel = Size(1600, 900);
  const fbW = 320;
  const fbH = 224;

  group('pictureRect', () {
    test('centres the picture and preserves its aspect', () {
      final r = pictureRect(panel, fbW, fbH);
      // Height-limited: 900/224 is the smaller scale, so the picture is as
      // tall as the panel and narrower.
      expect(r.height, closeTo(900, 0.001));
      expect(r.width, closeTo(224 == 0 ? 0 : 320 * (900 / 224), 0.001));
      expect(r.left, closeTo((1600 - r.width) / 2, 0.001));
      expect(r.top, closeTo(0, 0.001));
    });

    test('is empty for a zero-sized panel or an unstarted core', () {
      expect(pictureRect(Size.zero, fbW, fbH).isEmpty, isTrue);
      expect(pictureRect(panel, 0, 0).isEmpty, isTrue);
    });
  });

  group('mapToFramebuffer', () {
    test('the centre of the picture is the centre of the framebuffer', () {
      final p = mapToFramebuffer(const Offset(800, 450), panel, fbW, fbH);
      expect(p, isNotNull);
      expect(p!.x, closeTo(fbW / 2, 1));
      expect(p.y, closeTo(fbH / 2, 1));
    });

    test('a pillarbox bar is off screen, not an aim at the edge', () {
      // 20px from the left of the PANEL is well inside the left bar.
      final p = mapToFramebuffer(const Offset(20, 450), panel, fbW, fbH);
      expect(
        p,
        isNull,
        reason: 'a bar is not part of the picture, so nothing is aimed at',
      );
    });

    test('the picture edges map to the framebuffer edges', () {
      final r = pictureRect(panel, fbW, fbH);
      final left = mapToFramebuffer(
        Offset(r.left + 0.5, r.top + r.height / 2),
        panel,
        fbW,
        fbH,
      );
      final right = mapToFramebuffer(
        Offset(r.right - 0.5, r.top + r.height / 2),
        panel,
        fbW,
        fbH,
      );
      expect(left!.x, 1);
      expect(right!.x, fbW - 1);
    });

    test('never returns the off-screen sentinel as a real aim', () {
      // Sweep the whole picture; no interior point may collide with 0xFFFF
      // or land on 0, both of which games read as "not aimed here".
      final r = pictureRect(panel, fbW, fbH);
      // Sweeps up to but not including the far edge: pictureRect is a Rect,
      // and a Rect's right/bottom edge is exclusive, so the last pixel column
      // belongs to the bar the same way it does in the drawn picture.
      for (var i = 0; i < 40; i++) {
        final t = i / 40;
        final p = mapToFramebuffer(
          Offset(r.left + r.width * t, r.top + r.height * t),
          panel,
          fbW,
          fbH,
        );
        expect(p, isNotNull);
        expect(p!.x, inInclusiveRange(1, fbW - 1));
        expect(p.y, inInclusiveRange(1, fbH - 1));
        expect(p.x, isNot(kGunOffScreen));
        expect(p.y, isNot(kGunOffScreen));
      }
    });

    test('follows a mid-game resolution change', () {
      // Same physical spot, two Saturn modes. The proportion across the
      // picture must hold; the pixel numbers must not.
      const spot = Offset(800, 300);
      final low = mapToFramebuffer(spot, panel, 320, 224)!;
      final high = mapToFramebuffer(spot, panel, 704, 512)!;
      expect(high.x / 704, closeTo(low.x / 320, 0.01));
      expect(
        high.x,
        greaterThan(low.x),
        reason: 'a higher mode has more pixels across the same picture',
      );
    });

    test('a letterboxed (not pillarboxed) panel bars top and bottom', () {
      // A tall panel showing a 4:3 picture: bars above and below instead.
      const tall = Size(600, 1200);
      expect(mapToFramebuffer(const Offset(300, 10), tall, fbW, fbH), isNull);
      expect(
        mapToFramebuffer(const Offset(300, 600), tall, fbW, fbH),
        isNotNull,
      );
    });
  });
}
