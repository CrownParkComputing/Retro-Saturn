// category.dart — Workbench sidebar categories. Mirrors the rail shape
// shared by Retro-C64, Retro-Amiga (uae4arm2026p) and Retro-Dosbox:
// declaration order is rail order, and [group] is the band each entry
// sits in. The shared widgets/sidebar.dart draws band separators and
// pins the last band to the bottom of the rail so "About" stays where
// "About" always is.
//
//   0  where you go        Games
//   1  how it is set up    Paths, Audio, Input
//   2  everything else     Memories, Compliance, About  (pinned to the bottom)
//
// Logs used to be its own destination; it is now a sub-section of
// About (see about_screen.dart). The runtime info that used to live
// in the sidebar footer (Core status, FPS, audio meter) is gone from
// the rail entirely -- the FPS overlay is on the framebuffer, the
// audio meter is on the in-emulator status bar, and the runtime
// diagnostics drawer is opened from the in-game toolbar's Settings
// button.
//
// Pause/resume remains a runtime feature: the in-game toolbar's Pause
// button snapshots the machine, drops the user back at the workbench
// with a "Paused -- <title> (tap to resume)" banner in the status
// bar, and the banner tap restores the snapshot. There is no Resume
// destination in the rail -- the banner is the only path back.
enum WorkbenchCategory {
  games('🎮', 'Games', 0),
  states('💾', 'States', 0),
  paths('📂', 'Paths', 1),
  core('⚙️', 'Core', 1),
  history('📜', 'Memories', 2),
  compliance('✅', 'Compliance', 2),
  about('ℹ️', 'About', 2);

  final String icon;
  final String title;
  final int group;
  const WorkbenchCategory(this.icon, this.title, this.group);

  String get label => '$icon $title';
}

bool isLibraryCategory(WorkbenchCategory cat) =>
    cat == WorkbenchCategory.games;