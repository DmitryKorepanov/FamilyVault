import 'package:flutter/material.dart';

import '../../core/models/models.dart';
import '../utils/formatters.dart';
import '../utils/file_icons.dart';
import 'loading_indicator.dart';

/// Карточка статистики
class StatsCard extends StatelessWidget {
  final IndexStats stats;
  final VoidCallback? onTap;

  const StatsCard({
    super.key,
    required this.stats,
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(
                    Icons.analytics_outlined,
                    color: colorScheme.primary,
                  ),
                  const SizedBox(width: 8),
                  Text(
                    'Library',
                    style: theme.textTheme.titleMedium?.copyWith(
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 16),
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceAround,
                children: [
                  _StatItem(
                    label: 'Files',
                    value: formatNumber(stats.totalFiles),
                    icon: Icons.insert_drive_file,
                  ),
                  _StatItem(
                    label: 'Folders',
                    value: formatNumber(stats.totalFolders),
                    icon: Icons.folder,
                  ),
                  _StatItem(
                    label: 'Size',
                    value: formatFileSize(stats.totalSize),
                    icon: Icons.storage,
                  ),
                ],
              ),
              if (stats.totalFiles > 0) ...[
                const Divider(height: 32),
                _TypeBreakdown(stats: stats),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

class _StatItem extends StatelessWidget {
  final String label;
  final String value;
  final IconData icon;

  const _StatItem({
    required this.label,
    required this.value,
    required this.icon,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Column(
      children: [
        Icon(icon, color: colorScheme.outline, size: 24),
        const SizedBox(height: 4),
        Text(
          value,
          style: theme.textTheme.titleLarge?.copyWith(
            fontWeight: FontWeight.bold,
          ),
        ),
        Text(
          label,
          style: theme.textTheme.bodySmall?.copyWith(
            color: colorScheme.outline,
          ),
        ),
      ],
    );
  }
}

class _TypeBreakdown extends StatelessWidget {
  final IndexStats stats;

  const _TypeBreakdown({required this.stats});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final isDark = theme.brightness == Brightness.dark;

    final types = [
      if (stats.imageCount > 0)
        (ContentType.image, stats.imageCount),
      if (stats.videoCount > 0)
        (ContentType.video, stats.videoCount),
      if (stats.audioCount > 0)
        (ContentType.audio, stats.audioCount),
      if (stats.documentCount > 0)
        (ContentType.document, stats.documentCount),
      if (stats.archiveCount > 0)
        (ContentType.archive, stats.archiveCount),
      if (stats.otherCount > 0)
        (ContentType.other, stats.otherCount),
    ];

    return Wrap(
      spacing: 8,
      runSpacing: 8,
      children: types.map((e) {
        final (type, count) = e;
        return Chip(
          avatar: Icon(
            getContentTypeIcon(type),
            size: 18,
            color: getContentTypeColor(type, dark: isDark),
          ),
          label: Text('$count ${type.displayName}'),
          visualDensity: VisualDensity.compact,
        );
      }).toList(),
    );
  }
}

/// Скелетон для карточки статистики
class StatsCardSkeleton extends StatelessWidget {
  const StatsCardSkeleton({super.key});

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                ShimmerBox(width: 24, height: 24),
                const SizedBox(width: 8),
                ShimmerBox(width: 80, height: 20),
              ],
            ),
            const SizedBox(height: 16),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceAround,
              children: [
                Column(
                  children: [
                    ShimmerBox(width: 24, height: 24),
                    const SizedBox(height: 4),
                    ShimmerBox(width: 48, height: 24),
                    const SizedBox(height: 4),
                    ShimmerBox(width: 40, height: 12),
                  ],
                ),
                Column(
                  children: [
                    ShimmerBox(width: 24, height: 24),
                    const SizedBox(height: 4),
                    ShimmerBox(width: 48, height: 24),
                    const SizedBox(height: 4),
                    ShimmerBox(width: 40, height: 12),
                  ],
                ),
                Column(
                  children: [
                    ShimmerBox(width: 24, height: 24),
                    const SizedBox(height: 4),
                    ShimmerBox(width: 48, height: 24),
                    const SizedBox(height: 4),
                    ShimmerBox(width: 40, height: 12),
                  ],
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

