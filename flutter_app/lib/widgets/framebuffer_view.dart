// framebuffer_view.dart — Poll YmirCoreBindings.getFramebuffer every
// 33 ms and decode the XRGB8888 framebuffer into a ui.Image, drawn
// via CustomPaint. Ported verbatim from ViceMultiplatform's pattern.
//
// Saturn variable resolutions (320x224 NTSC, 352x224, 704x512 hi-res)
// are handled by re-creating the ui.Image when w/h change.

import 'dart:async';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:retro_saturn/ffi/ymir_core.dart';

class FramebufferView extends StatefulWidget {
  final YmirCore core;
  final Duration pollInterval;

  /// Published whenever the emulated resolution changes, so overlays that
  /// map pointer coordinates into framebuffer space (the Virtua Gun) follow
  /// the Saturn from 320x224 to 704x512 without re-reading -- and copying --
  /// a whole frame of their own.
  final ValueNotifier<Size>? frameSize;

  /// Stretch the picture to fill the view instead of keeping the Saturn's
  /// shape. A 4:3 machine on a widescreen handheld leaves black bars either
  /// side; which annoyance you prefer is a matter of taste, so it is a
  /// toggle rather than a decision made for the user.
  final bool fillScreen;
  /// Draws the PANEL's redraw rate over the picture.
  ///
  /// Note this is not the core's frame rate: it counts how often this widget
  /// pulled and decoded a framebuffer, which is bounded by [pollInterval].
  /// The status row reports the core's own figure, so showing both means two
  /// readouts labelled FPS that disagree -- off by default for that reason.
  final bool showFps;

  const FramebufferView({
    super.key,
    required this.core,
    this.frameSize,
    // ~60fps, matching the rate the core actually produces and the interval
    // Retro-Dosbox uses. At 33ms this timer could tick only 30 times a
    // second, so a Saturn running at a solid 60 was displayed at half that
    // no matter how fast the emulation went -- and the panel's own counter
    // read 30 by construction, which is not a measurement, it is the cap.
    this.pollInterval = const Duration(milliseconds: 16),
    this.showFps = false,
      this.fillScreen = false,
  });

  @override
  State<FramebufferView> createState() => _FramebufferViewState();
}

class _FramebufferViewState extends State<FramebufferView> {
  ui.Image? _image;
  int _imageW = 0;
  int _imageH = 0;
  int _fps = 0;
  Timer? _timer;
  int _frames = 0;
  DateTime _lastFpsUpdate = DateTime.now();

  @override
  void initState() {
    super.initState();
    _timer = Timer.periodic(widget.pollInterval, (_) => _tick());
  }

  @override
  void dispose() {
    _timer?.cancel();
    _image?.dispose();
    super.dispose();
  }

  Future<void> _tick() async {
    // Nothing is being emulated while paused, so there is no new frame to
    // fetch -- and fetching one is not free: core.framebuffer copies the whole
    // buffer out of native memory and decodeImageFromPixels re-uploads it,
    // sixty times a second. Left running with the app in the background that
    // was most of the CPU the app was still burning after the emulation
    // itself had stopped.
    if (widget.core.presentationPaused) return;

    final snap = widget.core.framebuffer;
    if (snap == null) return;
    final newImage = await _decode(snap.argb, snap.width, snap.height);
    if (!mounted) {
      newImage.dispose();
      return;
    }
    final oldImage = _image;
    setState(() {
      _image = newImage;
      _imageW = snap.width;
      _imageH = snap.height;
    });
    oldImage?.dispose();
    final size = Size(snap.width.toDouble(), snap.height.toDouble());
    if (widget.frameSize != null && widget.frameSize!.value != size) {
      widget.frameSize!.value = size;
    }
    _frames++;
    final now = DateTime.now();
    if (now.difference(_lastFpsUpdate).inMilliseconds >= 1000) {
      _fps = (_frames * 1000 /
              now.difference(_lastFpsUpdate).inMilliseconds)
          .round();
      _frames = 0;
      _lastFpsUpdate = now;
    }
  }

  Future<ui.Image> _decode(Uint32List pixels, int w, int h) {
    final completer = Completer<ui.Image>();
    ui.decodeImageFromPixels(
      pixels.buffer.asUint8List(),
      w,
      h,
      ui.PixelFormat.rgba8888,
      completer.complete,
    );
    return completer.future;
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, constraints) {
      return Stack(
        fit: StackFit.expand,
        children: [
          if (_image == null)
            const Center(child: CircularProgressIndicator())
          else
            FittedBox(
              fit: widget.fillScreen ? BoxFit.fill : BoxFit.contain,
              child: SizedBox(
                width: _imageW.toDouble(),
                height: _imageH.toDouble(),
                child: CustomPaint(painter: _ImagePainter(_image!)),
              ),
            ),
          if (widget.showFps)
            Positioned(
              top: 8,
              right: 8,
              child: Container(
                padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                color: Colors.black54,
                child: Text('FPS $_fps',
                    style: const TextStyle(color: Colors.white, fontSize: 12)),
              ),
            ),
        ],
      );
    });
  }
}

class _ImagePainter extends CustomPainter {
  final ui.Image image;
  _ImagePainter(this.image);

  @override
  void paint(Canvas canvas, Size size) {
    final src = Rect.fromLTWH(0, 0, image.width.toDouble(), image.height.toDouble());
    final dst = Rect.fromLTWH(0, 0, size.width, size.height);
    canvas.drawImageRect(image, src, dst, Paint());
  }

  @override
  bool shouldRepaint(_ImagePainter oldDelegate) =>
      oldDelegate.image != image;
}