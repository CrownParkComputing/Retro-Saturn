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
  /// Floor for the rail.
  ///
  /// 128, not 120, because the floor has to clear the widest label the rail
  /// actually carries: "Compliance" needs about 122pt once the icon column and
  /// side padding are counted. At 120 the longest entry rendered as
  /// "Complian..." on every screen of every phone -- and so in every store
  /// screenshot -- while looking like a deliberate width.
  static const double sidebarMinWidth = 128.0;

  /// Upper bound for the measured rail, never below [sidebarMinWidth].
  ///
  /// The floor matters. A quarter of the screen is under 120 on any display
  /// narrower than 480pt, which is EVERY iPhone in portrait. Returning the
  /// bare quarter handed the rail a clamp whose lower bound was above its
  /// upper one, and `double.clamp` throws on that -- "Invalid argument(s):
  /// 120.0". The whole workbench then failed to build, which reaches a user
  /// as a red error screen in debug and a blank one in release.
  ///
  /// A quarter is a preference, not a constraint. On a narrow screen the rail
  /// takes its minimum and gives up a little more of the width, which is the
  /// intended trade: the labels stay readable either way.
  static double sidebarMaxWidth(double screenWidth) {
    // A third, not a quarter. The rail sizes itself to its widest label, and
    // "Compliance" needs about 122pt once the icon column and padding are
    // counted -- more than a quarter of any iPhone, so the cap was binding and
    // the label came out as "Complian...". It is the store-compliance page, on
    // every screenshot of every screen, which is a poor thing to have clipped.
    //
    // A third still leaves two thirds for content, and on anything wide enough
    // the cap is not reached at all: the rail takes only what its labels need.
    final share = screenWidth / 3;
    return share < sidebarMinWidth ? sidebarMinWidth : share;
  }

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