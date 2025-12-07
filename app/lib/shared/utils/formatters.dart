import 'package:intl/intl.dart';

/// Форматирование размера файла
String formatFileSize(int bytes) {
  if (bytes < 1024) {
    return '$bytes B';
  } else if (bytes < 1024 * 1024) {
    return '${(bytes / 1024).toStringAsFixed(1)} KB';
  } else if (bytes < 1024 * 1024 * 1024) {
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
  } else {
    return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(2)} GB';
  }
}

/// Форматирование даты
String formatDate(DateTime date) {
  final now = DateTime.now();
  final difference = now.difference(date);

  if (difference.inDays == 0) {
    if (difference.inHours == 0) {
      if (difference.inMinutes == 0) {
        return 'Just now';
      }
      return '${difference.inMinutes}m ago';
    }
    return '${difference.inHours}h ago';
  } else if (difference.inDays == 1) {
    return 'Yesterday';
  } else if (difference.inDays < 7) {
    return '${difference.inDays}d ago';
  } else if (date.year == now.year) {
    return DateFormat('MMM d').format(date);
  } else {
    return DateFormat('MMM d, y').format(date);
  }
}

/// Форматирование даты с временем
String formatDateTime(DateTime date) {
  final now = DateTime.now();
  final difference = now.difference(date);

  if (difference.inDays == 0) {
    return DateFormat('HH:mm').format(date);
  } else if (difference.inDays == 1) {
    return 'Yesterday, ${DateFormat('HH:mm').format(date)}';
  } else if (date.year == now.year) {
    return DateFormat('MMM d, HH:mm').format(date);
  } else {
    return DateFormat('MMM d, y HH:mm').format(date);
  }
}

/// Форматирование полной даты
String formatFullDate(DateTime date) {
  return DateFormat('EEEE, MMMM d, y').format(date);
}

/// Форматирование количества файлов
String formatFileCount(int count) {
  if (count == 1) {
    return '1 file';
  }
  return '$count files';
}

/// Форматирование количества папок
String formatFolderCount(int count) {
  if (count == 1) {
    return '1 folder';
  }
  return '$count folders';
}

/// Форматирование количества элементов
String formatItemCount(int count) {
  if (count == 1) {
    return '1 item';
  }
  return '$count items';
}

/// Форматирование числа с разделителями
String formatNumber(int number) {
  return NumberFormat.decimalPattern().format(number);
}

/// Форматирование процента
String formatPercent(double value) {
  return '${(value * 100).toStringAsFixed(1)}%';
}

/// Получение расширения файла
String getFileExtension(String filename) {
  final parts = filename.split('.');
  if (parts.length > 1) {
    return parts.last.toLowerCase();
  }
  return '';
}

/// Получение имени файла без расширения
String getFileNameWithoutExtension(String filename) {
  final lastDot = filename.lastIndexOf('.');
  if (lastDot > 0) {
    return filename.substring(0, lastDot);
  }
  return filename;
}

