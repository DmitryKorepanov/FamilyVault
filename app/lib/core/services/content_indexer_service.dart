import '../ffi/native_bridge.dart';
import '../models/models.dart';

/// Сервис для извлечения текста из документов
/// 
/// Обёртка над NativeBridge для ContentIndexer
class ContentIndexerService {
  final NativeBridge _bridge;

  ContentIndexerService(this._bridge);

  /// Запустить фоновое извлечение текста
  void start() => _bridge.startContentIndexer();

  /// Остановить извлечение текста
  void stop({bool wait = true}) => _bridge.stopContentIndexer(wait: wait);

  /// Проверить, запущено ли извлечение
  bool get isRunning => _bridge.isContentIndexerRunning();

  /// Извлечь текст из конкретного файла (синхронно)
  /// @return true если успешно извлечено
  bool processFile(int fileId) => _bridge.processFileContent(fileId);

  /// Добавить все необработанные файлы в очередь
  /// @return количество добавленных файлов
  int enqueueUnprocessed() => _bridge.enqueueUnprocessedContent();

  /// Получить статус индексации
  ContentIndexerStatus getStatus() => _bridge.getContentIndexerStatus();

  /// Получить количество файлов без извлечённого текста
  int get pendingCount => _bridge.getPendingContentCount();

  /// Проверить, поддерживается ли MIME тип для извлечения
  bool canExtract(String mimeType) => _bridge.canExtractContent(mimeType);

  /// Переиндексировать весь контент
  Future<void> reindexAll({void Function(int processed, int total)? onProgress}) {
    return _bridge.reindexAllContent(onProgress: onProgress);
  }

  /// Получить максимальный размер текста для индексации (KB)
  int getMaxTextSizeKB() => _bridge.getContentIndexerMaxTextSizeKB();

  /// Установить максимальный размер текста для индексации (KB)
  void setMaxTextSizeKB(int sizeKB) => _bridge.setContentIndexerMaxTextSizeKB(sizeKB);
}

