// Модели для передачи файлов между устройствами

/// Результат запроса файла
/// Может быть либо cache hit (файл уже есть), либо pending (запрос отправлен)
class FileRequestResult {
  /// ID запроса (если файл запрошен)
  final String? requestId;
  
  /// Путь к кэшированному файлу (если cache hit)
  final String? cachedPath;
  
  /// Был ли это cache hit
  bool get isCached => cachedPath != null;
  
  /// Был ли это новый запрос
  bool get isPending => requestId != null;
  
  /// Cache hit — файл уже есть локально
  const FileRequestResult.cached(String path) : requestId = null, cachedPath = path;
  
  /// Pending — запрос отправлен, ждём ответа
  const FileRequestResult.pending(String id) : requestId = id, cachedPath = null;
  
  @override
  String toString() => isCached 
      ? 'FileRequestResult.cached($cachedPath)' 
      : 'FileRequestResult.pending($requestId)';
}

/// Статус передачи файла
enum FileTransferStatus {
  pending,
  inProgress,
  completed,
  failed,
  cancelled;

  static FileTransferStatus fromString(String value) {
    switch (value) {
      case 'pending':
        return FileTransferStatus.pending;
      case 'inProgress':
        return FileTransferStatus.inProgress;
      case 'completed':
        return FileTransferStatus.completed;
      case 'failed':
        return FileTransferStatus.failed;
      case 'cancelled':
        return FileTransferStatus.cancelled;
      default:
        return FileTransferStatus.pending;
    }
  }
}

/// Прогресс передачи файла
class FileTransferProgress {
  final String requestId;   // Уникальный ID запроса
  final String deviceId;    // ID устройства-источника
  final int fileId;
  final String fileName;
  final int totalSize;
  final int transferredSize;
  final FileTransferStatus status;
  final String? error;
  final String? localPath;

  const FileTransferProgress({
    required this.requestId,
    required this.deviceId,
    required this.fileId,
    required this.fileName,
    required this.totalSize,
    required this.transferredSize,
    required this.status,
    this.error,
    this.localPath,
  });

  /// Прогресс от 0.0 до 1.0
  double get progress =>
      totalSize > 0 ? transferredSize / totalSize : 0.0;

  /// Процент прогресса (0-100)
  int get progressPercent => (progress * 100).round();

  /// Проверка завершения
  bool get isComplete => status == FileTransferStatus.completed;

  /// Проверка активности
  bool get isActive =>
      status == FileTransferStatus.pending ||
      status == FileTransferStatus.inProgress;

  /// Проверка ошибки
  bool get hasError =>
      status == FileTransferStatus.failed ||
      status == FileTransferStatus.cancelled;

  factory FileTransferProgress.fromJson(Map<String, dynamic> json) {
    return FileTransferProgress(
      requestId: json['requestId'] as String? ?? '',
      deviceId: json['deviceId'] as String? ?? '',
      fileId: json['fileId'] as int? ?? 0,
      fileName: json['fileName'] as String? ?? '',
      totalSize: json['totalSize'] as int? ?? 0,
      transferredSize: json['transferredSize'] as int? ?? 0,
      status: FileTransferStatus.fromString(json['status'] as String? ?? 'pending'),
      error: json['error'] as String?,
      localPath: json['localPath'] as String?,
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'requestId': requestId,
      'deviceId': deviceId,
      'fileId': fileId,
      'fileName': fileName,
      'totalSize': totalSize,
      'transferredSize': transferredSize,
      'status': status.name,
      'error': error,
      'localPath': localPath,
      'progress': progress,
    };
  }

  @override
  String toString() {
    return 'FileTransferProgress(requestId: $requestId, deviceId: $deviceId, '
        'fileId: $fileId, fileName: $fileName, '
        'progress: ${progressPercent}%, status: ${status.name})';
  }

  FileTransferProgress copyWith({
    String? requestId,
    String? deviceId,
    int? fileId,
    String? fileName,
    int? totalSize,
    int? transferredSize,
    FileTransferStatus? status,
    String? error,
    String? localPath,
  }) {
    return FileTransferProgress(
      requestId: requestId ?? this.requestId,
      deviceId: deviceId ?? this.deviceId,
      fileId: fileId ?? this.fileId,
      fileName: fileName ?? this.fileName,
      totalSize: totalSize ?? this.totalSize,
      transferredSize: transferredSize ?? this.transferredSize,
      status: status ?? this.status,
      error: error ?? this.error,
      localPath: localPath ?? this.localPath,
    );
  }
}

