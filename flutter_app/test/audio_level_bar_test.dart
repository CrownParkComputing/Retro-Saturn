// The level meter used a width FRACTION, and fractions of an unbounded width
// are NaN. The bar can be handed an unbounded width, and its level is zero
// whenever nothing is playing -- so `infinity * 0.0` took the whole screen
// down with "BoxConstraints has NaN values", in exactly the state a store
// reviewer opens it in.
import 'package:flutter_test/flutter_test.dart';
import 'package:retro_saturn/screens/audio_settings_screen.dart';

void main() {
  test('an unbounded width never yields NaN, at any level', () {
    for (final level in <int>[0, 1, 50, 99, 100]) {
      final w = audioBarFillWidth(double.infinity, level, false);
      expect(w.isNaN, isFalse, reason: 'level $level produced NaN');
      expect(w.isFinite, isTrue, reason: 'level $level produced $w');
    }
  });

  test('zero level on an unbounded width is the case that crashed', () {
    expect(audioBarFillWidth(double.infinity, 0, false), 0);
  });

  test('a bounded width fills proportionally', () {
    expect(audioBarFillWidth(200, 50, false), 100);
    expect(audioBarFillWidth(200, 100, false), 200);
    expect(audioBarFillWidth(200, 0, false), 0);
  });

  test('muted is empty whatever the level says', () {
    expect(audioBarFillWidth(200, 100, true), 0);
    expect(audioBarFillWidth(double.infinity, 100, true), 0);
  });

  test('levels outside 0..100 are clamped, not extrapolated', () {
    expect(audioBarFillWidth(200, -5, false), 0);
    expect(audioBarFillWidth(200, 250, false), 200);
  });
}
