// category.dart — Workbench sidebar categories. Mirrors
// ViceMultiplatform's WorkbenchCategory enum (same emoji + label
// order) so the two multiplatform FE shells look related.

enum WorkbenchCategory {
  games('🎮', 'Games'),
  paths('📂', 'Paths'),
  input('🕹️', 'Input'),
  history('📜', 'Memories'),
  logs('📝', 'Logs'),
  about('ℹ️', 'About');

  final String icon;
  final String title;
  const WorkbenchCategory(this.icon, this.title);

  String get label => '$icon $title';
}

bool isLibraryCategory(WorkbenchCategory cat) =>
    cat == WorkbenchCategory.games;