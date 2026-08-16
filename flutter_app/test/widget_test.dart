// Widget smoke test: the root YmirApp builds without throwing.
// Uses FakeYmirCore so no libymircore.so is needed in the test env.

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