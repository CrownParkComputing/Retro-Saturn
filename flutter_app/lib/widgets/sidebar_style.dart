import 'package:retro_saturn/widgets/sidebar.dart';
import 'package:retro_saturn/theme/saturn_theme.dart';

/// The Saturn front end's rail palette. This adapter is the only per-app part
/// of the side nav -- widgets/sidebar.dart itself is identical in every
/// Retro-* app, so a fix there lands everywhere instead of once.
const SidebarStyle saturnSidebarStyle = SidebarStyle(
  panelFill: SaturnColors.panelFill,
  panelStroke: SaturnColors.panelStroke,
  selectedFill: SaturnColors.selectedFill,
  selectedStroke: SaturnColors.selectedBorder,
  labelIdle: SaturnColors.sidebarLabelIdle,
  labelSelected: SaturnColors.sidebarLabelSelected,
  minWidth: SaturnMetrics.sidebarMinWidth,
  buttonHeight: SaturnMetrics.sidebarButtonHeight,
  buttonTextSize: SaturnMetrics.sidebarButtonTextSize,
  // Saturn's theme has no bottom-margin constant of its own; the family value
  // is 4, which is what its old inline rail spaced rows by in practice.
  buttonBottomMargin: 4.0,
  buttonSidePadding: SaturnMetrics.sidebarButtonSidePadding,
  buttonVerticalPadding: SaturnMetrics.sidebarButtonVerticalPadding,
  navPadding: SaturnMetrics.sideNavPadding,
  maxWidth: SaturnMetrics.sidebarMaxWidth,
);
