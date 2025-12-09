import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/models/models.dart';
import '../../core/providers/providers.dart';
import '../../shared/utils/formatters.dart';

/// Статус-бар индексирования — показывается внизу экрана
class IndexStatusBar extends ConsumerWidget {
  const IndexStatusBar({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;
    final stats = ref.watch(indexStatsProvider);
    final pendingContent = ref.watch(pendingContentCountProvider);
    final scanningFolderId = ref.watch(scanningFolderIdProvider);
    final isScanning = scanningFolderId != null;

    return Container(
      decoration: BoxDecoration(
        color: colorScheme.surfaceContainer,
        border: Border(
          top: BorderSide(
            color: colorScheme.outlineVariant.withValues(alpha: 0.5),
            width: 1,
          ),
        ),
      ),
      child: SafeArea(
        top: false,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          child: Row(
            children: [
              // Статистика
              stats.when(
                data: (s) => _StatsSection(stats: s),
                loading: () => const _StatsPlaceholder(),
                error: (_, _) => const SizedBox.shrink(),
              ),

              const Spacer(),

              // Прогресс индексирования
              _IndexingProgress(
                isScanning: isScanning,
                pendingContent: pendingContent,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _StatsSection extends StatelessWidget {
  final IndexStats stats;

  const _StatsSection({required this.stats});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        _StatItem(
          icon: Icons.folder_outlined,
          value: '${stats.totalFolders}',
          label: 'folders',
          color: colorScheme.primary,
        ),
        const SizedBox(width: 20),
        _StatItem(
          icon: Icons.insert_drive_file_outlined,
          value: formatCompactNumber(stats.totalFiles),
          label: 'files',
          color: colorScheme.secondary,
        ),
        const SizedBox(width: 20),
        _StatItem(
          icon: Icons.storage_outlined,
          value: formatFileSize(stats.totalSize),
          label: '',
          color: colorScheme.tertiary,
        ),
      ],
    );
  }
}

class _StatItem extends StatelessWidget {
  final IconData icon;
  final String value;
  final String label;
  final Color color;

  const _StatItem({
    required this.icon,
    required this.value,
    required this.label,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: 16, color: color.withValues(alpha: 0.8)),
        const SizedBox(width: 6),
        Text(
          value,
          style: theme.textTheme.bodySmall?.copyWith(
            fontWeight: FontWeight.w600,
            color: colorScheme.onSurface,
          ),
        ),
        if (label.isNotEmpty) ...[
          const SizedBox(width: 3),
          Text(
            label,
            style: theme.textTheme.bodySmall?.copyWith(
              color: colorScheme.outline,
            ),
          ),
        ],
      ],
    );
  }
}

class _StatsPlaceholder extends StatelessWidget {
  const _StatsPlaceholder();

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;

    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 80,
          height: 16,
          decoration: BoxDecoration(
            color: colorScheme.outlineVariant.withValues(alpha: 0.3),
            borderRadius: BorderRadius.circular(4),
          ),
        ),
        const SizedBox(width: 16),
        Container(
          width: 60,
          height: 16,
          decoration: BoxDecoration(
            color: colorScheme.outlineVariant.withValues(alpha: 0.3),
            borderRadius: BorderRadius.circular(4),
          ),
        ),
      ],
    );
  }
}

class _IndexingProgress extends StatelessWidget {
  final bool isScanning;
  final AsyncValue<int> pendingContent;

  const _IndexingProgress({
    required this.isScanning,
    required this.pendingContent,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    final pendingCount = pendingContent.valueOrNull ?? 0;
    final hasWork = isScanning || pendingCount > 0;

    if (!hasWork) {
      // Показываем "всё готово" или пусто
      return Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            Icons.check_circle_outline,
            size: 16,
            color: colorScheme.outline.withValues(alpha: 0.6),
          ),
          const SizedBox(width: 6),
          Text(
            'Ready',
            style: theme.textTheme.bodySmall?.copyWith(
              color: colorScheme.outline,
            ),
          ),
        ],
      );
    }

    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        // Animated progress indicator
        SizedBox(
          width: 14,
          height: 14,
          child: CircularProgressIndicator(
            strokeWidth: 2,
            color: colorScheme.primary,
          ),
        ),
        const SizedBox(width: 8),
        Text(
          isScanning
              ? 'Scanning...'
              : pendingCount > 0
                  ? 'Indexing content: $pendingCount'
                  : '',
          style: theme.textTheme.bodySmall?.copyWith(
            color: colorScheme.primary,
            fontWeight: FontWeight.w500,
          ),
        ),
      ],
    );
  }
}

