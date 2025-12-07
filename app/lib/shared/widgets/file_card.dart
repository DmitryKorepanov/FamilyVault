import 'package:flutter/material.dart';

import '../../core/models/models.dart';
import '../utils/file_icons.dart';
import '../utils/formatters.dart';

/// Карточка файла для сетки
class FileCard extends StatelessWidget {
  final FileRecordCompact file;
  final VoidCallback? onTap;
  final VoidCallback? onLongPress;
  final bool selected;

  const FileCard({
    super.key,
    required this.file,
    this.onTap,
    this.onLongPress,
    this.selected = false,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;
    final isDark = theme.brightness == Brightness.dark;

    return Card(
      clipBehavior: Clip.antiAlias,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: selected
            ? BorderSide(color: colorScheme.primary, width: 2)
            : BorderSide.none,
      ),
      child: InkWell(
        onTap: onTap,
        onLongPress: onLongPress,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // Thumbnail или иконка
            Expanded(
              child: Container(
                color: colorScheme.surfaceContainerHighest,
                child: Stack(
                  fit: StackFit.expand,
                  children: [
                    // TODO: Реальный thumbnail когда будет ThumbnailService
                    _FileTypeIcon(
                      contentType: file.contentType,
                      extension: file.extension,
                    ),

                    // Remote индикатор
                    if (file.isRemote)
                      Positioned(
                        top: 8,
                        left: 8,
                        child: _Badge(
                          icon: Icons.cloud,
                          color: colorScheme.primary,
                        ),
                      ),

                    // Индикатор типа
                    Positioned(
                      bottom: 8,
                      right: 8,
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 6,
                          vertical: 2,
                        ),
                        decoration: BoxDecoration(
                          color: getContentTypeColor(file.contentType, dark: isDark)
                              .withValues(alpha: 0.9),
                          borderRadius: BorderRadius.circular(4),
                        ),
                        child: Text(
                          file.extension.toUpperCase(),
                          style: theme.textTheme.labelSmall?.copyWith(
                            color: Colors.white,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                      ),
                    ),

                    // Выбранный индикатор
                    if (selected)
                      Positioned(
                        top: 8,
                        right: 8,
                        child: Container(
                          padding: const EdgeInsets.all(4),
                          decoration: BoxDecoration(
                            color: colorScheme.primary,
                            shape: BoxShape.circle,
                          ),
                          child: Icon(
                            Icons.check,
                            size: 16,
                            color: colorScheme.onPrimary,
                          ),
                        ),
                      ),
                  ],
                ),
              ),
            ),

            // Информация
            Padding(
              padding: const EdgeInsets.all(12),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    file.name,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: theme.textTheme.bodyMedium?.copyWith(
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                  const SizedBox(height: 4),
                  Row(
                    children: [
                      Text(
                        formatFileSize(file.size),
                        style: theme.textTheme.bodySmall?.copyWith(
                          color: colorScheme.outline,
                        ),
                      ),
                      const Spacer(),
                      Text(
                        formatDate(file.modifiedAt),
                        style: theme.textTheme.bodySmall?.copyWith(
                          color: colorScheme.outline,
                        ),
                      ),
                    ],
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Элемент списка файлов
class FileListTile extends StatelessWidget {
  final FileRecordCompact file;
  final VoidCallback? onTap;
  final VoidCallback? onLongPress;
  final bool selected;
  final Widget? trailing;

  const FileListTile({
    super.key,
    required this.file,
    this.onTap,
    this.onLongPress,
    this.selected = false,
    this.trailing,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;
    final isDark = theme.brightness == Brightness.dark;

    return ListTile(
      leading: Container(
        width: 48,
        height: 48,
        decoration: BoxDecoration(
          color: getContentTypeColor(file.contentType, dark: isDark)
              .withValues(alpha: 0.1),
          borderRadius: BorderRadius.circular(8),
        ),
        child: Icon(
          getContentTypeIcon(file.contentType),
          color: getContentTypeColor(file.contentType, dark: isDark),
        ),
      ),
      title: Text(
        file.name,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
      ),
      subtitle: Row(
        children: [
          Text(formatFileSize(file.size)),
          const Text(' • '),
          Text(formatDate(file.modifiedAt)),
          if (file.isRemote) ...[
            const Text(' • '),
            Icon(
              Icons.cloud,
              size: 14,
              color: colorScheme.outline,
            ),
          ],
        ],
      ),
      trailing: trailing ??
          (selected
              ? Icon(Icons.check_circle, color: colorScheme.primary)
              : null),
      selected: selected,
      onTap: onTap,
      onLongPress: onLongPress,
    );
  }
}

/// Иконка типа файла
class _FileTypeIcon extends StatelessWidget {
  final ContentType contentType;
  final String extension;

  const _FileTypeIcon({
    required this.contentType,
    required this.extension,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final isDark = theme.brightness == Brightness.dark;

    return Center(
      child: Icon(
        getExtensionIcon(extension),
        size: 48,
        color: getContentTypeColor(contentType, dark: isDark),
      ),
    );
  }
}

/// Бейдж (для remote, visibility и т.д.)
class _Badge extends StatelessWidget {
  final IconData icon;
  final Color color;

  const _Badge({
    required this.icon,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(4),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.9),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Icon(
        icon,
        size: 14,
        color: Colors.white,
      ),
    );
  }
}

