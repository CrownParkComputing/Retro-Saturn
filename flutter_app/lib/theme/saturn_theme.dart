// saturn_theme.dart — Color tokens + metrics for the ymir-multiplatform
// Flutter FE. Mirrors ViceMultiplatform's `ViceColors` so the two
// multiplatform shells (C64-Retro + ymir-android) read as sibling apps.
// Tokens lifted from the ymir-android Java UI palette.

import 'package:flutter/material.dart';

class SaturnColors {
  SaturnColors._();

  /// Root background — matches the existing dark theme.
  static const Color rootBackground = Color(0xFF050607);

  /// Sidebar + content panel background.
  static const Color panelFill = Color(0xFF101113);

  /// Sidebar / panel divider.
  static const Color panelStroke = Color(0xFF1A1F2C);

  /// Selected sidebar entry.
  static const Color selectedFill = Color(0xFF1F2632);
  static const Color selectedBorder = Color(0xFF3D8BFF);

  /// Tab badge selected.
  static const Color tabSelected = Color(0xFF3D8BFF);

  /// Idle label (sidebar entry, etc.).
  static const Color sidebarLabelIdle = Color(0xFFB9C2CE);
  static const Color sidebarLabelSelected = Colors.white;

  /// Section labels (uppercase, dim).
  static const Color sectionLabel = Color(0xFF6D7689);

  /// Subtle text — used for stat lines, format badges.
  static const Color subtleText = Color(0xFF6D7689);
}

class SaturnMetrics {
  SaturnMetrics._();

  /// Sidebar width — measured from widest entry title in
  /// WorkbenchCategory, clamped to these bounds. The Retroid Flip2's
  /// 456dp-tall landscape has plenty of room for a 180dp rail.
  static const double sidebarMinWidth = 120.0;
  static double sidebarMaxWidth(double screenWidth) =>
      screenWidth * 0.25;

  /// Inner padding of the rail container.
  static const double sideNavPadding = 4.0;

  /// Padding inside each sidebar button row.
  static const double sidebarButtonSidePadding = 10.0;
  static const double sidebarButtonVerticalPadding = 4.0;
  static const double sidebarButtonHeight = 28.0;
  static const double sidebarButtonTextSize = 12.0;

  /// Library grid card defaults.
  static const double mediaCardWidth = 140.0;
  static const double mediaCardHeight = 178.0;
  static const double mediaCardCoverHeight = 124.0;
}