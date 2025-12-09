// SyncStatusWidget — виджет показывающий статус синхронизации

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/providers/network_providers.dart';

/// Компактная карточка статуса синхронизации (для главного экрана)
class SyncStatusCard extends ConsumerWidget {
  const SyncStatusCard({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    final service = ref.watch(networkServiceProvider);
    final isConfigured = ref.watch(isFamilyConfiguredProvider);
    final remoteCount = ref.watch(remoteFileCountProvider);
    final isSyncing = service.isSyncing();

    if (!isConfigured) return const SizedBox.shrink();

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: InkWell(
        onTap: () => context.push('/network/files'),
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: theme.colorScheme.primaryContainer,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Icon(
                  Icons.cloud,
                  color: theme.colorScheme.onPrimaryContainer,
                ),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Файлы семьи',
                      style: theme.textTheme.titleMedium,
                    ),
                    const SizedBox(height: 4),
                    Row(
                      children: [
                        if (isSyncing) ...[
                          const SizedBox(
                            width: 12,
                            height: 12,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          ),
                          const SizedBox(width: 8),
                          Text(
                            'Синхронизация...',
                            style: theme.textTheme.bodySmall?.copyWith(
                              color: theme.colorScheme.outline,
                            ),
                          ),
                        ] else ...[
                          Icon(
                            Icons.check_circle,
                            size: 12,
                            color: Colors.green,
                          ),
                          const SizedBox(width: 4),
                          Text(
                            '$remoteCount файлов',
                            style: theme.textTheme.bodySmall?.copyWith(
                              color: theme.colorScheme.outline,
                            ),
                          ),
                        ],
                      ],
                    ),
                  ],
                ),
              ),
              Icon(
                Icons.chevron_right,
                color: theme.colorScheme.outline,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Кнопка синхронизации для AppBar
class SyncButton extends ConsumerWidget {
  const SyncButton({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final service = ref.watch(networkServiceProvider);
    final isSyncing = service.isSyncing();
    final isNetworkRunning = service.isNetworkRunning();

    if (!isNetworkRunning) return const SizedBox.shrink();

    return IconButton(
      icon: isSyncing
          ? const SizedBox(
              width: 20,
              height: 20,
              child: CircularProgressIndicator(strokeWidth: 2),
            )
          : const Icon(Icons.sync),
      tooltip: 'Синхронизировать',
      onPressed: isSyncing
          ? null
          : () {
              ref.read(indexSyncProvider.notifier).syncWithAllConnected();
              ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(
                  content: Text('Синхронизация запущена'),
                  duration: Duration(seconds: 1),
                ),
              );
            },
    );
  }
}

/// Индикатор синхронизации в строке состояния
class SyncStatusIndicator extends ConsumerWidget {
  final bool compact;

  const SyncStatusIndicator({super.key, this.compact = false});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    final service = ref.watch(networkServiceProvider);
    final isSyncing = service.isSyncing();
    final remoteCount = ref.watch(remoteFileCountProvider);
    final isNetworkRunning = service.isNetworkRunning();

    if (!isNetworkRunning) return const SizedBox.shrink();

    if (compact) {
      return Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (isSyncing)
            const SizedBox(
              width: 12,
              height: 12,
              child: CircularProgressIndicator(strokeWidth: 2),
            )
          else
            Icon(
              Icons.cloud_done,
              size: 14,
              color: theme.colorScheme.outline,
            ),
          const SizedBox(width: 4),
          Text(
            '$remoteCount',
            style: theme.textTheme.bodySmall?.copyWith(
              color: theme.colorScheme.outline,
            ),
          ),
        ],
      );
    }

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: theme.colorScheme.surfaceContainerHighest,
        borderRadius: BorderRadius.circular(16),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (isSyncing) ...[
            const SizedBox(
              width: 14,
              height: 14,
              child: CircularProgressIndicator(strokeWidth: 2),
            ),
            const SizedBox(width: 6),
            Text(
              'Синхронизация...',
              style: theme.textTheme.bodySmall,
            ),
          ] else ...[
            Icon(
              Icons.cloud_done,
              size: 14,
              color: theme.colorScheme.outline,
            ),
            const SizedBox(width: 6),
            Text(
              '$remoteCount файлов семьи',
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.outline,
              ),
            ),
          ],
        ],
      ),
    );
  }
}

/// Баннер для подключения к семье (если не настроено)
class FamilySetupBanner extends ConsumerWidget {
  const FamilySetupBanner({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    final isConfigured = ref.watch(isFamilyConfiguredProvider);

    if (isConfigured) return const SizedBox.shrink();

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      color: theme.colorScheme.secondaryContainer,
      child: InkWell(
        onTap: () => context.push('/family'),
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: theme.colorScheme.secondary.withOpacity(0.2),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Icon(
                  Icons.family_restroom,
                  color: theme.colorScheme.secondary,
                ),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Настройте семью',
                      style: theme.textTheme.titleMedium?.copyWith(
                        color: theme.colorScheme.onSecondaryContainer,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      'Делитесь файлами с близкими',
                      style: theme.textTheme.bodySmall?.copyWith(
                        color: theme.colorScheme.onSecondaryContainer.withOpacity(0.8),
                      ),
                    ),
                  ],
                ),
              ),
              Icon(
                Icons.arrow_forward,
                color: theme.colorScheme.onSecondaryContainer,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

