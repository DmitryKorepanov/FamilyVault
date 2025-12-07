import 'package:flutter/material.dart';

/// Виджет для пустых состояний
class EmptyState extends StatelessWidget {
  final IconData icon;
  final String title;
  final String? subtitle;
  final Widget? action;
  final double iconSize;

  const EmptyState({
    super.key,
    required this.icon,
    required this.title,
    this.subtitle,
    this.action,
    this.iconSize = 80,
  });

  /// Нет файлов
  factory EmptyState.noFiles({VoidCallback? onAddFolder}) {
    return EmptyState(
      icon: Icons.folder_open,
      title: 'No files yet',
      subtitle: 'Add a folder to start indexing your files',
      action: onAddFolder != null
          ? FilledButton.icon(
              onPressed: onAddFolder,
              icon: const Icon(Icons.add),
              label: const Text('Add Folder'),
            )
          : null,
    );
  }

  /// Нет результатов поиска
  factory EmptyState.noResults({VoidCallback? onClearFilters}) {
    return EmptyState(
      icon: Icons.search_off,
      title: 'No results found',
      subtitle: 'Try different keywords or adjust filters',
      action: onClearFilters != null
          ? TextButton(
              onPressed: onClearFilters,
              child: const Text('Clear filters'),
            )
          : null,
    );
  }

  /// Нет папок
  factory EmptyState.noFolders({VoidCallback? onAddFolder}) {
    return EmptyState(
      icon: Icons.folder_off,
      title: 'No folders added',
      subtitle: 'Add folders to index and search your files',
      action: onAddFolder != null
          ? FilledButton.icon(
              onPressed: onAddFolder,
              icon: const Icon(Icons.add),
              label: const Text('Add Folder'),
            )
          : null,
    );
  }

  /// Нет тегов
  factory EmptyState.noTags() {
    return const EmptyState(
      icon: Icons.label_off,
      title: 'No tags yet',
      subtitle: 'Tags will appear here as you add them to files',
    );
  }

  /// Нет дубликатов
  factory EmptyState.noDuplicates() {
    return const EmptyState(
      icon: Icons.check_circle_outline,
      title: 'No duplicates found',
      subtitle: 'Great! Your files are unique',
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              icon,
              size: iconSize,
              color: colorScheme.outline,
            ),
            const SizedBox(height: 16),
            Text(
              title,
              style: theme.textTheme.titleLarge,
              textAlign: TextAlign.center,
            ),
            if (subtitle != null) ...[
              const SizedBox(height: 8),
              Text(
                subtitle!,
                style: theme.textTheme.bodyMedium?.copyWith(
                  color: colorScheme.outline,
                ),
                textAlign: TextAlign.center,
              ),
            ],
            if (action != null) ...[
              const SizedBox(height: 24),
              action!,
            ],
          ],
        ),
      ),
    );
  }
}

