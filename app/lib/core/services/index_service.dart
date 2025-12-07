import '../ffi/native_bridge.dart';
import '../models/models.dart';

/// Сервис для работы с индексом файлов
/// 
/// Теперь тонкая обёртка над NativeBridge — все типизированные методы в bridge.
class IndexService {
  final NativeBridge _bridge;

  IndexService({NativeBridge? bridge}) : _bridge = bridge ?? NativeBridge.instance;

  // ═══════════════════════════════════════════════════════════
  // Папки
  // ═══════════════════════════════════════════════════════════

  /// Добавить папку для отслеживания
  Future<int> addFolder(String path, {String? name, Visibility visibility = Visibility.family}) async {
    final folderName = name ?? path.split(RegExp(r'[/\\]')).last;
    return _bridge.addFolder(path, folderName, visibility.value);
  }

  /// Удалить папку из отслеживания
  Future<void> removeFolder(int folderId) async {
    _bridge.removeFolder(folderId);
  }

  /// Получить список папок
  Future<List<WatchedFolder>> getFolders() async => _bridge.getFolders();

  /// Установить видимость папки
  Future<void> setFolderVisibility(int folderId, Visibility visibility) async {
    _bridge.setFolderVisibility(folderId, visibility.value);
  }

  /// Включить/отключить папку
  Future<void> setFolderEnabled(int folderId, bool enabled) async {
    _bridge.setFolderEnabled(folderId, enabled);
  }

  // ═══════════════════════════════════════════════════════════
  // Сканирование
  // ═══════════════════════════════════════════════════════════

  /// Сканировать папку
  Future<void> scanFolder(int folderId, {void Function(ScanProgress)? onProgress}) async {
    await _bridge.scanFolder(folderId, onProgress: onProgress);
  }

  /// Сканировать все папки
  Future<void> scanAll({void Function(ScanProgress)? onProgress}) async {
    await _bridge.scanAll(onProgress: onProgress);
  }

  /// Остановить сканирование
  void stopScan() => _bridge.stopScan();

  // ═══════════════════════════════════════════════════════════
  // Файлы
  // ═══════════════════════════════════════════════════════════

  /// Получить файл по ID
  Future<FileRecord?> getFile(int fileId) async => _bridge.getFile(fileId);

  /// Получить недавние файлы (полная версия)
  Future<List<FileRecord>> getRecentFiles({int limit = 50}) async =>
      _bridge.getRecentFiles(limit);

  /// Получить недавние файлы (компактная версия)
  Future<List<FileRecordCompact>> getRecentFilesCompact({int limit = 50}) async =>
      _bridge.getRecentFilesCompact(limit);

  /// Получить файлы папки (компактная версия)
  Future<List<FileRecordCompact>> getFilesByFolder(int folderId, {int limit = 100, int offset = 0}) async =>
      _bridge.getFilesByFolderCompact(folderId, limit, offset);

  /// Установить видимость файла
  Future<void> setFileVisibility(int fileId, Visibility visibility) async {
    _bridge.setFileVisibility(fileId, visibility.value);
  }

  // ═══════════════════════════════════════════════════════════
  // Статистика
  // ═══════════════════════════════════════════════════════════

  /// Получить статистику индекса
  Future<IndexStats> getStats() async => _bridge.getStats();

  // ═══════════════════════════════════════════════════════════
  // Настройки и оптимизация
  // ═══════════════════════════════════════════════════════════

  /// Оптимизировать БД (rebuild FTS + VACUUM)
  Future<void> optimizeDatabase() async {
    _bridge.optimizeDatabase();
  }

  /// Получить максимальный размер текста для индексации (KB)
  int getMaxTextSizeKB() => _bridge.getMaxTextSizeKB();

  /// Установить максимальный размер текста для индексации (KB)
  void setMaxTextSizeKB(int sizeKB) => _bridge.setMaxTextSizeKB(sizeKB);
}
