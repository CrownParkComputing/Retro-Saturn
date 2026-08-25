// picture_mapping.dart — where a pointer landed, in framebuffer pixels.
//
// FramebufferView draws the Saturn picture with BoxFit.contain, so on any
// panel whose aspect differs from the emulated mode the picture is letterboxed
// and its top-left is NOT the widget's top-left. An overlay that maps touches
// across its own full size therefore aims high or wide by the size of the
// bars -- which is every phone, because the Saturn is 4:3 and the phone is not.
//
// This file is deliberately pure: no widgets, no core handle. The mapping is
// the part that is easy to get wrong and impossible to eyeball on a device, so
// it is a function with a test rather than arithmetic inline in a gesture
// handler.

import 'dart:math' as math;
import 'dart:ui' show Offset, Rect, Size;

/// The Saturn's own coordinate for "the gun is not pointed at the screen".
const int kGunOffScreen = 0xFFFF;

/// The rectangle the picture actually occupies inside [widget], matching
/// `BoxFit.contain`: scaled up to the largest size that fits, centred, with
/// the remainder as bars.
Rect pictureRect(Size widget, int fbWidth, int fbHeight) {
  if (widget.width <= 0 ||
      widget.height <= 0 ||
      fbWidth <= 0 ||
      fbHeight <= 0) {
    return Rect.zero;
  }
  final scale = math.min(widget.width / fbWidth, widget.height / fbHeight);
  final w = fbWidth * scale;
  final h = fbHeight * scale;
  return Rect.fromLTWH((widget.width - w) / 2, (widget.height - h) / 2, w, h);
}

/// A point in framebuffer pixels.
class FbPoint {
  final int x;
  final int y;
  const FbPoint(this.x, this.y);

  @override
  bool operator ==(Object other) =>
      other is FbPoint && other.x == x && other.y == y;

  @override
  int get hashCode => Object.hash(x, y);

  @override
  String toString() => 'FbPoint($x, $y)';
}

/// Maps a pointer position in widget-local coordinates to framebuffer pixels,
/// or null when it landed on a letterbox bar rather than on the picture.
///
/// Null is meaningful, not an error: a gun aimed at the bar is aimed off
/// screen, which is exactly how a Saturn game is told to reload.
FbPoint? mapToFramebuffer(
  Offset local,
  Size widget,
  int fbWidth,
  int fbHeight,
) {
  final rect = pictureRect(widget, fbWidth, fbHeight);
  if (rect.isEmpty) return null;
  if (!rect.contains(local)) return null;

  final fx = (local.dx - rect.left) / rect.width * fbWidth;
  final fy = (local.dy - rect.top) / rect.height * fbHeight;

  // Clamped to [1, fb-1]: 0 reads as an edge case in some titles and 0xFFFF
  // is the off-screen sentinel, so the usable range excludes both ends.
  return FbPoint(
    fx.floor().clamp(1, fbWidth - 1),
    fy.floor().clamp(1, fbHeight - 1),
  );
}
