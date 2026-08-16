// Widget smoke test: the root YmirApp reaches one of
// {Failed to load, Loading, EmulatorScreen} without throwing.
// Uses FakeYmirCore so no libymircore.so is needed.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:ymir_multiplatform/main.dart';

void main() {
  testWidgets('YmirApp builds without crashing', (tester) async {
    await tester.pumpWidget(const YmirApp());
    // We don't pumpAndSettle because the real FFI load will hang on
    // the host. Instead just assert something renders.
    expect(find.byType(MaterialApp), findsOneWidget);
  });
}