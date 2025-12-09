// RemoteFilesScreen — список файлов с удалённых устройств

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/models/models.dart';
import '../../core/providers/network_providers.dart';
import '../../shared/utils/formatters.dart';
import '../../theme/app_theme.dart';

// ═══════════════════════════════════════════════════════════
// Remote Files Provider (async to avoid blocking build)
// ═══════════════════════════════════════════════════════════

/// Parameters for remote files query
class RemoteFilesParams {
  final String? deviceId;
  final String? searchQuery;
  
  const RemoteFilesParams({this.deviceId, this.searchQuery});
  
  @override
  bool operator ==(Object other) =>
      other is RemoteFilesParams &&
      other.deviceId == deviceId &&
      other.searchQuery == searchQuery;
  
  @override
  int get hashCode => Object.hash(deviceId, searchQuery);
}

/// Provider that fetches remote files asynchronously (off the build path)
/// Uses family to support filtering by device and search query
final remoteFilesQueryProvider = FutureProvider.family<List<RemoteFileRecord>, RemoteFilesParams>((ref, params) async {
  // Watch connected devices to auto-refresh when connections change
  ref.watch(connectedDevicesProvider);
  
  final service = ref.read(networkServiceProvider);
  
  // Yield to event loop to avoid blocking current frame
  await Future<void>.delayed(Duration.zero);
  
  if (params.searchQuery != null && params.searchQuery!.isNotEmpty) {
    return service.searchRemoteFiles(params.searchQuery!);
  } else if (params.deviceId != null) {
    return service.getRemoteFilesFrom(params.deviceId!);
  } else {
    return service.getRemoteFiles();
  }
});

/// Экран со списком файлов с удалённых устройств
class RemoteFilesScreen extends ConsumerStatefulWidget {
  final String? deviceId; // null = все устройства

  const RemoteFilesScreen({super.key, this.deviceId});

  @override
  ConsumerState<RemoteFilesScreen> createState() => _RemoteFilesScreenState();
}

class _RemoteFilesScreenState extends ConsumerState<RemoteFilesScreen> {
  String _searchQuery = '';
  ContentType? _filterType;
  bool _isRefreshing = false;
  bool _hasTriedConnect = false;

  @override
  void initState() {
    super.initState();
    // Ensure connected to device when viewing its files
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _ensureConnected();
    });
  }

  void _ensureConnected() {
    if (widget.deviceId == null || _hasTriedConnect) return;
    _hasTriedConnect = true;
    
    final service = ref.read(networkServiceProvider);
    final connectedDevices = service.getConnectedDevices();
    final isConnected = connectedDevices.any((d) => d.deviceId == widget.deviceId);
    
    if (!isConnected) {
      // Start async connection (returns immediately, result via callback)
      service.connectToDevice(widget.deviceId!);
      // UI will auto-refresh when connection completes via network event stream
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    
    // Use async provider for remote files (runs in background isolate)
    final filesAsync = ref.watch(remoteFilesQueryProvider(
      RemoteFilesParams(deviceId: widget.deviceId, searchQuery: _searchQuery.isEmpty ? null : _searchQuery),
    ));

    return Scaffold(
      appBar: AppBar(
        title: Text(widget.deviceId != null ? 'Файлы устройства' : 'Файлы семьи'),
        actions: [
          IconButton(
            icon: const Icon(Icons.filter_list),
            tooltip: 'Фильтр',
            onPressed: _showFilterDialog,
          ),
          IconButton(
            icon: const Icon(Icons.sync),
            tooltip: 'Синхронизировать',
            onPressed: _syncFiles,
          ),
        ],
        bottom: PreferredSize(
          preferredSize: const Size.fromHeight(56),
          child: Padding(
            padding: const EdgeInsets.fromLTRB(16, 0, 16, 8),
            child: TextField(
              decoration: InputDecoration(
                hintText: 'Поиск файлов...',
                prefixIcon: const Icon(Icons.search),
                suffixIcon: _searchQuery.isNotEmpty
                    ? IconButton(
                        icon: const Icon(Icons.clear),
                        onPressed: () => setState(() => _searchQuery = ''),
                      )
                    : null,
                filled: true,
                fillColor: theme.colorScheme.surfaceContainerHighest,
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(28),
                  borderSide: BorderSide.none,
                ),
                contentPadding: const EdgeInsets.symmetric(horizontal: 16),
              ),
              onChanged: (value) => setState(() => _searchQuery = value),
            ),
          ),
        ),
      ),
      body: RefreshIndicator(
        onRefresh: () async {
          await _syncFiles();
          // Invalidate provider to refetch
          ref.invalidate(remoteFilesQueryProvider(
            RemoteFilesParams(deviceId: widget.deviceId, searchQuery: _searchQuery.isEmpty ? null : _searchQuery),
          ));
        },
        child: _buildFileList(filesAsync),
      ),
    );
  }

  Widget _buildFileList(AsyncValue<List<RemoteFileRecord>> filesAsync) {
    return filesAsync.when(
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (e, st) => Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(Icons.error_outline, size: 48, color: Colors.red),
            const SizedBox(height: 16),
            Text('Ошибка загрузки: $e'),
            const SizedBox(height: 16),
            ElevatedButton(
              onPressed: () => ref.invalidate(remoteFilesQueryProvider(
                RemoteFilesParams(deviceId: widget.deviceId, searchQuery: _searchQuery.isEmpty ? null : _searchQuery),
              )),
              child: const Text('Повторить'),
            ),
          ],
        ),
      ),
      data: (files) => _buildFileListContent(files),
    );
  }
  
  Widget _buildFileListContent(List<RemoteFileRecord> files) {
    // Фильтруем по типу
    final filteredFiles = _filterType != null
        ? files.where((f) => f.contentType == _filterType).toList()
        : files;

    if (filteredFiles.isEmpty) {
      return _EmptyView(
        hasFilter: _filterType != null || _searchQuery.isNotEmpty,
        onClearFilter: () => setState(() {
          _filterType = null;
          _searchQuery = '';
        }),
      );
    }

    // Группируем по устройству если показываем все
    if (widget.deviceId == null) {
      return _GroupedFilesList(
        files: filteredFiles,
        onFileTap: _openFile,
      );
    }

    return ListView.builder(
      padding: AppSizes.contentPadding(context),
      itemCount: filteredFiles.length,
      itemBuilder: (context, index) {
        final file = filteredFiles[index];
        return _RemoteFileTile(
          file: file,
          onTap: () => _openFile(file),
        );
      },
    );
  }

  void _showFilterDialog() {
    showModalBottomSheet(
      context: context,
      builder: (context) => SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Фильтр по типу',
                style: Theme.of(context).textTheme.titleLarge,
              ),
              const SizedBox(height: 16),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  _FilterChip(
                    label: 'Все',
                    isSelected: _filterType == null,
                    onTap: () {
                      setState(() => _filterType = null);
                      Navigator.pop(context);
                    },
                  ),
                  _FilterChip(
                    label: 'Изображения',
                    icon: Icons.image,
                    isSelected: _filterType == ContentType.image,
                    onTap: () {
                      setState(() => _filterType = ContentType.image);
                      Navigator.pop(context);
                    },
                  ),
                  _FilterChip(
                    label: 'Видео',
                    icon: Icons.videocam,
                    isSelected: _filterType == ContentType.video,
                    onTap: () {
                      setState(() => _filterType = ContentType.video);
                      Navigator.pop(context);
                    },
                  ),
                  _FilterChip(
                    label: 'Аудио',
                    icon: Icons.audiotrack,
                    isSelected: _filterType == ContentType.audio,
                    onTap: () {
                      setState(() => _filterType = ContentType.audio);
                      Navigator.pop(context);
                    },
                  ),
                  _FilterChip(
                    label: 'Документы',
                    icon: Icons.description,
                    isSelected: _filterType == ContentType.document,
                    onTap: () {
                      setState(() => _filterType = ContentType.document);
                      Navigator.pop(context);
                    },
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  Future<void> _syncFiles() async {
    if (_isRefreshing) return;
    
    setState(() => _isRefreshing = true);
    
    try {
      final service = ref.read(networkServiceProvider);
      if (widget.deviceId != null) {
        service.requestSync(widget.deviceId!, fullSync: false);
      } else {
        // Синхронизируем со всеми подключёнными
        ref.read(indexSyncProvider.notifier).syncWithAllConnected();
      }
      
      // Ждём немного для обновления
      await Future.delayed(const Duration(milliseconds: 500));
    } finally {
      if (mounted) {
        setState(() => _isRefreshing = false);
      }
    }
  }

  void _openFile(RemoteFileRecord file) {
    context.push('/network/files/view', extra: file);
  }
}

/// Пустой вид
class _EmptyView extends StatelessWidget {
  final bool hasFilter;
  final VoidCallback onClearFilter;

  const _EmptyView({
    required this.hasFilter,
    required this.onClearFilter,
  });

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
              hasFilter ? Icons.filter_list_off : Icons.cloud_off,
              size: 64,
              color: theme.colorScheme.outline,
            ),
            const SizedBox(height: 16),
            Text(
              hasFilter ? 'Файлы не найдены' : 'Нет файлов',
              style: theme.textTheme.titleLarge,
            ),
            const SizedBox(height: 8),
            Text(
              hasFilter
                  ? 'Попробуйте изменить параметры поиска или фильтра'
                  : 'Синхронизируйте файлы с другими устройствами семьи',
              style: theme.textTheme.bodyMedium?.copyWith(
                color: theme.colorScheme.outline,
              ),
              textAlign: TextAlign.center,
            ),
            if (hasFilter) ...[
              const SizedBox(height: 16),
              TextButton(
                onPressed: onClearFilter,
                child: const Text('Сбросить фильтры'),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

/// Список файлов с группировкой по устройству
class _GroupedFilesList extends StatelessWidget {
  final List<RemoteFileRecord> files;
  final ValueChanged<RemoteFileRecord> onFileTap;

  const _GroupedFilesList({
    required this.files,
    required this.onFileTap,
  });

  @override
  Widget build(BuildContext context) {
    // Группируем по устройству
    final grouped = <String, List<RemoteFileRecord>>{};
    for (final file in files) {
      grouped.putIfAbsent(file.sourceDeviceId, () => []).add(file);
    }

    final deviceIds = grouped.keys.toList();

    return ListView.builder(
      padding: AppSizes.contentPadding(context),
      itemCount: deviceIds.length,
      itemBuilder: (context, index) {
        final deviceId = deviceIds[index];
        final deviceFiles = grouped[deviceId]!;
        
        return _DeviceFilesSection(
          deviceId: deviceId,
          files: deviceFiles,
          onFileTap: onFileTap,
        );
      },
    );
  }
}

/// Секция файлов устройства
class _DeviceFilesSection extends ConsumerWidget {
  final String deviceId;
  final List<RemoteFileRecord> files;
  final ValueChanged<RemoteFileRecord> onFileTap;

  const _DeviceFilesSection({
    required this.deviceId,
    required this.files,
    required this.onFileTap,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    final devices = ref.watch(discoveredDevicesProvider);
    final device = devices.where((d) => d.deviceId == deviceId).firstOrNull;
    final deviceName = device?.deviceName ?? 'Устройство';

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.symmetric(vertical: 8),
          child: Row(
            children: [
              Icon(
                device?.deviceType == DeviceType.mobile
                    ? Icons.smartphone
                    : Icons.computer,
                size: 20,
                color: theme.colorScheme.primary,
              ),
              const SizedBox(width: 8),
              Text(
                deviceName,
                style: theme.textTheme.titleMedium?.copyWith(
                  color: theme.colorScheme.primary,
                ),
              ),
              const SizedBox(width: 8),
              Text(
                '(${files.length} файлов)',
                style: theme.textTheme.bodySmall?.copyWith(
                  color: theme.colorScheme.outline,
                ),
              ),
            ],
          ),
        ),
        ...files.take(10).map((file) => _RemoteFileTile(
              file: file,
              onTap: () => onFileTap(file),
              showDevice: false,
            )),
        if (files.length > 10)
          TextButton(
            onPressed: () {
              // TODO: Navigate to device files
            },
            child: Text('Показать все ${files.length} файлов'),
          ),
        const Divider(),
      ],
    );
  }
}

/// Плитка файла
class _RemoteFileTile extends ConsumerWidget {
  final RemoteFileRecord file;
  final VoidCallback onTap;
  final bool showDevice;

  const _RemoteFileTile({
    required this.file,
    required this.onTap,
    this.showDevice = true,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    final transferState = ref.watch(fileTransferProvider);
    
    // Проверяем, есть ли файл в кэше или передаётся
    final service = ref.read(networkServiceProvider);
    final isCached = service.isFileCached(
      file.sourceDeviceId,
      file.remoteId,
      checksum: file.checksum,
    );
    final isTransferring = transferState.activeTransfers
        .any((t) => t.fileId == file.remoteId && t.isActive);

    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: Row(
            children: [
              // Иконка файла
              Container(
                width: 48,
                height: 48,
                decoration: BoxDecoration(
                  color: _getContentTypeColor(file.contentType).withOpacity(0.1),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Stack(
                  alignment: Alignment.center,
                  children: [
                    Icon(
                      _getContentTypeIcon(file.contentType),
                      color: _getContentTypeColor(file.contentType),
                      size: 24,
                    ),
                    if (isCached)
                      Positioned(
                        right: 2,
                        bottom: 2,
                        child: Container(
                          padding: const EdgeInsets.all(2),
                          decoration: BoxDecoration(
                            color: Colors.green,
                            shape: BoxShape.circle,
                          ),
                          child: const Icon(
                            Icons.check,
                            size: 10,
                            color: Colors.white,
                          ),
                        ),
                      ),
                  ],
                ),
              ),
              const SizedBox(width: 12),
              
              // Информация о файле
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      file.name,
                      style: theme.textTheme.bodyLarge,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                    ),
                    const SizedBox(height: 4),
                    Row(
                      children: [
                        Text(
                          file.formattedSize,
                          style: theme.textTheme.bodySmall?.copyWith(
                            color: theme.colorScheme.outline,
                          ),
                        ),
                        const SizedBox(width: 8),
                        Icon(
                          Icons.schedule,
                          size: 12,
                          color: theme.colorScheme.outline,
                        ),
                        const SizedBox(width: 4),
                        Text(
                          Formatters.date(file.modifiedDate),
                          style: theme.textTheme.bodySmall?.copyWith(
                            color: theme.colorScheme.outline,
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),

              // Статус / стрелка
              if (isTransferring)
                const SizedBox(
                  width: 24,
                  height: 24,
                  child: CircularProgressIndicator(strokeWidth: 2),
                )
              else if (isCached)
                Icon(
                  Icons.cloud_done,
                  color: Colors.green,
                  size: 20,
                )
              else
                Icon(
                  Icons.cloud_download,
                  color: theme.colorScheme.outline,
                  size: 20,
                ),
            ],
          ),
        ),
      ),
    );
  }

  IconData _getContentTypeIcon(ContentType type) {
    switch (type) {
      case ContentType.image:
        return Icons.image;
      case ContentType.video:
        return Icons.videocam;
      case ContentType.audio:
        return Icons.audiotrack;
      case ContentType.document:
        return Icons.description;
      case ContentType.other:
      default:
        return Icons.insert_drive_file;
    }
  }

  Color _getContentTypeColor(ContentType type) {
    switch (type) {
      case ContentType.image:
        return Colors.blue;
      case ContentType.video:
        return Colors.red;
      case ContentType.audio:
        return Colors.orange;
      case ContentType.document:
        return Colors.green;
      case ContentType.other:
      default:
        return Colors.grey;
    }
  }
}

/// Чип фильтра
class _FilterChip extends StatelessWidget {
  final String label;
  final IconData? icon;
  final bool isSelected;
  final VoidCallback onTap;

  const _FilterChip({
    required this.label,
    this.icon,
    required this.isSelected,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return FilterChip(
      label: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (icon != null) ...[
            Icon(icon, size: 18),
            const SizedBox(width: 4),
          ],
          Text(label),
        ],
      ),
      selected: isSelected,
      onSelected: (_) => onTap(),
      selectedColor: theme.colorScheme.primaryContainer,
      checkmarkColor: theme.colorScheme.primary,
    );
  }
}

