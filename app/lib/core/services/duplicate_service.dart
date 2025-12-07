import '../ffi/native_bridge.dart';
import '../models/models.dart';

/// Сервис для работы с дубликатами
/// 
/// Теперь тонкая обёртка над NativeBridge — все типизированные методы в bridge.
class DuplicateService {
  final NativeBridge _bridge;

  DuplicateService({NativeBridge? bridge}) : _bridge = bridge ?? NativeBridge.instance;

  /// Найти дубликаты
  Future<List<DuplicateGroup>> findDuplicates() async => _bridge.findDuplicates();

  /// Получить статистику дубликатов
  Future<DuplicateStats> getStats() async => _bridge.getDuplicatesStats();

  /// Получить файлы без бэкапа
  Future<List<FileRecord>> getFilesWithoutBackup() async => _bridge.getFilesWithoutBackup();

  /// Удалить файл
  Future<void> deleteFile(int fileId) async {
    _bridge.deleteFile(fileId);
  }

  /// Удалить несколько файлов
  Future<void> deleteFiles(List<int> fileIds) async {
    for (final id in fileIds) {
      await deleteFile(id);
    }
  }

  /// Вычислить недостающие checksums
  Future<void> computeChecksums({void Function(int processed, int total)? onProgress}) async {
    await _bridge.computeChecksums(onProgress: onProgress);
  }

  /// Удалить дубликаты, оставив один файл
  /// Возвращает количество удалённых файлов
  Future<int> deleteDuplicates(DuplicateGroup group, {int? keepFileId}) async {
    if (group.localCopies.length <= 1) return 0;
    
    final idToKeep = keepFileId ?? group.localCopies.first.id;
    final toDelete = group.localCopies.where((f) => f.id != idToKeep).map((f) => f.id).toList();
    
    await deleteFiles(toDelete);
    return toDelete.length;
  }
}
