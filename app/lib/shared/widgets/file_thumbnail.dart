import 'dart:io';

import 'package:flutter/material.dart';

import '../../core/models/models.dart';
import '../../shared/utils/file_icons.dart';

/// Виджет превью файла с поддержкой изображений
class FileThumbnail extends StatelessWidget {
  final FileRecordCompact file;
  final double? width;
  final double? height;
  final BoxFit fit;
  final bool showExtensionBadge;

  const FileThumbnail({
    super.key,
    required this.file,
    this.width,
    this.height,
    this.fit = BoxFit.cover,
    this.showExtensionBadge = false,
  });

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final isDark = Theme.of(context).brightness == Brightness.dark;

    // Для изображений показываем превью
    if (file.isImage && file.isLocal && file.fullPath.isNotEmpty) {
      return _ImageThumbnail(
        file: file,
        width: width,
        height: height,
        fit: fit,
        showExtensionBadge: showExtensionBadge,
      );
    }

    // Для остальных файлов — иконка
    return Container(
      width: width,
      height: height,
      color: colorScheme.surfaceContainerHighest,
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              getContentTypeIcon(file.contentType),
              size: 48,
              color: getContentTypeColor(file.contentType, dark: isDark),
            ),
            if (showExtensionBadge && file.extension.isNotEmpty) ...[
              const SizedBox(height: 8),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                decoration: BoxDecoration(
                  color: getContentTypeColor(file.contentType, dark: isDark),
                  borderRadius: BorderRadius.circular(4),
                ),
                child: Text(
                  file.extension.toUpperCase(),
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 10,
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _ImageThumbnail extends StatefulWidget {
  final FileRecordCompact file;
  final double? width;
  final double? height;
  final BoxFit fit;
  final bool showExtensionBadge;

  const _ImageThumbnail({
    required this.file,
    this.width,
    this.height,
    this.fit = BoxFit.cover,
    this.showExtensionBadge = false,
  });

  @override
  State<_ImageThumbnail> createState() => _ImageThumbnailState();
}

class _ImageThumbnailState extends State<_ImageThumbnail> {
  bool _hasError = false;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final isDark = Theme.of(context).brightness == Brightness.dark;

    if (_hasError) {
      return _buildFallback(colorScheme, isDark);
    }

    final file = File(widget.file.fullPath);

    return Container(
      width: widget.width,
      height: widget.height,
      color: colorScheme.surfaceContainerHighest,
      child: Image.file(
        file,
        width: widget.width,
        height: widget.height,
        fit: widget.fit,
        cacheWidth: 300, // Ограничиваем размер кэша для производительности
        errorBuilder: (context, error, stackTrace) {
          // При ошибке показываем иконку
          WidgetsBinding.instance.addPostFrameCallback((_) {
            if (mounted) {
              setState(() => _hasError = true);
            }
          });
          return _buildFallback(colorScheme, isDark);
        },
        frameBuilder: (context, child, frame, wasSynchronouslyLoaded) {
          if (wasSynchronouslyLoaded) return child;
          return AnimatedSwitcher(
            duration: const Duration(milliseconds: 200),
            child: frame != null
                ? child
                : Container(
                    color: colorScheme.surfaceContainerHighest,
                    child: Center(
                      child: SizedBox(
                        width: 24,
                        height: 24,
                        child: CircularProgressIndicator(
                          strokeWidth: 2,
                          color: colorScheme.outline.withValues(alpha: 0.5),
                        ),
                      ),
                    ),
                  ),
          );
        },
      ),
    );
  }

  Widget _buildFallback(ColorScheme colorScheme, bool isDark) {
    return Container(
      width: widget.width,
      height: widget.height,
      color: colorScheme.surfaceContainerHighest,
      child: Center(
        child: Icon(
          getContentTypeIcon(widget.file.contentType),
          size: 48,
          color: getContentTypeColor(widget.file.contentType, dark: isDark),
        ),
      ),
    );
  }
}

/// Виджет превью для панели деталей (больший размер)
class FileThumbnailLarge extends StatelessWidget {
  final FileRecordCompact? compactFile;
  final FileRecord? fullFile;
  final String? folderPath;
  final double height;

  const FileThumbnailLarge({
    super.key,
    this.compactFile,
    this.fullFile,
    this.folderPath,
    this.height = 200,
  });

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final isDark = Theme.of(context).brightness == Brightness.dark;

    // Определяем путь к файлу
    String? filePath;
    ContentType contentType;
    String extension;

    if (compactFile != null) {
      filePath = compactFile!.fullPath;
      contentType = compactFile!.contentType;
      extension = compactFile!.extension;
    } else if (fullFile != null && folderPath != null) {
      final separator = folderPath!.contains('\\') ? '\\' : '/';
      filePath = '$folderPath$separator${fullFile!.relativePath}';
      contentType = fullFile!.contentType;
      extension = fullFile!.extension;
    } else if (fullFile != null) {
      contentType = fullFile!.contentType;
      extension = fullFile!.extension;
      filePath = null;
    } else {
      return const SizedBox.shrink();
    }

    final isImage = contentType == ContentType.image;

    // Для изображений показываем превью
    if (isImage && filePath != null && filePath.isNotEmpty) {
      final file = File(filePath);
      return Container(
        height: height,
        width: double.infinity,
        decoration: BoxDecoration(
          color: colorScheme.surfaceContainerHighest,
          borderRadius: BorderRadius.circular(12),
        ),
        clipBehavior: Clip.antiAlias,
        child: Image.file(
          file,
          height: height,
          width: double.infinity,
          fit: BoxFit.cover,
          cacheWidth: 600,
          errorBuilder: (context, error, stackTrace) {
            return _buildIconPreview(
                context, contentType, extension, colorScheme, isDark);
          },
          frameBuilder: (context, child, frame, wasSynchronouslyLoaded) {
            if (wasSynchronouslyLoaded) return child;
            return AnimatedSwitcher(
              duration: const Duration(milliseconds: 200),
              child: frame != null
                  ? child
                  : Center(
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        color: colorScheme.outline.withValues(alpha: 0.5),
                      ),
                    ),
            );
          },
        ),
      );
    }

    // Для остальных файлов — иконка
    return _buildIconPreview(context, contentType, extension, colorScheme, isDark);
  }

  Widget _buildIconPreview(BuildContext context, ContentType contentType,
      String extension, ColorScheme colorScheme, bool isDark) {
    return Container(
      height: height,
      width: double.infinity,
      decoration: BoxDecoration(
        color: colorScheme.surfaceContainerHighest,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              getContentTypeIcon(contentType),
              size: 56,
              color: getContentTypeColor(contentType, dark: isDark),
            ),
            const SizedBox(height: 8),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
              decoration: BoxDecoration(
                color: getContentTypeColor(contentType, dark: isDark),
                borderRadius: BorderRadius.circular(6),
              ),
              child: Text(
                extension.toUpperCase(),
                style: const TextStyle(
                  color: Colors.white,
                  fontWeight: FontWeight.bold,
                  fontSize: 12,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

