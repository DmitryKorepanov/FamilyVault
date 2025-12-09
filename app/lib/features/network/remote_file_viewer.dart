// RemoteFileViewer — просмотр и скачивание удалённого файла

import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:open_filex/open_filex.dart';
import 'package:share_plus/share_plus.dart';

import '../../core/models/models.dart';
import '../../core/providers/network_providers.dart';
import '../../shared/utils/formatters.dart';

/// Экран просмотра удалённого файла
class RemoteFileViewer extends ConsumerStatefulWidget {
  final RemoteFileRecord file;

  const RemoteFileViewer({super.key, required this.file});

  @override
  ConsumerState<RemoteFileViewer> createState() => _RemoteFileViewerState();
}

class _RemoteFileViewerState extends ConsumerState<RemoteFileViewer> {
  String? _requestId;
  bool _isDownloading = false;
  String? _error;

  @override
  Widget build(BuildContext context) {
    final service = ref.watch(networkServiceProvider);
    final transferState = ref.watch(fileTransferProvider);
    
    // Проверяем кэш (используем sourceDeviceId для уникальности)
    final isCached = service.isFileCached(
      widget.file.sourceDeviceId,
      widget.file.remoteId,
      checksum: widget.file.checksum,
    );
    final cachedPath = isCached 
        ? service.getCachedFilePath(widget.file.sourceDeviceId, widget.file.remoteId) 
        : null;
    
    // Проверяем передачу (используем deviceId + fileId для уникальности)
    final transfer = _requestId != null
        ? transferState.activeTransfers
            .where((t) => t.deviceId == widget.file.sourceDeviceId && 
                          t.fileId == widget.file.remoteId)
            .firstOrNull
        : null;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Файл'),
        actions: [
          if (isCached && cachedPath != null) ...[
            IconButton(
              icon: const Icon(Icons.share),
              tooltip: 'Поделиться',
              onPressed: () => _shareFile(cachedPath),
            ),
            IconButton(
              icon: const Icon(Icons.open_in_new),
              tooltip: 'Открыть',
              onPressed: () => _openFile(cachedPath),
            ),
          ],
        ],
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Превью файла
            _FilePreview(
              file: widget.file,
              cachedPath: cachedPath,
            ),
            const SizedBox(height: 24),

            // Информация о файле
            _FileInfo(file: widget.file),
            const SizedBox(height: 24),

            // Статус / Кнопка скачивания
            if (_error != null)
              _ErrorCard(
                error: _error!,
                onRetry: _downloadFile,
              )
            else if (transfer != null && transfer.isActive)
              _DownloadProgress(progress: transfer)
            else if (isCached && cachedPath != null)
              _CachedCard(
                path: cachedPath,
                onOpen: () => _openFile(cachedPath),
                onShare: () => _shareFile(cachedPath),
              )
            else
              _DownloadCard(
                file: widget.file,
                onDownload: _downloadFile,
                isLoading: _isDownloading,
              ),
          ],
        ),
      ),
    );
  }

  Future<void> _downloadFile() async {
    setState(() {
      _isDownloading = true;
      _error = null;
    });

    try {
      final service = ref.read(networkServiceProvider);
      
      // Находим устройство-источник
      final devices = service.getConnectedDevices();
      final sourceDevice = devices.where((d) => d.deviceId == widget.file.sourceDeviceId).firstOrNull;
      
      if (sourceDevice == null) {
        setState(() {
          _error = 'Устройство-источник не подключено';
          _isDownloading = false;
        });
        return;
      }

      final result = service.requestRemoteFile(
        deviceId: widget.file.sourceDeviceId,
        fileId: widget.file.remoteId,
        fileName: widget.file.name,
        expectedSize: widget.file.size,
        checksum: widget.file.checksum,
      );

      if (result == null) {
        setState(() {
          _error = 'Не удалось запросить файл';
          _isDownloading = false;
        });
        return;
      }

      if (result.isCached) {
        // Файл уже есть в кэше - можно сразу открыть
        setState(() {
          _isDownloading = false;
        });
        if (mounted && result.cachedPath != null) {
          _openFile(result.cachedPath!);
        }
        return;
      }

      setState(() {
        _requestId = result.requestId;
        _isDownloading = false;
      });
    } catch (e) {
      setState(() {
        _error = e.toString();
        _isDownloading = false;
      });
    }
  }

  Future<void> _openFile(String path) async {
    try {
      final result = await OpenFilex.open(path);
      if (result.type != ResultType.done) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('Не удалось открыть файл: ${result.message}')),
          );
        }
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Ошибка: $e')),
        );
      }
    }
  }

  Future<void> _shareFile(String path) async {
    try {
      await Share.shareXFiles([XFile(path)], text: widget.file.name);
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Ошибка: $e')),
        );
      }
    }
  }
}

/// Превью файла
class _FilePreview extends StatelessWidget {
  final RemoteFileRecord file;
  final String? cachedPath;

  const _FilePreview({
    required this.file,
    this.cachedPath,
  });

  @override
  Widget build(BuildContext context) {
    // Показываем изображение если есть в кэше
    if (cachedPath != null && file.contentType == ContentType.image) {
      return ClipRRect(
        borderRadius: BorderRadius.circular(12),
        child: Image.file(
          File(cachedPath!),
          width: double.infinity,
          height: 200,
          fit: BoxFit.cover,
          errorBuilder: (_, __, ___) => _PlaceholderPreview(file: file),
        ),
      );
    }

    return _PlaceholderPreview(file: file);
  }
}

/// Заглушка превью
class _PlaceholderPreview extends StatelessWidget {
  final RemoteFileRecord file;

  const _PlaceholderPreview({required this.file});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Container(
      width: double.infinity,
      height: 160,
      decoration: BoxDecoration(
        color: _getContentTypeColor(file.contentType).withOpacity(0.1),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(
            _getContentTypeIcon(file.contentType),
            size: 64,
            color: _getContentTypeColor(file.contentType),
          ),
          const SizedBox(height: 8),
          Text(
            file.extension.toUpperCase(),
            style: theme.textTheme.titleMedium?.copyWith(
              color: _getContentTypeColor(file.contentType),
            ),
          ),
        ],
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

/// Информация о файле
class _FileInfo extends StatelessWidget {
  final RemoteFileRecord file;

  const _FileInfo({required this.file});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              file.name,
              style: theme.textTheme.titleLarge,
            ),
            const SizedBox(height: 16),
            _InfoRow(
              icon: Icons.folder,
              label: 'Путь',
              value: file.path,
            ),
            _InfoRow(
              icon: Icons.straighten,
              label: 'Размер',
              value: file.formattedSize,
            ),
            _InfoRow(
              icon: Icons.category,
              label: 'Тип',
              value: file.mimeType,
            ),
            _InfoRow(
              icon: Icons.edit_calendar,
              label: 'Изменён',
              value: Formatters.dateTime(file.modifiedDate),
            ),
            _InfoRow(
              icon: Icons.sync,
              label: 'Синхронизирован',
              value: Formatters.dateTime(file.syncedDate),
            ),
          ],
        ),
      ),
    );
  }
}

/// Строка информации
class _InfoRow extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;

  const _InfoRow({
    required this.icon,
    required this.label,
    required this.value,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 18, color: theme.colorScheme.outline),
          const SizedBox(width: 12),
          SizedBox(
            width: 100,
            child: Text(
              label,
              style: theme.textTheme.bodyMedium?.copyWith(
                color: theme.colorScheme.outline,
              ),
            ),
          ),
          Expanded(
            child: Text(
              value,
              style: theme.textTheme.bodyMedium,
            ),
          ),
        ],
      ),
    );
  }
}

/// Карточка с ошибкой
class _ErrorCard extends StatelessWidget {
  final String error;
  final VoidCallback onRetry;

  const _ErrorCard({
    required this.error,
    required this.onRetry,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Card(
      color: theme.colorScheme.errorContainer,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Icon(
              Icons.error_outline,
              color: theme.colorScheme.error,
              size: 48,
            ),
            const SizedBox(height: 8),
            Text(
              error,
              style: theme.textTheme.bodyMedium?.copyWith(
                color: theme.colorScheme.onErrorContainer,
              ),
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 16),
            FilledButton.icon(
              onPressed: onRetry,
              icon: const Icon(Icons.refresh),
              label: const Text('Повторить'),
            ),
          ],
        ),
      ),
    );
  }
}

/// Карточка прогресса скачивания
class _DownloadProgress extends StatelessWidget {
  final FileTransferProgress progress;

  const _DownloadProgress({required this.progress});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Row(
              children: [
                const SizedBox(
                  width: 24,
                  height: 24,
                  child: CircularProgressIndicator(strokeWidth: 2),
                ),
                const SizedBox(width: 16),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Скачивание...',
                        style: theme.textTheme.titleMedium,
                      ),
                      Text(
                        '${progress.progressPercent}%',
                        style: theme.textTheme.bodySmall?.copyWith(
                          color: theme.colorScheme.outline,
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),
            LinearProgressIndicator(
              value: progress.progress,
              borderRadius: BorderRadius.circular(4),
            ),
          ],
        ),
      ),
    );
  }
}

/// Карточка скачанного файла
class _CachedCard extends StatelessWidget {
  final String path;
  final VoidCallback onOpen;
  final VoidCallback onShare;

  const _CachedCard({
    required this.path,
    required this.onOpen,
    required this.onShare,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Card(
      color: theme.colorScheme.primaryContainer,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Icon(
              Icons.cloud_done,
              color: theme.colorScheme.primary,
              size: 48,
            ),
            const SizedBox(height: 8),
            Text(
              'Файл скачан',
              style: theme.textTheme.titleMedium?.copyWith(
                color: theme.colorScheme.onPrimaryContainer,
              ),
            ),
            const SizedBox(height: 16),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                FilledButton.icon(
                  onPressed: onOpen,
                  icon: const Icon(Icons.open_in_new),
                  label: const Text('Открыть'),
                ),
                const SizedBox(width: 8),
                OutlinedButton.icon(
                  onPressed: onShare,
                  icon: const Icon(Icons.share),
                  label: const Text('Поделиться'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

/// Карточка скачивания
class _DownloadCard extends StatelessWidget {
  final RemoteFileRecord file;
  final VoidCallback onDownload;
  final bool isLoading;

  const _DownloadCard({
    required this.file,
    required this.onDownload,
    this.isLoading = false,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Icon(
              Icons.cloud_download,
              color: theme.colorScheme.outline,
              size: 48,
            ),
            const SizedBox(height: 8),
            Text(
              'Файл на удалённом устройстве',
              style: theme.textTheme.titleMedium,
            ),
            Text(
              file.formattedSize,
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.outline,
              ),
            ),
            const SizedBox(height: 16),
            FilledButton.icon(
              onPressed: isLoading ? null : onDownload,
              icon: isLoading
                  ? const SizedBox(
                      width: 18,
                      height: 18,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Icon(Icons.download),
              label: const Text('Скачать'),
            ),
          ],
        ),
      ),
    );
  }
}

