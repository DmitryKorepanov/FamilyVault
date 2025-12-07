import 'package:flutter/material.dart';
import '../../core/models/types.dart' show ContentType, FileVisibility;

/// Иконка для типа контента
IconData getContentTypeIcon(ContentType type) {
  switch (type) {
    case ContentType.image:
      return Icons.image;
    case ContentType.video:
      return Icons.videocam;
    case ContentType.audio:
      return Icons.audiotrack;
    case ContentType.document:
      return Icons.description;
    case ContentType.archive:
      return Icons.archive;
    case ContentType.other:
    case ContentType.unknown:
      return Icons.insert_drive_file;
  }
}

/// Цвет для типа контента
Color getContentTypeColor(ContentType type, {bool dark = false}) {
  switch (type) {
    case ContentType.image:
      return dark ? Colors.green.shade300 : Colors.green;
    case ContentType.video:
      return dark ? Colors.red.shade300 : Colors.red;
    case ContentType.audio:
      return dark ? Colors.purple.shade300 : Colors.purple;
    case ContentType.document:
      return dark ? Colors.blue.shade300 : Colors.blue;
    case ContentType.archive:
      return dark ? Colors.amber.shade300 : Colors.amber;
    case ContentType.other:
    case ContentType.unknown:
      return dark ? Colors.grey.shade300 : Colors.grey;
  }
}

/// Иконка для расширения файла
IconData getExtensionIcon(String extension) {
  switch (extension.toLowerCase()) {
    // Изображения
    case 'jpg':
    case 'jpeg':
    case 'png':
    case 'gif':
    case 'bmp':
    case 'webp':
    case 'svg':
    case 'ico':
    case 'heic':
    case 'heif':
    case 'tiff':
    case 'tif':
    case 'raw':
      return Icons.image;

    // Видео
    case 'mp4':
    case 'avi':
    case 'mkv':
    case 'mov':
    case 'wmv':
    case 'flv':
    case 'webm':
    case 'm4v':
    case '3gp':
      return Icons.videocam;

    // Аудио
    case 'mp3':
    case 'wav':
    case 'flac':
    case 'aac':
    case 'm4a':
    case 'ogg':
    case 'wma':
      return Icons.audiotrack;

    // Документы
    case 'pdf':
      return Icons.picture_as_pdf;
    case 'doc':
    case 'docx':
      return Icons.description;
    case 'xls':
    case 'xlsx':
      return Icons.table_chart;
    case 'ppt':
    case 'pptx':
      return Icons.slideshow;
    case 'txt':
    case 'md':
    case 'rtf':
      return Icons.article;

    // Код
    case 'html':
    case 'css':
    case 'js':
    case 'ts':
    case 'jsx':
    case 'tsx':
    case 'json':
    case 'xml':
    case 'yaml':
    case 'yml':
    case 'dart':
    case 'py':
    case 'java':
    case 'cpp':
    case 'c':
    case 'h':
    case 'cs':
    case 'go':
    case 'rs':
    case 'swift':
    case 'kt':
    case 'php':
    case 'rb':
    case 'sh':
    case 'bat':
      return Icons.code;

    // Архивы
    case 'zip':
    case 'rar':
    case '7z':
    case 'tar':
    case 'gz':
    case 'bz2':
    case 'xz':
      return Icons.archive;

    // Исполняемые
    case 'exe':
    case 'msi':
    case 'apk':
    case 'dmg':
    case 'deb':
    case 'rpm':
      return Icons.app_shortcut;

    default:
      return Icons.insert_drive_file;
  }
}

/// Цвет для расширения файла
Color getExtensionColor(String extension, {bool dark = false}) {
  switch (extension.toLowerCase()) {
    // Изображения
    case 'jpg':
    case 'jpeg':
    case 'png':
    case 'gif':
    case 'bmp':
    case 'webp':
    case 'svg':
    case 'heic':
    case 'heif':
      return dark ? Colors.green.shade300 : Colors.green;

    // Видео
    case 'mp4':
    case 'avi':
    case 'mkv':
    case 'mov':
    case 'wmv':
    case 'webm':
      return dark ? Colors.red.shade300 : Colors.red;

    // Аудио
    case 'mp3':
    case 'wav':
    case 'flac':
    case 'aac':
    case 'm4a':
      return dark ? Colors.purple.shade300 : Colors.purple;

    // Документы
    case 'pdf':
      return dark ? Colors.red.shade300 : Colors.red.shade700;
    case 'doc':
    case 'docx':
      return dark ? Colors.blue.shade300 : Colors.blue;
    case 'xls':
    case 'xlsx':
      return dark ? Colors.green.shade300 : Colors.green.shade700;
    case 'ppt':
    case 'pptx':
      return dark ? Colors.orange.shade300 : Colors.orange;

    // Код
    case 'html':
    case 'css':
    case 'js':
    case 'ts':
    case 'dart':
    case 'py':
    case 'java':
      return dark ? Colors.cyan.shade300 : Colors.cyan;

    // Архивы
    case 'zip':
    case 'rar':
    case '7z':
      return dark ? Colors.amber.shade300 : Colors.amber;

    default:
      return dark ? Colors.grey.shade300 : Colors.grey;
  }
}

/// Иконка для видимости
IconData getVisibilityIcon(FileVisibility visibility) {
  switch (visibility) {
    case FileVisibility.private:
      return Icons.lock;
    case FileVisibility.family:
      return Icons.family_restroom;
  }
}

/// Цвет для видимости
Color getVisibilityColor(FileVisibility visibility, {bool dark = false}) {
  switch (visibility) {
    case FileVisibility.private:
      return dark ? Colors.grey.shade300 : Colors.grey;
    case FileVisibility.family:
      return dark ? Colors.blue.shade300 : Colors.blue;
  }
}
