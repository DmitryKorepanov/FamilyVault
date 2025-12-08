import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../core/models/models.dart';
import '../../core/providers/providers.dart';
import '../../shared/utils/formatters.dart';
import '../../shared/utils/file_icons.dart';
import 'file_thumbnail.dart';

/// Панель деталей файла — боковая панель на desktop, bottom sheet на mobile
class FileDetailsPanel extends ConsumerWidget {
  final int fileId;
  final VoidCallback onClose;

  const FileDetailsPanel({
    super.key,
    required this.fileId,
    required this.onClose,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final file = ref.watch(fileDetailsProvider(fileId));
    final tags = ref.watch(fileTagsProvider(fileId));
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Container(
      decoration: BoxDecoration(
        color: colorScheme.surface,
        border: Border(
          left: BorderSide(
            color: colorScheme.outlineVariant.withValues(alpha: 0.5),
            width: 1,
          ),
        ),
      ),
      child: file.when(
        data: (f) {
          if (f == null) {
            return _buildEmpty(context);
          }
          return _FileDetailsContent(
            file: f,
            tags: tags,
            onClose: onClose,
          );
        },
        loading: () => _buildLoading(context),
        error: (e, _) => _buildError(context, e.toString()),
      ),
    );
  }

  Widget _buildEmpty(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            Icons.error_outline,
            size: 48,
            color: colorScheme.outline,
          ),
          const SizedBox(height: 16),
          Text(
            'File not found',
            style: Theme.of(context).textTheme.titleMedium?.copyWith(
                  color: colorScheme.outline,
                ),
          ),
        ],
      ),
    );
  }

  Widget _buildLoading(BuildContext context) {
    return const Center(
      child: CircularProgressIndicator(),
    );
  }

  Widget _buildError(BuildContext context, String error) {
    final colorScheme = Theme.of(context).colorScheme;
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              Icons.error_outline,
              size: 48,
              color: colorScheme.error,
            ),
            const SizedBox(height: 16),
            Text(
              'Error loading file',
              style: Theme.of(context).textTheme.titleMedium,
            ),
            const SizedBox(height: 8),
            Text(
              error,
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: colorScheme.outline,
                  ),
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}

class _FileDetailsContent extends ConsumerWidget {
  final FileRecord file;
  final AsyncValue<List<String>> tags;
  final VoidCallback onClose;

  const _FileDetailsContent({
    required this.file,
    required this.tags,
    required this.onClose,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;
    
    // Получаем путь к папке для построения полного пути
    final folders = ref.watch(foldersNotifierProvider);
    final folderPath = folders.whenOrNull(
      data: (list) => list.where((f) => f.id == file.folderId).firstOrNull?.path,
    );

    return Column(
      children: [
        // Header
        Container(
          padding: const EdgeInsets.all(12),
          decoration: BoxDecoration(
            border: Border(
              bottom: BorderSide(
                color: colorScheme.outlineVariant.withValues(alpha: 0.5),
              ),
            ),
          ),
          child: Row(
            children: [
              Expanded(
                child: Text(
                  'Details',
                  style: theme.textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ),
              IconButton(
                icon: const Icon(Icons.close, size: 20),
                onPressed: onClose,
                tooltip: 'Close',
                visualDensity: VisualDensity.compact,
              ),
            ],
          ),
        ),

        // Content
        Expanded(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                // Preview area with image support
                FileThumbnailLarge(
                  fullFile: file,
                  folderPath: folderPath,
                  height: 160,
                ),

                const SizedBox(height: 16),

                // File name
                Text(
                  file.name,
                  style: theme.textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.w600,
                  ),
                  maxLines: 3,
                  overflow: TextOverflow.ellipsis,
                ),

                const SizedBox(height: 16),

                // Details list
                _DetailRow(
                  icon: Icons.folder_outlined,
                  label: 'Path',
                  value: file.relativePath,
                ),
                _DetailRow(
                  icon: Icons.storage_outlined,
                  label: 'Size',
                  value: formatFileSize(file.size),
                ),
                _DetailRow(
                  icon: Icons.category_outlined,
                  label: 'Type',
                  value: file.mimeType,
                ),
                _DetailRow(
                  icon: Icons.calendar_today_outlined,
                  label: 'Modified',
                  value: formatFullDate(file.modifiedAt),
                ),
                _DetailRow(
                  icon: getVisibilityIcon(file.visibility),
                  label: 'Visibility',
                  value: file.visibility.displayName,
                ),

                // EXIF for images
                if (file.imageMeta != null) ...[
                  const SizedBox(height: 16),
                  _SectionHeader(title: 'Image Info'),
                  const SizedBox(height: 8),
                  _DetailRow(
                    icon: Icons.aspect_ratio_outlined,
                    label: 'Resolution',
                    value: file.imageMeta!.resolution,
                  ),
                  if (file.imageMeta!.camera != null)
                    _DetailRow(
                      icon: Icons.camera_alt_outlined,
                      label: 'Camera',
                      value: file.imageMeta!.camera!,
                    ),
                  if (file.imageMeta!.takenAt != null)
                    _DetailRow(
                      icon: Icons.access_time_outlined,
                      label: 'Taken',
                      value: formatFullDate(file.imageMeta!.takenAt!),
                    ),
                ],

                // Tags
                const SizedBox(height: 16),
                _SectionHeader(title: 'Tags'),
                const SizedBox(height: 8),
                tags.when(
                  data: (tagList) => _TagsSection(
                    fileId: file.id,
                    tags: tagList,
                  ),
                  loading: () => const Center(
                    child: SizedBox(
                      width: 20,
                      height: 20,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    ),
                  ),
                  error: (_, __) => Text(
                    'Failed to load tags',
                    style: theme.textTheme.bodySmall?.copyWith(
                      color: colorScheme.error,
                    ),
                  ),
                ),

                const SizedBox(height: 24),
              ],
            ),
          ),
        ),

        // Action buttons
        Container(
          padding: const EdgeInsets.all(12),
          decoration: BoxDecoration(
            border: Border(
              top: BorderSide(
                color: colorScheme.outlineVariant.withValues(alpha: 0.5),
              ),
            ),
          ),
          child: Row(
            children: [
              Expanded(
                child: OutlinedButton.icon(
                  onPressed: () => _openInFolder(context, ref),
                  icon: const Icon(Icons.folder_open, size: 18),
                  label: const Text('Show in Folder'),
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: FilledButton.icon(
                  onPressed: () => _openFile(context, ref),
                  icon: const Icon(Icons.open_in_new, size: 18),
                  label: const Text('Open'),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }

  Future<void> _openFile(BuildContext context, WidgetRef ref) async {
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
  }

  Future<void> _openInFolder(BuildContext context, WidgetRef ref) async {
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

    try {
      if (Platform.isWindows) {
        await Process.run('explorer', ['/select,', fullPath]);
      } else if (Platform.isMacOS) {
        await Process.run('open', ['-R', fullPath]);
      } else if (Platform.isLinux) {
        final dirPath = File(fullPath).parent.path;
        await Process.run('xdg-open', [dirPath]);
      }
    } catch (e) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed to open folder: $e')),
        );
      }
    }
  }
}

class _SectionHeader extends StatelessWidget {
  final String title;

  const _SectionHeader({required this.title});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Row(
      children: [
        Text(
          title,
          style: theme.textTheme.labelLarge?.copyWith(
            color: colorScheme.primary,
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: Divider(
            color: colorScheme.outlineVariant.withValues(alpha: 0.5),
          ),
        ),
      ],
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
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 18, color: colorScheme.outline),
          const SizedBox(width: 10),
          SizedBox(
            width: 70,
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
              style: theme.textTheme.bodySmall?.copyWith(
                fontWeight: FontWeight.w500,
              ),
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
            ),
          ),
        ],
      ),
    );
  }
}

class _TagsSection extends ConsumerStatefulWidget {
  final int fileId;
  final List<String> tags;

  const _TagsSection({
    required this.fileId,
    required this.tags,
  });

  @override
  ConsumerState<_TagsSection> createState() => _TagsSectionState();
}

class _TagsSectionState extends ConsumerState<_TagsSection> {
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

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        if (_adding)
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _controller,
                    decoration: InputDecoration(
                      hintText: 'Enter tag...',
                      isDense: true,
                      contentPadding: const EdgeInsets.symmetric(
                        horizontal: 12,
                        vertical: 10,
                      ),
                      border: OutlineInputBorder(
                        borderRadius: BorderRadius.circular(8),
                      ),
                    ),
                    autofocus: true,
                    onSubmitted: _addTag,
                  ),
                ),
                const SizedBox(width: 4),
                IconButton(
                  icon: const Icon(Icons.check, size: 20),
                  onPressed: () => _addTag(_controller.text),
                  visualDensity: VisualDensity.compact,
                ),
                IconButton(
                  icon: const Icon(Icons.close, size: 20),
                  onPressed: () {
                    setState(() => _adding = false);
                    _controller.clear();
                  },
                  visualDensity: VisualDensity.compact,
                ),
              ],
            ),
          ),
        Wrap(
          spacing: 6,
          runSpacing: 6,
          children: [
            ...widget.tags.map(
              (tag) => InputChip(
                label: Text(tag),
                onDeleted: () => _removeTag(tag),
                deleteIconColor: colorScheme.onSurfaceVariant,
                visualDensity: VisualDensity.compact,
                labelStyle: theme.textTheme.bodySmall,
              ),
            ),
            if (!_adding)
              ActionChip(
                avatar: Icon(
                  Icons.add,
                  size: 16,
                  color: colorScheme.primary,
                ),
                label: const Text('Add'),
                onPressed: () => setState(() => _adding = true),
                visualDensity: VisualDensity.compact,
                labelStyle: theme.textTheme.bodySmall?.copyWith(
                  color: colorScheme.primary,
                ),
              ),
          ],
        ),
        if (widget.tags.isEmpty && !_adding)
          Padding(
            padding: const EdgeInsets.only(top: 4),
            child: Text(
              'No tags',
              style: theme.textTheme.bodySmall?.copyWith(
                color: colorScheme.outline,
              ),
            ),
          ),
      ],
    );
  }

  Future<void> _addTag(String tag) async {
    if (tag.trim().isNotEmpty) {
      await ref.read(tagServiceProvider).addTag(widget.fileId, tag.trim());
      ref.invalidate(fileTagsProvider(widget.fileId));
      ref.invalidate(allTagsProvider);
      _controller.clear();
      setState(() => _adding = false);
    }
  }

  Future<void> _removeTag(String tag) async {
    await ref.read(tagServiceProvider).removeTag(widget.fileId, tag);
    ref.invalidate(fileTagsProvider(widget.fileId));
    ref.invalidate(allTagsProvider);
  }
}

/// Показывает детали файла в bottom sheet (для мобильных)
Future<void> showFileDetailsSheet(BuildContext context, int fileId) {
  return showModalBottomSheet(
    context: context,
    isScrollControlled: true,
    useSafeArea: true,
    builder: (context) => DraggableScrollableSheet(
      initialChildSize: 0.7,
      minChildSize: 0.5,
      maxChildSize: 0.95,
      expand: false,
      builder: (context, scrollController) {
        return Consumer(
          builder: (context, ref, _) {
            final file = ref.watch(fileDetailsProvider(fileId));
            final tags = ref.watch(fileTagsProvider(fileId));

            return Container(
              decoration: BoxDecoration(
                color: Theme.of(context).colorScheme.surface,
                borderRadius: const BorderRadius.vertical(
                  top: Radius.circular(20),
                ),
              ),
              child: file.when(
                data: (f) {
                  if (f == null) {
                    return const Center(child: Text('File not found'));
                  }
                  return _FileDetailsContent(
                    file: f,
                    tags: tags,
                    onClose: () => Navigator.pop(context),
                  );
                },
                loading: () => const Center(
                  child: CircularProgressIndicator(),
                ),
                error: (e, _) => Center(
                  child: Text('Error: $e'),
                ),
              ),
            );
          },
        );
      },
    ),
  );
}

