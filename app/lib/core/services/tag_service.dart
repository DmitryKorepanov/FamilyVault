import '../ffi/native_bridge.dart';
import '../models/models.dart';

/// Сервис для работы с тегами
/// 
/// Теперь тонкая обёртка над NativeBridge — все типизированные методы в bridge.
class TagService {
  final NativeBridge _bridge;

  TagService({NativeBridge? bridge}) : _bridge = bridge ?? NativeBridge.instance;

  /// Добавить тег к файлу
  Future<void> addTag(int fileId, String tag) async {
    _bridge.addTag(fileId, tag.trim().toLowerCase());
  }

  /// Удалить тег с файла
  Future<void> removeTag(int fileId, String tag) async {
    _bridge.removeTag(fileId, tag);
  }

  /// Получить теги файла
  Future<List<String>> getTagsForFile(int fileId) async => _bridge.getTagsForFile(fileId);

  /// Получить все теги
  Future<List<Tag>> getAllTags() async => _bridge.getAllTags();

  /// Получить популярные теги
  Future<List<Tag>> getPopularTags({int limit = 20}) async => _bridge.getPopularTags(limit);

  /// Добавить несколько тегов к файлу
  Future<void> addTags(int fileId, List<String> tags) async {
    for (final tag in tags) {
      await addTag(fileId, tag);
    }
  }

  /// Установить теги файла (заменить все)
  Future<void> setTags(int fileId, List<String> newTags) async {
    final currentTags = await getTagsForFile(fileId);
    
    // Удаляем старые теги, которых нет в новых
    for (final tag in currentTags) {
      if (!newTags.contains(tag)) {
        await removeTag(fileId, tag);
      }
    }
    
    // Добавляем новые теги, которых нет в текущих
    for (final tag in newTags) {
      if (!currentTags.contains(tag)) {
        await addTag(fileId, tag);
      }
    }
  }
}
