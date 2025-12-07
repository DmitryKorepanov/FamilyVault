import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../app.dart';
import '../../core/models/models.dart';
import '../../core/providers/providers.dart';
import '../../shared/widgets/widgets.dart';
import '../../shared/utils/formatters.dart';

/// Экран управления тегами
class TagsScreen extends ConsumerWidget {
  const TagsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final allTags = ref.watch(allTagsProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Tags'),
      ),
      body: allTags.when(
        data: (tags) {
          if (tags.isEmpty) {
            return const EmptyState(
              icon: Icons.label_off,
              title: 'No tags yet',
              subtitle: 'Tags will appear here as you add them to files',
            );
          }

          return ListView.builder(
            padding: const EdgeInsets.all(16),
            itemCount: tags.length,
            itemBuilder: (context, index) {
              final tag = tags[index];
              return _TagTile(
                tag: tag,
                onTap: () => context.go(AppRoutes.home), // TODO: filter by tag
              );
            },
          );
        },
        loading: () => const LoadingIndicator(message: 'Loading tags...'),
        error: (e, _) => AppErrorWidget(
          message: 'Failed to load tags',
          details: e.toString(),
          onRetry: () => ref.invalidate(allTagsProvider),
        ),
      ),
    );
  }
}

class _TagTile extends StatelessWidget {
  final Tag tag;
  final VoidCallback onTap;

  const _TagTile({
    required this.tag,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: ListTile(
        leading: Container(
          width: 40,
          height: 40,
          decoration: BoxDecoration(
            color: colorScheme.primaryContainer,
            borderRadius: BorderRadius.circular(8),
          ),
          child: Icon(
            _getTagIcon(tag),
            color: colorScheme.primary,
          ),
        ),
        title: Text(
          tag.name,
          style: theme.textTheme.titleMedium,
        ),
        subtitle: Text(
          formatFileCount(tag.fileCount),
          style: theme.textTheme.bodySmall?.copyWith(
            color: colorScheme.outline,
          ),
        ),
        trailing: const Icon(Icons.chevron_right),
        onTap: onTap,
      ),
    );
  }

  IconData _getTagIcon(Tag tag) {
    switch (tag.source) {
      case TagSource.user:
        return Icons.label;
      case TagSource.auto:
        return Icons.auto_awesome;
      case TagSource.ai:
        return Icons.psychology;
    }
  }
}

