import 'package:flutter/material.dart';

import '../../core/models/models.dart';
import 'file_card.dart';
import 'loading_indicator.dart';

/// Список файлов
class FileList extends StatelessWidget {
  final List<FileRecordCompact> files;
  final ScrollController? controller;
  final void Function(FileRecordCompact file)? onFileTap;
  final void Function(FileRecordCompact file)? onFileLongPress;
  final Set<int>? selectedIds;
  final bool loading;
  final VoidCallback? onLoadMore;

  const FileList({
    super.key,
    required this.files,
    this.controller,
    this.onFileTap,
    this.onFileLongPress,
    this.selectedIds,
    this.loading = false,
    this.onLoadMore,
  });

  @override
  Widget build(BuildContext context) {
    return NotificationListener<ScrollNotification>(
      onNotification: (notification) {
        if (notification is ScrollEndNotification &&
            notification.metrics.extentAfter < 200 &&
            onLoadMore != null) {
          onLoadMore!();
        }
        return false;
      },
      child: ListView.builder(
        controller: controller,
        itemCount: files.length + (loading ? 1 : 0),
        itemBuilder: (context, index) {
          if (index >= files.length) {
            return const Padding(
              padding: EdgeInsets.all(16),
              child: Center(child: CircularProgressIndicator()),
            );
          }

          final file = files[index];
          return FileListTile(
            file: file,
            selected: selectedIds?.contains(file.id) ?? false,
            onTap: onFileTap != null ? () => onFileTap!(file) : null,
            onLongPress:
                onFileLongPress != null ? () => onFileLongPress!(file) : null,
          );
        },
      ),
    );
  }
}

/// Sliver версия списка файлов
class SliverFileList extends StatelessWidget {
  final List<FileRecordCompact> files;
  final void Function(FileRecordCompact file)? onFileTap;
  final void Function(FileRecordCompact file)? onFileLongPress;
  final Set<int>? selectedIds;

  const SliverFileList({
    super.key,
    required this.files,
    this.onFileTap,
    this.onFileLongPress,
    this.selectedIds,
  });

  @override
  Widget build(BuildContext context) {
    return SliverList(
      delegate: SliverChildBuilderDelegate(
        (context, index) {
          final file = files[index];
          return FileListTile(
            file: file,
            selected: selectedIds?.contains(file.id) ?? false,
            onTap: onFileTap != null ? () => onFileTap!(file) : null,
            onLongPress:
                onFileLongPress != null ? () => onFileLongPress!(file) : null,
          );
        },
        childCount: files.length,
      ),
    );
  }
}

/// Скелетон для списка файлов
class FileListSkeleton extends StatelessWidget {
  final int itemCount;

  const FileListSkeleton({
    super.key,
    this.itemCount = 10,
  });

  @override
  Widget build(BuildContext context) {
    return ListView.builder(
      itemCount: itemCount,
      itemBuilder: (context, index) => const _FileListTileSkeleton(),
    );
  }
}

/// Sliver скелетон для списка файлов
class SliverFileListSkeleton extends StatelessWidget {
  final int itemCount;

  const SliverFileListSkeleton({
    super.key,
    this.itemCount = 10,
  });

  @override
  Widget build(BuildContext context) {
    return SliverList(
      delegate: SliverChildBuilderDelegate(
        (context, index) => const _FileListTileSkeleton(),
        childCount: itemCount,
      ),
    );
  }
}

class _FileListTileSkeleton extends StatelessWidget {
  const _FileListTileSkeleton();

  @override
  Widget build(BuildContext context) {
    return ListTile(
      leading: ShimmerBox(width: 48, height: 48),
      title: ShimmerBox(width: double.infinity, height: 16),
      subtitle: Padding(
        padding: const EdgeInsets.only(top: 4),
        child: ShimmerBox(width: 120, height: 12),
      ),
    );
  }
}

