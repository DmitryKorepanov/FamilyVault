// DevicesScreen — список устройств в сети

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/models/models.dart';
import '../../core/providers/network_providers.dart';
import '../../theme/app_theme.dart';

/// Экран со списком устройств в сети
class DevicesScreen extends ConsumerStatefulWidget {
  const DevicesScreen({super.key});

  @override
  ConsumerState<DevicesScreen> createState() => _DevicesScreenState();
}

class _DevicesScreenState extends ConsumerState<DevicesScreen> {
  @override
  void initState() {
    super.initState();
    // Запускаем discovery при входе на экран
    WidgetsBinding.instance.addPostFrameCallback((_) {
      final isConfigured = ref.read(isFamilyConfiguredProvider);
      if (isConfigured) {
        ref.read(discoveredDevicesProvider.notifier).start();
      }
    });
  }

  @override
  void dispose() {
    // Останавливаем discovery при выходе
    ref.read(discoveredDevicesProvider.notifier).stop();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final isConfigured = ref.watch(isFamilyConfiguredProvider);
    final devices = ref.watch(discoveredDevicesProvider);
    final thisDevice = ref.watch(thisDeviceInfoProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Устройства'),
        actions: [
          if (isConfigured)
            IconButton(
              icon: const Icon(Icons.refresh),
              tooltip: 'Обновить',
              onPressed: () {
                ref.read(discoveredDevicesProvider.notifier).refresh();
              },
            ),
        ],
      ),
      body: !isConfigured
          ? _NotConfiguredView()
          : devices.isEmpty
              ? _EmptyView(isSearching: true)
              : _DevicesList(
                  devices: devices,
                  thisDeviceId: thisDevice?.deviceId,
                ),
    );
  }
}

/// Вид когда семья не настроена
class _NotConfiguredView extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              Icons.family_restroom,
              size: 64,
              color: theme.colorScheme.outline,
            ),
            const SizedBox(height: 16),
            Text(
              'Семья не настроена',
              style: theme.textTheme.titleLarge,
            ),
            const SizedBox(height: 8),
            Text(
              'Создайте семью или присоединитесь к существующей, '
              'чтобы видеть устройства в сети',
              style: theme.textTheme.bodyMedium?.copyWith(
                color: theme.colorScheme.outline,
              ),
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 24),
            FilledButton.icon(
              onPressed: () {
                // TODO: Navigate to family setup
              },
              icon: const Icon(Icons.add),
              label: const Text('Настроить семью'),
            ),
          ],
        ),
      ),
    );
  }
}

/// Вид когда нет устройств
class _EmptyView extends StatelessWidget {
  final bool isSearching;

  const _EmptyView({this.isSearching = false});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (isSearching) ...[
              const SizedBox(
                width: 48,
                height: 48,
                child: CircularProgressIndicator(),
              ),
              const SizedBox(height: 24),
              Text(
                'Поиск устройств...',
                style: theme.textTheme.titleMedium,
              ),
            ] else ...[
              Icon(
                Icons.devices_other,
                size: 64,
                color: theme.colorScheme.outline,
              ),
              const SizedBox(height: 16),
              Text(
                'Устройства не найдены',
                style: theme.textTheme.titleMedium,
              ),
            ],
            const SizedBox(height: 8),
            Text(
              'Убедитесь, что другие устройства включены и '
              'подключены к той же WiFi сети',
              style: theme.textTheme.bodyMedium?.copyWith(
                color: theme.colorScheme.outline,
              ),
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}

/// Список устройств
class _DevicesList extends StatelessWidget {
  final List<DeviceInfo> devices;
  final String? thisDeviceId;

  const _DevicesList({
    required this.devices,
    this.thisDeviceId,
  });

  @override
  Widget build(BuildContext context) {
    // Сортируем: сначала онлайн, потом по имени
    final sortedDevices = List<DeviceInfo>.from(devices)
      ..sort((a, b) {
        if (a.isOnline != b.isOnline) {
          return a.isOnline ? -1 : 1;
        }
        return a.deviceName.compareTo(b.deviceName);
      });

    return ListView.builder(
      padding: AppSizes.contentPadding(context),
      itemCount: sortedDevices.length,
      itemBuilder: (context, index) {
        final device = sortedDevices[index];
        final isThis = device.deviceId == thisDeviceId;
        
        return _DeviceCard(
          device: device,
          isThisDevice: isThis,
        );
      },
    );
  }
}

/// Карточка устройства
class _DeviceCard extends ConsumerWidget {
  final DeviceInfo device;
  final bool isThisDevice;

  const _DeviceCard({
    required this.device,
    this.isThisDevice = false,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);

    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: InkWell(
        onTap: isThisDevice ? null : () => _showDeviceDetails(context, ref),
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              // Иконка устройства
              Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: device.isOnline
                      ? theme.colorScheme.primaryContainer
                      : theme.colorScheme.surfaceContainerHighest,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Icon(
                  _deviceTypeIcon(device.deviceType),
                  color: device.isOnline
                      ? theme.colorScheme.onPrimaryContainer
                      : theme.colorScheme.outline,
                ),
              ),
              const SizedBox(width: 16),

              // Информация
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        Text(
                          device.deviceName,
                          style: theme.textTheme.titleMedium,
                        ),
                        if (isThisDevice) ...[
                          const SizedBox(width: 8),
                          Container(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 8,
                              vertical: 2,
                            ),
                            decoration: BoxDecoration(
                              color: theme.colorScheme.primaryContainer,
                              borderRadius: BorderRadius.circular(4),
                            ),
                            child: Text(
                              'Это устройство',
                              style: theme.textTheme.labelSmall?.copyWith(
                                color: theme.colorScheme.onPrimaryContainer,
                              ),
                            ),
                          ),
                        ],
                      ],
                    ),
                    const SizedBox(height: 4),
                    Row(
                      children: [
                        // Статус
                        Container(
                          width: 8,
                          height: 8,
                          decoration: BoxDecoration(
                            shape: BoxShape.circle,
                            color: device.isOnline ? Colors.green : Colors.grey,
                          ),
                        ),
                        const SizedBox(width: 6),
                        Text(
                          device.isOnline ? 'В сети' : 'Не в сети',
                          style: theme.textTheme.bodySmall?.copyWith(
                            color: theme.colorScheme.outline,
                          ),
                        ),
                        if (device.ipAddress.isNotEmpty) ...[
                          const SizedBox(width: 12),
                          Icon(
                            Icons.wifi,
                            size: 14,
                            color: theme.colorScheme.outline,
                          ),
                          const SizedBox(width: 4),
                          Text(
                            device.ipAddress,
                            style: theme.textTheme.bodySmall?.copyWith(
                              color: theme.colorScheme.outline,
                              fontFamily: 'monospace',
                            ),
                          ),
                        ],
                      ],
                    ),
                    if (device.fileCount > 0) ...[
                      const SizedBox(height: 4),
                      Text(
                        '${device.fileCount} файлов',
                        style: theme.textTheme.bodySmall?.copyWith(
                          color: theme.colorScheme.outline,
                        ),
                      ),
                    ],
                  ],
                ),
              ),

              // Стрелка
              if (!isThisDevice)
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

  IconData _deviceTypeIcon(DeviceType type) {
    switch (type) {
      case DeviceType.desktop:
        return Icons.computer;
      case DeviceType.mobile:
        return Icons.smartphone;
      case DeviceType.tablet:
        return Icons.tablet;
    }
  }

  void _connectToDevice(BuildContext context, WidgetRef ref) {
    final networkService = ref.read(networkServiceProvider);
    
    // Close the bottom sheet
    Navigator.pop(context);
    
    // Start async connection (returns immediately, result via callback)
    networkService.connectToDevice(device.deviceId);
    
    // Show snackbar - connection happens in background
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text('Подключение к ${device.deviceName}...'),
        duration: const Duration(seconds: 2),
      ),
    );
    
    // Device list will auto-refresh via provider when connection completes
  }

  void _syncWithDevice(BuildContext context, WidgetRef ref) {
    Navigator.pop(context);
    ref.read(indexSyncProvider.notifier).requestSync(device.deviceId);
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Синхронизация запущена')),
    );
  }

  void _showDeviceDetails(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);

    showModalBottomSheet(
      context: context,
      builder: (context) => SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Заголовок
              Row(
                children: [
                  Container(
                    padding: const EdgeInsets.all(12),
                    decoration: BoxDecoration(
                      color: theme.colorScheme.primaryContainer,
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: Icon(
                      _deviceTypeIcon(device.deviceType),
                      color: theme.colorScheme.onPrimaryContainer,
                    ),
                  ),
                  const SizedBox(width: 16),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          device.deviceName,
                          style: theme.textTheme.titleLarge,
                        ),
                        Text(
                          device.isOnline ? 'В сети' : 'Не в сети',
                          style: theme.textTheme.bodyMedium?.copyWith(
                            color: device.isOnline ? Colors.green : Colors.grey,
                          ),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 24),

              // Детали
              _DetailRow(
                icon: Icons.fingerprint,
                label: 'ID',
                value: device.deviceId,
              ),
              if (device.ipAddress.isNotEmpty)
                _DetailRow(
                  icon: Icons.wifi,
                  label: 'IP адрес',
                  value: '${device.ipAddress}:${device.servicePort}',
                ),
              if (device.fileCount > 0)
                _DetailRow(
                  icon: Icons.folder,
                  label: 'Файлов',
                  value: device.fileCount.toString(),
                ),

              const SizedBox(height: 24),

              // Кнопки действий
              if (device.isOnline) ...[
                // Кнопка подключения (если не подключён)
                if (!device.isConnected)
                  SizedBox(
                    width: double.infinity,
                    child: FilledButton.icon(
                      onPressed: () => _connectToDevice(context, ref),
                      icon: const Icon(Icons.link),
                      label: const Text('Подключиться'),
                    ),
                  )
                else ...[
                  // Статус подключения
                  Container(
                    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                    decoration: BoxDecoration(
                      color: Colors.green.withOpacity(0.1),
                      borderRadius: BorderRadius.circular(16),
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(Icons.check_circle, size: 16, color: Colors.green),
                        const SizedBox(width: 6),
                        Text('Подключено', style: TextStyle(color: Colors.green)),
                      ],
                    ),
                  ),
                  const SizedBox(height: 12),
                  SizedBox(
                    width: double.infinity,
                    child: FilledButton.icon(
                      onPressed: () {
                        Navigator.pop(context);
                        context.push('/network/files?deviceId=${device.deviceId}');
                      },
                      icon: const Icon(Icons.folder_open),
                      label: const Text('Просмотреть файлы'),
                    ),
                  ),
                ],
                const SizedBox(height: 8),
                SizedBox(
                  width: double.infinity,
                  child: OutlinedButton.icon(
                    onPressed: device.isConnected 
                        ? () => _syncWithDevice(context, ref)
                        : null,
                    icon: const Icon(Icons.sync),
                    label: const Text('Синхронизировать'),
                  ),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

/// Ряд с деталями
class _DetailRow extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;

  const _DetailRow({
    required this.icon,
    required this.label,
    required this.value,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Row(
        children: [
          Icon(icon, size: 18, color: theme.colorScheme.outline),
          const SizedBox(width: 12),
          Text(
            '$label:',
            style: theme.textTheme.bodyMedium?.copyWith(
              color: theme.colorScheme.outline,
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              value,
              style: theme.textTheme.bodyMedium?.copyWith(
                fontFamily: label == 'ID' || label == 'IP адрес' ? 'monospace' : null,
              ),
              overflow: TextOverflow.ellipsis,
            ),
          ),
        ],
      ),
    );
  }
}

