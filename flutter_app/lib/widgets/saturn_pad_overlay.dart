// saturn_pad_overlay.dart — The on-screen Saturn pad: d-pad, the six face
// buttons + Start, and the L/R triggers, each cluster movable in layout
// mode and remembered.
//
// The layout model is the family's (Retro-C64 originated it): positions are
// fractions of the play area, persisted via AppPrefs.setControlPosition, and
// the clusters are only draggable while [editing] is true -- an always-
// draggable pad meant a pan across the d-pad moved the cluster instead of
// pressing it, which read as the pad not working at all.

import 'package:flutter/material.dart';
import 'package:retro_saturn/ffi/ymir_bindings.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';
import 'package:retro_saturn/services/app_prefs.dart';
import 'package:retro_saturn/widgets/dpad_view.dart';
import 'package:retro_saturn/widgets/movable_control.dart';

class SaturnPadOverlay extends StatefulWidget {
  final YmirCore core;
  final int port;

  /// Layout mode: clusters drag instead of press, and moves persist.
  final bool editing;

  const SaturnPadOverlay({
    super.key,
    required this.core,
    this.port = 1,
    this.editing = false,
  });

  @override
  State<SaturnPadOverlay> createState() => _SaturnPadOverlayState();
}

class _SaturnPadOverlayState extends State<SaturnPadOverlay> {
  static const _dpadId = 'dpad';
  static const _faceId = 'face';
  static const _triggersId = 'triggers';

  /// Centre of each cluster as fractions of the play area. The defaults put
  /// the d-pad under the left thumb, the face buttons under the right, and
  /// the triggers up out of the way -- the way the real pad sits in two
  /// hands.
  Map<String, Offset> _positions = const {
    _dpadId: Offset(0.14, 0.72),
    _faceId: Offset(0.84, 0.72),
    _triggersId: Offset(0.50, 0.92),
  };

  final Set<int> _pressed = {};

  @override
  void initState() {
    super.initState();
    AppPrefs.getControlPositions().then((stored) {
      if (!mounted || stored.isEmpty) return;
      setState(() => _positions = {..._positions, ...stored});
    });
  }

  @override
  void dispose() {
    // An overlay torn down mid-press (pad hidden, session closed) must not
    // leave a button held in the core.
    for (final value in _pressed) {
      for (final b in YmirButton.values) {
        if (b.value == value) widget.core.setPadButton(widget.port, b, false);
      }
    }
    super.dispose();
  }

  void _press(YmirButton b, bool down) {
    if (down) {
      _pressed.add(b.value);
    } else {
      _pressed.remove(b.value);
    }
    widget.core.setPadButton(widget.port, b, down);
    setState(() {});
  }

  void _directions(bool up, bool down, bool left, bool right) {
    widget.core.setPadButton(widget.port, YmirButton.up, up);
    widget.core.setPadButton(widget.port, YmirButton.down, down);
    widget.core.setPadButton(widget.port, YmirButton.left, left);
    widget.core.setPadButton(widget.port, YmirButton.right, right);
  }

  void _moved(String id, Offset fraction) {
    setState(() => _positions = {..._positions, id: fraction});
  }

  void _moveEnd(String id) {
    final fraction = _positions[id];
    if (fraction != null) AppPrefs.setControlPosition(id, fraction);
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, constraints) {
      final area = constraints.biggest;
      return Stack(children: [
        MovableControl(
          area: area,
          fraction: _positions[_dpadId]!,
          editing: widget.editing,
          label: 'D-pad',
          onMoved: (f) => _moved(_dpadId, f),
          onMoveEnd: () => _moveEnd(_dpadId),
          child: DpadView(size: 132, onDirections: _directions),
        ),
        MovableControl(
          area: area,
          fraction: _positions[_faceId]!,
          editing: widget.editing,
          label: 'Buttons',
          onMoved: (f) => _moved(_faceId, f),
          onMoveEnd: () => _moveEnd(_faceId),
          child: _faceCluster(),
        ),
        MovableControl(
          area: area,
          fraction: _positions[_triggersId]!,
          editing: widget.editing,
          label: 'Triggers',
          onMoved: (f) => _moved(_triggersId, f),
          onMoveEnd: () => _moveEnd(_triggersId),
          child: _triggerCluster(),
        ),
      ]);
    });
  }

  /// The Saturn's face: X/Y/Z over A/B/C, Start beside them -- the real
  /// pad's arc, not a flat row.
  Widget _faceCluster() {
    return Row(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.center,
      children: [
        Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Row(mainAxisSize: MainAxisSize.min, children: [
              _padBtn(YmirButton.x, 'X', small: true),
              _padBtn(YmirButton.y, 'Y', small: true),
              _padBtn(YmirButton.z, 'Z', small: true),
            ]),
            const SizedBox(height: 4),
            Row(mainAxisSize: MainAxisSize.min, children: [
              _padBtn(YmirButton.a, 'A'),
              _padBtn(YmirButton.b, 'B'),
              _padBtn(YmirButton.c, 'C'),
            ]),
          ],
        ),
        const SizedBox(width: 8),
        _padBtn(YmirButton.start, 'START', small: true),
      ],
    );
  }

  Widget _triggerCluster() {
    return Row(mainAxisSize: MainAxisSize.min, children: [
      _padBtn(YmirButton.l, 'L'),
      const SizedBox(width: 24),
      _padBtn(YmirButton.r, 'R'),
    ]);
  }

  Widget _padBtn(YmirButton b, String label, {bool small = false}) {
    final isDown = _pressed.contains(b.value);
    final size = small ? 36.0 : 46.0;
    return Listener(
      onPointerDown: (_) => _press(b, true),
      onPointerUp: (_) => _press(b, false),
      onPointerCancel: (_) => _press(b, false),
      child: Container(
        margin: const EdgeInsets.all(2),
        width: label == 'START' ? 56 : size,
        height: label == 'START' ? 24 : size,
        decoration: BoxDecoration(
          color: isDown ? Colors.white38 : Colors.black38,
          shape: label == 'START' ? BoxShape.rectangle : BoxShape.circle,
          borderRadius:
              label == 'START' ? BorderRadius.circular(12) : null,
          border: Border.all(color: Colors.white54),
        ),
        child: Center(
          child: Text(
            label,
            style: TextStyle(
              color: Colors.white,
              fontSize: small ? 11 : 15,
              fontWeight: FontWeight.bold,
            ),
          ),
        ),
      ),
    );
  }
}
