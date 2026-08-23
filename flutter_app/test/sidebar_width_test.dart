// The rail width feeds a clamp, and a clamp whose lower bound exceeds its
// upper one throws rather than clamping -- taking the whole workbench with it.
import 'package:flutter_test/flutter_test.dart';
import 'package:retro_saturn/theme/saturn_theme.dart';

void main() {
  test('the maximum is never below the minimum, at any iPhone width', () {
    // 440pt is the iPhone 17 Pro Max. Even at a third that is 146, and at the
    // original quarter it was 110 -- under the 120 floor, which is the case
    // that threw "Invalid argument(s): 120.0".
    for (final width in <double>[320, 375, 393, 430, 440]) {
      final max = SaturnMetrics.sidebarMaxWidth(width);
      expect(max, greaterThanOrEqualTo(SaturnMetrics.sidebarMinWidth),
          reason: 'width $width gives max $max, below the minimum');
      // The clamp the rail actually performs must not throw.
      expect(() => 150.0.clamp(SaturnMetrics.sidebarMinWidth, max),
          returnsNormally);
    }
  });

  test('a wide screen gets a third, not the floor', () {
    expect(SaturnMetrics.sidebarMaxWidth(1024), closeTo(341.33, 0.01));
  });

  test('the cap leaves room for the longest label on the narrowest phone', () {
    // "Compliance" needs about 122pt with the icon column and padding. A
    // quarter of a 320pt phone is 80 and the floor made that 120 -- still
    // short, so the label rendered as "Complian...". On every screen, and so
    // in every store screenshot.
    const longestLabelNeeds = 122.0;
    for (final width in <double>[320, 375, 393, 430, 440]) {
      expect(SaturnMetrics.sidebarMaxWidth(width),
          greaterThanOrEqualTo(longestLabelNeeds),
          reason: 'width $width cannot fit the longest rail label');
    }
  });
}
