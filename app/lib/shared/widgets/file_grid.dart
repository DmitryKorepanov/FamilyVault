import 'package:flutter/material.dart';

import '../../core/models/models.dart';
import '../../theme/app_theme.dart';
import 'file_card.dart';
import 'loading_indicator.dart';

/// Сетка файлов
class FileGrid extends StatelessWidget {
  final List<FileRecordCompact> files;
  final ScrollController? controller;
  final void Function(FileRecordCompact file)? onFileTap;
  final void Function(FileRecordCompact file)? onFileLongPress;
  final Set<int>? selectedIds;
  final bool loading;
  final VoidCallback? onLoadMore;

  const FileGrid({
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
      child: GridView.builder(
        controller: controller,
        padding: const EdgeInsets.all(16),
        gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
          crossAxisCount: AppSizes.gridCrossAxisCount(context),
          mainAxisSpacing: 16,
          crossAxisSpacing: 16,
          childAspectRatio: 0.85,
        ),
        itemCount: files.length + (loading ? 1 : 0),
        itemBuilder: (context, index) {
          if (index >= files.length) {
            return const Center(child: CircularProgressIndicator());
          }

          final file = files[index];
          return FileCard(
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

/// Sliver версия сетки файлов
class SliverFileGrid extends StatelessWidget {
  final List<FileRecordCompact> files;
  final void Function(FileRecordCompact file)? onFileTap;
  final void Function(FileRecordCompact file)? onFileLongPress;
  final Set<int>? selectedIds;

  const SliverFileGrid({
    super.key,
    required this.files,
    this.onFileTap,
    this.onFileLongPress,
    this.selectedIds,
  });

  @override
  Widget build(BuildContext context) {
    return SliverPadding(
      padding: const EdgeInsets.all(16),
      sliver: SliverGrid(
        delegate: SliverChildBuilderDelegate(
          (context, index) {
            final file = files[index];
            return FileCard(
              file: file,
              selected: selectedIds?.contains(file.id) ?? false,
              onTap: onFileTap != null ? () => onFileTap!(file) : null,
              onLongPress:
                  onFileLongPress != null ? () => onFileLongPress!(file) : null,
            );
          },
          childCount: files.length,
        ),
        gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
          crossAxisCount: AppSizes.gridCrossAxisCount(context),
          mainAxisSpacing: 16,
          crossAxisSpacing: 16,
          childAspectRatio: 0.85,
        ),
      ),
    );
  }
}

/// Скелетон для сетки файлов
class FileGridSkeleton extends StatelessWidget {
  final int itemCount;

  const FileGridSkeleton({
    super.key,
    this.itemCount = 12,
  });

  @override
  Widget build(BuildContext context) {
    return GridView.builder(
      padding: const EdgeInsets.all(16),
      gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: AppSizes.gridCrossAxisCount(context),
        mainAxisSpacing: 16,
        crossAxisSpacing: 16,
        childAspectRatio: 0.85,
      ),
      itemCount: itemCount,
      itemBuilder: (context, index) => const _FileCardSkeleton(),
    );
  }
}

/// Sliver скелетон для сетки файлов
class SliverFileGridSkeleton extends StatelessWidget {
  final int itemCount;

  const SliverFileGridSkeleton({
    super.key,
    this.itemCount = 12,
  });

  @override
  Widget build(BuildContext context) {
    return SliverPadding(
      padding: const EdgeInsets.all(16),
      sliver: SliverGrid(
        delegate: SliverChildBuilderDelegate(
          (context, index) => const _FileCardSkeleton(),
          childCount: itemCount,
        ),
        gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
          crossAxisCount: AppSizes.gridCrossAxisCount(context),
          mainAxisSpacing: 16,
          crossAxisSpacing: 16,
          childAspectRatio: 0.85,
        ),
      ),
    );
  }
}

class _FileCardSkeleton extends StatelessWidget {
  const _FileCardSkeleton();

  @override
  Widget build(BuildContext context) {
    return Card(
      clipBehavior: Clip.antiAlias,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Expanded(
            child: ShimmerBox(
              width: double.infinity,
              height: double.infinity,
              borderRadius: BorderRadius.zero,
            ),
          ),
          Padding(
            padding: const EdgeInsets.all(12),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                ShimmerBox(width: double.infinity, height: 16),
                const SizedBox(height: 8),
                ShimmerBox(width: 80, height: 12),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

