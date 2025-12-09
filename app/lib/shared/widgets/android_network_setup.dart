// AndroidNetworkSetup — виджет для настройки P2P на Android

import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/providers/network_providers.dart';
import '../../core/services/android_network_service.dart';

/// Виджет для настройки Android-специфичных параметров P2P.
/// 
/// Показывает:
/// - Статус foreground service
/// - Статус WiFi соединения
/// - Настройку оптимизации батареи
class AndroidNetworkSetup extends ConsumerWidget {
  const AndroidNetworkSetup({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // На не-Android платформах не показываем
    if (!Platform.isAndroid) return const SizedBox.shrink();
    
    final statusAsync = ref.watch(androidNetworkStatusProvider);
    final theme = Theme.of(context);
    
    return statusAsync.when(
      data: (status) => _StatusCard(status: status),
      loading: () => const Card(
        child: Padding(
          padding: EdgeInsets.all(16),
          child: Center(child: CircularProgressIndicator()),
        ),
      ),
      error: (e, _) => Card(
        color: theme.colorScheme.errorContainer,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Text('Ошибка: $e'),
        ),
      ),
    );
  }
}

class _StatusCard extends ConsumerWidget {
  final AndroidNetworkStatus status;
  
  const _StatusCard({required this.status});
  
  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    final service = ref.watch(androidNetworkServiceProvider);
    
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(
                  Icons.android,
                  color: theme.colorScheme.primary,
                ),
                const SizedBox(width: 8),
                Text(
                  'Android P2P',
                  style: theme.textTheme.titleMedium,
                ),
              ],
            ),
            const SizedBox(height: 16),
            
            // Service status
            _StatusRow(
              icon: Icons.play_circle_outline,
              label: 'Фоновый сервис',
              isOk: status.isServiceRunning,
              okText: 'Запущен',
              badText: 'Остановлен',
            ),
            const SizedBox(height: 8),
            
            // WiFi status
            _StatusRow(
              icon: Icons.wifi,
              label: 'WiFi',
              isOk: status.isWifiConnected,
              okText: 'Подключен',
              badText: 'Не подключен',
            ),
            const SizedBox(height: 8),
            
            // Battery optimization
            _StatusRow(
              icon: Icons.battery_saver,
              label: 'Оптимизация батареи',
              isOk: status.isBatteryOptimizationExempt,
              okText: 'Отключена',
              badText: 'Активна (может мешать)',
              warning: true,
            ),
            
            // Actions
            if (!status.isBatteryOptimizationExempt) ...[
              const SizedBox(height: 16),
              SizedBox(
                width: double.infinity,
                child: OutlinedButton.icon(
                  onPressed: () async {
                    await service.requestBatteryOptimizationExemption();
                    // Обновить статус
                    ref.invalidate(androidNetworkStatusProvider);
                  },
                  icon: const Icon(Icons.settings),
                  label: const Text('Отключить оптимизацию'),
                ),
              ),
              const SizedBox(height: 8),
              Text(
                'Это позволит FamilyVault работать в фоне без ограничений',
                style: theme.textTheme.bodySmall?.copyWith(
                  color: theme.colorScheme.outline,
                ),
              ),
            ],
            
            if (!status.isServiceRunning) ...[
              const SizedBox(height: 16),
              SizedBox(
                width: double.infinity,
                child: FilledButton.icon(
                  onPressed: () async {
                    await service.startService();
                    ref.invalidate(androidNetworkStatusProvider);
                  },
                  icon: const Icon(Icons.play_arrow),
                  label: const Text('Запустить сервис'),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _StatusRow extends StatelessWidget {
  final IconData icon;
  final String label;
  final bool isOk;
  final String okText;
  final String badText;
  final bool warning;
  
  const _StatusRow({
    required this.icon,
    required this.label,
    required this.isOk,
    required this.okText,
    required this.badText,
    this.warning = false,
  });
  
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final statusColor = isOk 
        ? Colors.green 
        : (warning ? Colors.orange : Colors.red);
    
    return Row(
      children: [
        Icon(icon, size: 20, color: theme.colorScheme.outline),
        const SizedBox(width: 12),
        Expanded(
          child: Text(label, style: theme.textTheme.bodyMedium),
        ),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
          decoration: BoxDecoration(
            color: statusColor.withOpacity(0.1),
            borderRadius: BorderRadius.circular(12),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                isOk ? Icons.check_circle : (warning ? Icons.warning : Icons.cancel),
                size: 14,
                color: statusColor,
              ),
              const SizedBox(width: 4),
              Text(
                isOk ? okText : badText,
                style: theme.textTheme.labelSmall?.copyWith(
                  color: statusColor,
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

/// Баннер для настройки Android (показывается если есть проблемы)
class AndroidSetupBanner extends ConsumerWidget {
  const AndroidSetupBanner({super.key});
  
  @override
  Widget build(BuildContext context, WidgetRef ref) {
    if (!Platform.isAndroid) return const SizedBox.shrink();
    
    final needsSetup = ref.watch(needsAndroidSetupProvider);
    final theme = Theme.of(context);
    
    return needsSetup.when(
      data: (needs) {
        if (!needs) return const SizedBox.shrink();
        
        return Card(
          margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          color: Colors.orange.shade50,
          child: InkWell(
            onTap: () => _showSetupDialog(context, ref),
            borderRadius: BorderRadius.circular(12),
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: Row(
                children: [
                  Icon(Icons.warning_amber, color: Colors.orange.shade700),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'Настройте P2P для Android',
                          style: theme.textTheme.bodyMedium?.copyWith(
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        Text(
                          'Отключите оптимизацию батареи',
                          style: theme.textTheme.bodySmall,
                        ),
                      ],
                    ),
                  ),
                  const Icon(Icons.chevron_right),
                ],
              ),
            ),
          ),
        );
      },
      loading: () => const SizedBox.shrink(),
      error: (_, __) => const SizedBox.shrink(),
    );
  }
  
  void _showSetupDialog(BuildContext context, WidgetRef ref) {
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Настройка P2P'),
        content: const AndroidNetworkSetup(),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Закрыть'),
          ),
        ],
      ),
    );
  }
}

