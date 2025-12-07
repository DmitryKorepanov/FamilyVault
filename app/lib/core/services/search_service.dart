import '../ffi/native_bridge.dart';
import '../models/models.dart';

/// Сервис поиска
/// 
/// Теперь тонкая обёртка над NativeBridge — все типизированные методы в bridge.
class SearchService {
  final NativeBridge _bridge;

  SearchService({NativeBridge? bridge}) : _bridge = bridge ?? NativeBridge.instance;

  /// Поиск файлов (полная версия) - runs in background isolate
  Future<List<SearchResult>> search(SearchQuery query) => _bridge.search(query);

  /// Поиск файлов (компактная версия) - runs in background isolate
  Future<List<SearchResultCompact>> searchCompact(SearchQuery query) => _bridge.searchCompact(query);

  /// Подсчёт результатов поиска
  Future<int> count(SearchQuery query) async => _bridge.searchCount(query);

  /// Автодополнение
  Future<List<String>> suggest(String prefix, {int limit = 10}) async =>
      _bridge.searchSuggest(prefix, limit);

  /// Быстрый поиск по тексту
  Future<List<FileRecordCompact>> quickSearch(String text, {int limit = 20}) async {
    final query = SearchQuery(text: text, limit: limit);
    final results = await searchCompact(query);
    return results.map((r) => r.file).toList();
  }

  /// Поиск по типу контента
  Future<List<FileRecordCompact>> searchByType(ContentType type, {int limit = 100, int offset = 0}) async {
    final query = SearchQuery(contentType: type, limit: limit, offset: offset, sortBy: SortBy.date);
    final results = await searchCompact(query);
    return results.map((r) => r.file).toList();
  }

  /// Поиск по тегам
  Future<List<FileRecordCompact>> searchByTags(List<String> tags, {int limit = 100, int offset = 0}) async {
    final query = SearchQuery(tags: tags, limit: limit, offset: offset);
    final results = await searchCompact(query);
    return results.map((r) => r.file).toList();
  }
}
