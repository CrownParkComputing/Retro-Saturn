// The rail width feeds a clamp, and a clamp whose lower bound exceeds its
// upper one throws rather than clamping -- taking the whole workbench with it.
import 'package:flutter_test/flutter_test.dart';
import 'package:retro_saturn/theme/saturn_theme.dart';

void main() {
  test('the maximum is never below the minimum, at any iPhone width', () {
    // 440pt is the iPhone 17 Pro Max; a quarter of it is 110, under the 120
    // floor. This is the case that threw "Invalid argument(s): 120.0".
    for (final width in <double>[320, 375, 393, 430, 440]) {
      final max = SaturnMetrics.sidebarMaxWidth(width);
      expect(max, greaterThanOrEqualTo(SaturnMetrics.sidebarMinWidth),
          reason: 'width $width gives max $max, below the minimum');
      // The clamp the rail actually performs must not throw.
      expect(() => 150.0.clamp(SaturnMetrics.sidebarMinWidth, max),
          returnsNormally);
    }
  });

  test('a wide screen still gets a quarter', () {
    expect(SaturnMetrics.sidebarMaxWidth(1024), 256.0);
  });
}
