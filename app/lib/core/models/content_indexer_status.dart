/// Статус индексации контента (извлечения текста)
class ContentIndexerStatus {
  /// Файлов в очереди
  final int pending;
  
  /// Обработано файлов
  final int processed;
  
  /// Не удалось обработать
  final int failed;
  
  /// Идёт ли обработка
  final bool isRunning;
  
  /// Текущий обрабатываемый файл
  final String? currentFile;

  const ContentIndexerStatus({
    required this.pending,
    required this.processed,
    required this.failed,
    required this.isRunning,
    this.currentFile,
  });

  factory ContentIndexerStatus.fromJson(Map<String, dynamic> json) {
    return ContentIndexerStatus(
      pending: json['pending'] as int? ?? 0,
      processed: json['processed'] as int? ?? 0,
      failed: json['failed'] as int? ?? 0,
      isRunning: json['isRunning'] as bool? ?? false,
      currentFile: json['currentFile'] as String?,
    );
  }

  Map<String, dynamic> toJson() => {
    'pending': pending,
    'processed': processed,
    'failed': failed,
    'isRunning': isRunning,
    'currentFile': currentFile,
  };

  /// Прогресс обработки (0.0 - 1.0)
  double get progress {
    final total = processed + pending;
    return total > 0 ? processed / total : 0.0;
  }

  /// Общее количество файлов
  int get total => processed + pending;
}

