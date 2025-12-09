import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/models/models.dart';
import '../../core/providers/providers.dart';
import '../../shared/widgets/widgets.dart';
import '../../shared/utils/formatters.dart';

/// Экран дубликатов
class DuplicatesScreen extends ConsumerStatefulWidget {
  const DuplicatesScreen({super.key});

  @override
  ConsumerState<DuplicatesScreen> createState() => _DuplicatesScreenState();
}

class _DuplicatesScreenState extends ConsumerState<DuplicatesScreen>
    with SingleTickerProviderStateMixin {
  late TabController _tabController;

  @override
  void initState() {
    super.initState();
    _tabController = TabController(length: 2, vsync: this);
  }

  @override
  void dispose() {
    _tabController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final stats = ref.watch(duplicateStatsProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Duplicates'),
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: () => context.pop(),
        ),
        bottom: TabBar(
          controller: _tabController,
          tabs: const [
            Tab(text: 'Duplicates'),
            Tab(text: 'No Backup'),
          ],
        ),
      ),
      body: Column(
        children: [
          // Stats card
          stats.when(
            data: (s) => _DuplicateStatsCard(stats: s),
            loading: () => const Padding(
              padding: EdgeInsets.all(16),
              child: LinearProgressIndicator(),
            ),
            error: (_, _) => const SizedBox.shrink(),
          ),

          // Tab content
          Expanded(
            child: TabBarView(
              controller: _tabController,
              children: const [
                _DuplicatesTab(),
                _NoBackupTab(),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _DuplicateStatsCard extends StatelessWidget {
  final DuplicateStats stats;

  const _DuplicateStatsCard({required this.stats});

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.all(16),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceAround,
          children: [
            _StatItem(
              icon: Icons.content_copy,
              value: '${stats.totalGroups}',
              label: 'Groups',
            ),
            _StatItem(
              icon: Icons.file_copy,
              value: '${stats.totalDuplicates}',
              label: 'Duplicates',
            ),
            _StatItem(
              icon: Icons.savings,
              value: formatFileSize(stats.potentialSavings),
              label: 'Can Save',
              highlight: true,
            ),
          ],
        ),
      ),
    );
  }
}

class _StatItem extends StatelessWidget {
  final IconData icon;
  final String value;
  final String label;
  final bool highlight;

  const _StatItem({
    required this.icon,
    required this.value,
    required this.label,
    this.highlight = false,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Column(
      children: [
        Icon(
          icon,
          color: highlight ? colorScheme.primary : colorScheme.outline,
        ),
        const SizedBox(height: 4),
        Text(
          value,
          style: theme.textTheme.titleLarge?.copyWith(
            fontWeight: FontWeight.bold,
            color: highlight ? colorScheme.primary : null,
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

class _DuplicatesTab extends ConsumerWidget {
  const _DuplicatesTab();

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final groups = ref.watch(duplicateGroupsProvider);

    return groups.when(
      data: (list) {
        if (list.isEmpty) {
          return EmptyState.noDuplicates();
        }

        return ListView.builder(
          padding: const EdgeInsets.all(16),
          itemCount: list.length,
          itemBuilder: (context, index) {
            final group = list[index];
            return _DuplicateGroupCard(
              group: group,
              onDelete: (fileId) {
                ref.read(duplicateServiceProvider).deleteFile(fileId);
                ref.invalidate(duplicateGroupsProvider);
                ref.invalidate(duplicateStatsProvider);
              },
            );
          },
        );
      },
      loading: () => const LoadingIndicator(message: 'Finding duplicates...'),
      error: (e, _) => AppErrorWidget(
        message: 'Failed to find duplicates',
        details: e.toString(),
        onRetry: () => ref.invalidate(duplicateGroupsProvider),
      ),
    );
  }
}

class _DuplicateGroupCard extends StatelessWidget {
  final DuplicateGroup group;
  final void Function(int fileId) onDelete;

  const _DuplicateGroupCard({
    required this.group,
    required this.onDelete,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;
    final first = group.firstFile;

    if (first == null) return const SizedBox.shrink();

    return Card(
      margin: const EdgeInsets.only(bottom: 16),
      child: ExpansionTile(
        leading: Container(
          width: 48,
          height: 48,
          decoration: BoxDecoration(
            color: colorScheme.errorContainer,
            borderRadius: BorderRadius.circular(8),
          ),
          child: Center(
            child: Text(
              '${group.localCount}',
              style: theme.textTheme.titleLarge?.copyWith(
                color: colorScheme.error,
                fontWeight: FontWeight.bold,
              ),
            ),
          ),
        ),
        title: Text(
          first.name,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
        ),
        subtitle: Text(
          '${formatFileSize(group.fileSize)} • Can save ${formatFileSize(group.potentialSavings)}',
        ),
        children: [
          ...group.localCopies.map((file) => ListTile(
                dense: true,
                leading: Icon(
                  file == first ? Icons.star : Icons.file_copy,
                  color: file == first ? Colors.amber : null,
                ),
                title: Text(
                  file.relativePath,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                ),
                subtitle: Text(formatDate(file.modifiedAt)),
                trailing: file != first
                    ? IconButton(
                        icon: Icon(Icons.delete, color: colorScheme.error),
                        onPressed: () => onDelete(file.id),
                      )
                    : null,
              )),
          if (group.hasRemoteBackup)
            ListTile(
              dense: true,
              leading: const Icon(Icons.cloud, color: Colors.green),
              title: Text('${group.remoteCount} backup(s) on other devices'),
            ),
        ],
      ),
    );
  }
}

class _NoBackupTab extends ConsumerWidget {
  const _NoBackupTab();

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final files = ref.watch(filesWithoutBackupProvider);

    return files.when(
      data: (list) {
        if (list.isEmpty) {
          return const EmptyState(
            icon: Icons.verified_user,
            title: 'All files backed up',
            subtitle: 'Every file has at least one copy on another device',
          );
        }

        return ListView.builder(
          padding: const EdgeInsets.all(16),
          itemCount: list.length,
          itemBuilder: (context, index) {
            final file = list[index];
            return Card(
              margin: const EdgeInsets.only(bottom: 8),
              child: ListTile(
                leading: const Icon(Icons.warning_amber, color: Colors.orange),
                title: Text(
                  file.name,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                ),
                subtitle: Text(formatFileSize(file.size)),
                trailing: const Icon(Icons.chevron_right),
                onTap: () => context.push('/file/${file.id}'),
              ),
            );
          },
        );
      },
      loading: () => const LoadingIndicator(message: 'Checking backups...'),
      error: (e, _) => AppErrorWidget(
        message: 'Failed to check backups',
        details: e.toString(),
        onRetry: () => ref.invalidate(filesWithoutBackupProvider),
      ),
    );
  }
}

