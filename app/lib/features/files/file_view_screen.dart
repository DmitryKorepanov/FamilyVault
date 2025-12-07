import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/models/models.dart';
import '../../core/providers/providers.dart';
import '../../shared/widgets/widgets.dart';
import '../../shared/utils/formatters.dart';
import '../../shared/utils/file_icons.dart';

/// Экран просмотра файла
class FileViewScreen extends ConsumerWidget {
  final int fileId;

  const FileViewScreen({super.key, required this.fileId});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final file = ref.watch(fileDetailsProvider(fileId));
    final tags = ref.watch(fileTagsProvider(fileId));

    return file.when(
      data: (f) {
        if (f == null) {
          return Scaffold(
            appBar: AppBar(),
            body: const EmptyState(
              icon: Icons.error_outline,
              title: 'File not found',
              subtitle: 'This file may have been deleted or moved',
            ),
          );
        }

        return Scaffold(
          appBar: AppBar(
            title: Text(f.name),
            actions: [
              IconButton(
                icon: const Icon(Icons.share),
                onPressed: () => _shareFile(context, f),
              ),
              PopupMenuButton<String>(
                itemBuilder: (context) => [
                  const PopupMenuItem(
                    value: 'open',
                    child: ListTile(
                      leading: Icon(Icons.open_in_new),
                      title: Text('Open'),
                      contentPadding: EdgeInsets.zero,
                    ),
                  ),
                  const PopupMenuItem(
                    value: 'folder',
                    child: ListTile(
                      leading: Icon(Icons.folder_open),
                      title: Text('Show in Folder'),
                      contentPadding: EdgeInsets.zero,
                    ),
                  ),
                  if (f.isRemote)
                    const PopupMenuItem(
                      value: 'download',
                      child: ListTile(
                        leading: Icon(Icons.download),
                        title: Text('Download'),
                        contentPadding: EdgeInsets.zero,
                      ),
                    ),
                ],
                onSelected: (value) => _handleAction(context, ref, f, value),
              ),
            ],
          ),
          body: SingleChildScrollView(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                // Preview
                _FilePreview(file: f),

                // Details
                _FileDetailsCard(file: f),

                // EXIF (for images)
                if (f.imageMeta != null) _ExifCard(metadata: f.imageMeta!),

                // Tags
                tags.when(
                  data: (tagList) => _TagsCard(
                    fileId: f.id,
                    tags: tagList,
                    onAddTag: (tag) async {
                      await ref.read(tagServiceProvider).addTag(f.id, tag);
                      ref.invalidate(fileTagsProvider(f.id));
                      ref.invalidate(allTagsProvider);
                    },
                    onRemoveTag: (tag) async {
                      await ref.read(tagServiceProvider).removeTag(f.id, tag);
                      ref.invalidate(fileTagsProvider(f.id));
                      ref.invalidate(allTagsProvider);
                    },
                  ),
                  loading: () => const Card(
                    margin: EdgeInsets.all(16),
                    child: Padding(
                      padding: EdgeInsets.all(16),
                      child: Center(child: CircularProgressIndicator()),
                    ),
                  ),
                  error: (_, __) => const SizedBox.shrink(),
                ),

                // Location (for remote files)
                if (f.isRemote) _RemoteFileCard(file: f),

                const SizedBox(height: 32),
              ],
            ),
          ),
        );
      },
      loading: () => Scaffold(
        appBar: AppBar(),
        body: const LoadingIndicator(message: 'Loading file...'),
      ),
      error: (e, _) => Scaffold(
        appBar: AppBar(),
        body: AppErrorWidget(
          message: 'Failed to load file',
          details: e.toString(),
          onRetry: () => ref.invalidate(fileDetailsProvider(fileId)),
        ),
      ),
    );
  }

  void _shareFile(BuildContext context, FileRecord file) {
    // TODO: Implement share
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Share not implemented yet')),
    );
  }

  Future<void> _handleAction(BuildContext context, WidgetRef ref, FileRecord file, String action) async {
    // Get folder path to construct full file path
    final folders = await ref.read(foldersNotifierProvider.future);
    final folder = folders.where((f) => f.id == file.folderId).firstOrNull;
    
    if (folder == null) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Folder not found')),
        );
      }
      return;
    }
    
    final fullPath = '${folder.path}/${file.relativePath}'.replaceAll('/', '\\');
    
    switch (action) {
      case 'open':
        try {
          if (Platform.isWindows) {
            await Process.run('cmd', ['/c', 'start', '', fullPath]);
          } else if (Platform.isMacOS) {
            await Process.run('open', [fullPath]);
          } else if (Platform.isLinux) {
            await Process.run('xdg-open', [fullPath]);
          }
        } catch (e) {
          if (context.mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(content: Text('Failed to open: $e')),
            );
          }
        }
        break;
      case 'folder':
        try {
          final dirPath = File(fullPath).parent.path;
          if (Platform.isWindows) {
            await Process.run('explorer', ['/select,', fullPath]);
          } else if (Platform.isMacOS) {
            await Process.run('open', ['-R', fullPath]);
          } else if (Platform.isLinux) {
            await Process.run('xdg-open', [dirPath]);
          }
        } catch (e) {
          if (context.mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(content: Text('Failed to open folder: $e')),
            );
          }
        }
        break;
      case 'download':
        if (context.mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('Download not implemented yet')),
          );
        }
        break;
    }
  }
}

class _FilePreview extends StatelessWidget {
  final FileRecord file;

  const _FilePreview({required this.file});

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final isDark = Theme.of(context).brightness == Brightness.dark;

    return Container(
      height: 300,
      color: colorScheme.surfaceContainerHighest,
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              getContentTypeIcon(file.contentType),
              size: 80,
              color: getContentTypeColor(file.contentType, dark: isDark),
            ),
            const SizedBox(height: 16),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
              decoration: BoxDecoration(
                color: getContentTypeColor(file.contentType, dark: isDark)
                    .withValues(alpha: 0.9),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Text(
                file.extension.toUpperCase(),
                style: const TextStyle(
                  color: Colors.white,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _FileDetailsCard extends StatelessWidget {
  final FileRecord file;

  const _FileDetailsCard({required this.file});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Card(
      margin: const EdgeInsets.all(16),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Details',
              style: theme.textTheme.titleMedium?.copyWith(
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 16),
            _DetailRow(
              icon: Icons.description,
              label: 'Name',
              value: file.name,
            ),
            _DetailRow(
              icon: Icons.folder,
              label: 'Path',
              value: file.relativePath,
            ),
            _DetailRow(
              icon: Icons.storage,
              label: 'Size',
              value: formatFileSize(file.size),
            ),
            _DetailRow(
              icon: Icons.category,
              label: 'Type',
              value: file.mimeType,
            ),
            _DetailRow(
              icon: Icons.calendar_today,
              label: 'Modified',
              value: formatFullDate(file.modifiedAt),
            ),
            _DetailRow(
              icon: Icons.update,
              label: 'Indexed',
              value: formatDateTime(file.indexedAt),
            ),
            _DetailRow(
              icon: getVisibilityIcon(file.visibility),
              label: 'Visibility',
              value: file.visibility.displayName,
            ),
            if (file.checksum != null && file.checksum!.isNotEmpty)
              _DetailRow(
                icon: Icons.fingerprint,
                label: 'Checksum',
                value: file.checksum!.length > 16 
                    ? '${file.checksum!.substring(0, 16)}...'
                    : file.checksum!,
              ),
          ],
        ),
      ),
    );
  }
}

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
    final colorScheme = theme.colorScheme;

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Row(
        children: [
          Icon(icon, size: 20, color: colorScheme.outline),
          const SizedBox(width: 12),
          SizedBox(
            width: 80,
            child: Text(
              label,
              style: theme.textTheme.bodySmall?.copyWith(
                color: colorScheme.outline,
              ),
            ),
          ),
          Expanded(
            child: Text(
              value,
              style: theme.textTheme.bodyMedium,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
            ),
          ),
        ],
      ),
    );
  }
}

class _ExifCard extends StatelessWidget {
  final ImageMetadata metadata;

  const _ExifCard({required this.metadata});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Image Info',
              style: theme.textTheme.titleMedium?.copyWith(
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 16),
            _DetailRow(
              icon: Icons.aspect_ratio,
              label: 'Resolution',
              value: metadata.resolution,
            ),
            if (metadata.camera != null)
              _DetailRow(
                icon: Icons.camera_alt,
                label: 'Camera',
                value: metadata.camera!,
              ),
            if (metadata.takenAt != null)
              _DetailRow(
                icon: Icons.access_time,
                label: 'Taken',
                value: formatFullDate(metadata.takenAt!),
              ),
            if (metadata.hasLocation)
              _DetailRow(
                icon: Icons.location_on,
                label: 'Location',
                value: '${metadata.latitude!.toStringAsFixed(4)}, ${metadata.longitude!.toStringAsFixed(4)}',
              ),
          ],
        ),
      ),
    );
  }
}

class _TagsCard extends StatefulWidget {
  final int fileId;
  final List<String> tags;
  final Future<void> Function(String tag) onAddTag;
  final Future<void> Function(String tag) onRemoveTag;

  const _TagsCard({
    required this.fileId,
    required this.tags,
    required this.onAddTag,
    required this.onRemoveTag,
  });

  @override
  State<_TagsCard> createState() => _TagsCardState();
}

class _TagsCardState extends State<_TagsCard> {
  final _controller = TextEditingController();
  bool _adding = false;

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Card(
      margin: const EdgeInsets.all(16),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Text(
                  'Tags',
                  style: theme.textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
                ),
                const Spacer(),
                if (!_adding)
                  TextButton.icon(
                    onPressed: () => setState(() => _adding = true),
                    icon: const Icon(Icons.add, size: 18),
                    label: const Text('Add'),
                  ),
              ],
            ),
            if (_adding) ...[
              const SizedBox(height: 8),
              Row(
                children: [
                  Expanded(
                    child: TextField(
                      controller: _controller,
                      decoration: const InputDecoration(
                        hintText: 'Enter tag...',
                        isDense: true,
                      ),
                      autofocus: true,
                      onSubmitted: _addTag,
                    ),
                  ),
                  IconButton(
                    icon: const Icon(Icons.check),
                    onPressed: () => _addTag(_controller.text),
                  ),
                  IconButton(
                    icon: const Icon(Icons.close),
                    onPressed: () {
                      setState(() => _adding = false);
                      _controller.clear();
                    },
                  ),
                ],
              ),
            ],
            const SizedBox(height: 12),
            if (widget.tags.isEmpty)
              Text(
                'No tags',
                style: theme.textTheme.bodyMedium?.copyWith(
                  color: colorScheme.outline,
                ),
              )
            else
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: widget.tags
                    .map((tag) => InputChip(
                          label: Text(tag),
                          onDeleted: () => widget.onRemoveTag(tag),
                        ))
                    .toList(),
              ),
          ],
        ),
      ),
    );
  }

  void _addTag(String tag) {
    if (tag.trim().isNotEmpty) {
      widget.onAddTag(tag.trim());
      _controller.clear();
      setState(() => _adding = false);
    }
  }
}

class _RemoteFileCard extends StatelessWidget {
  final FileRecord file;

  const _RemoteFileCard({required this.file});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(Icons.cloud, color: colorScheme.primary),
                const SizedBox(width: 8),
                Text(
                  'Remote File',
                  style: theme.textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Text(
              'This file is on another device: ${file.sourceDeviceId ?? "Unknown"}',
              style: theme.textTheme.bodyMedium,
            ),
            const SizedBox(height: 16),
            FilledButton.icon(
              onPressed: () {
                // TODO: Download file
              },
              icon: const Icon(Icons.download),
              label: const Text('Download'),
            ),
          ],
        ),
      ),
    );
  }
}

