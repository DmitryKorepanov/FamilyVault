import 'dart:io';
import 'dart:developer' as dev;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';

import '../ffi/native_bridge.dart';
import '../models/models.dart';
import '../services/services.dart';
import 'network_providers.dart';

// ═══════════════════════════════════════════════════════════
// Сервисы (синглтоны)
// ═══════════════════════════════════════════════════════════

/// Провайдер NativeBridge
final nativeBridgeProvider = Provider<NativeBridge>((ref) {
  return NativeBridge.instance;
});

/// Провайдер DatabaseService
final databaseServiceProvider = Provider<DatabaseService>((ref) {
  return DatabaseService(bridge: ref.watch(nativeBridgeProvider));
});

/// Провайдер IndexService
final indexServiceProvider = Provider<IndexService>((ref) {
  return IndexService(bridge: ref.watch(nativeBridgeProvider));
});

/// Провайдер SearchService
final searchServiceProvider = Provider<SearchService>((ref) {
  return SearchService(bridge: ref.watch(nativeBridgeProvider));
});

/// Провайдер TagService
final tagServiceProvider = Provider<TagService>((ref) {
  return TagService(bridge: ref.watch(nativeBridgeProvider));
});

/// Провайдер DuplicateService
final duplicateServiceProvider = Provider<DuplicateService>((ref) {
  return DuplicateService(bridge: ref.watch(nativeBridgeProvider));
});

/// Провайдер ContentIndexerService
final contentIndexerServiceProvider = Provider<ContentIndexerService>((ref) {
  return ContentIndexerService(ref.watch(nativeBridgeProvider));
});

// ═══════════════════════════════════════════════════════════
// Инициализация
// ═══════════════════════════════════════════════════════════

/// Состояние инициализации приложения
final appInitializedProvider = FutureProvider<bool>((ref) async {
  final db = ref.watch(databaseServiceProvider);
  await db.initialize();
  
  // Resume text extraction for any pending files (e.g. after app restart)
  final contentIndexer = ref.read(contentIndexerServiceProvider);
  final pending = contentIndexer.enqueueUnprocessed();
  if (pending > 0) {
    contentIndexer.start();
  }
  
  // Initialize P2P networking components
  await _initializeP2P(ref);
  
  return true;
});

/// Инициализация P2P компонентов (вызывается один раз при старте)
Future<void> _initializeP2P(Ref ref) async {
  final networkService = ref.read(networkServiceProvider);
  
  // Skip if family not configured
  if (!networkService.isFamilyConfigured()) {
    dev.log('P2P: Skipping - family not configured', name: 'P2P');
    return;
  }
  
  try {
    // 1. Start network FIRST (creates NetworkManager, TLS server, discovery)
    dev.log('P2P: Starting network...', name: 'P2P');
    networkService.startNetwork();
    
    // 2. Setup index sync (requires NetworkManager to exist!)
    dev.log('P2P: Setting up index sync...', name: 'P2P');
    networkService.setupIndexSync();
    
    // 3. Setup file transfer cache directory
    final cacheDir = await getApplicationCacheDirectory();
    final p2pCacheDir = Directory('${cacheDir.path}/p2p_files');
    if (!p2pCacheDir.existsSync()) {
      p2pCacheDir.createSync(recursive: true);
    }
    networkService.setFileCacheDir(p2pCacheDir.path);
    
    // 4. Start Android foreground service for multicast/wake locks
    if (Platform.isAndroid) {
      try {
        final androidService = ref.read(androidNetworkServiceProvider);
        await androidService.startService();
        await androidService.requestBatteryOptimizationExemption();
        dev.log('P2P: Android network service started', name: 'P2P');
      } catch (e) {
        dev.log('P2P: Android service failed: $e', name: 'P2P');
        // Continue - P2P can work without foreground service, just less reliably
      }
    }
    
    dev.log('P2P: Initialization complete', name: 'P2P');
  } catch (e, stack) {
    // Log error with stack trace so issues are visible
    dev.log('P2P: Initialization FAILED: $e', name: 'P2P', error: e, stackTrace: stack);
    // Don't rethrow - app can still function without P2P
  }
}

/// Версия C++ библиотеки
final coreVersionProvider = Provider<String>((ref) {
  final bridge = ref.watch(nativeBridgeProvider);
  try {
    return bridge.getVersion();
  } catch (_) {
    return 'unknown';
  }
});

// ═══════════════════════════════════════════════════════════
// Версионирование данных (для реактивных зависимостей)
// ═══════════════════════════════════════════════════════════

/// Версия данных индекса — инкрементируется при изменениях.
/// Все провайдеры, зависящие от данных индекса, следят за этой версией
/// и автоматически обновляются при её изменении.
final indexVersionProvider = StateProvider<int>((ref) => 0);

/// Инкрементирует версию индекса, вызывая обновление всех зависимых провайдеров
void _incrementIndexVersion(Ref ref) {
  ref.read(indexVersionProvider.notifier).state++;
}

// ═══════════════════════════════════════════════════════════
// Статистика индекса
// ═══════════════════════════════════════════════════════════

/// Статистика индекса (автоматически обновляется при изменении indexVersion)
final indexStatsProvider = FutureProvider<IndexStats>((ref) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final service = ref.watch(indexServiceProvider);
  return service.getStats();
});

// ═══════════════════════════════════════════════════════════
// Папки
// ═══════════════════════════════════════════════════════════

/// Список отслеживаемых папок (автоматически обновляется)
final foldersProvider = FutureProvider<List<WatchedFolder>>((ref) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final service = ref.watch(indexServiceProvider);
  return service.getFolders();
});

/// Состояние сканирования (ID папки, которая сейчас сканируется)
final scanningFolderIdProvider = StateProvider<int?>((ref) => null);

/// Нотификатор для управления папками
final foldersNotifierProvider =
    AsyncNotifierProvider<FoldersNotifier, List<WatchedFolder>>(FoldersNotifier.new);

class FoldersNotifier extends AsyncNotifier<List<WatchedFolder>> {
  IndexService get _service => ref.read(indexServiceProvider);

  @override
  Future<List<WatchedFolder>> build() async {
    ref.watch(indexVersionProvider); // Реактивная зависимость
    await ref.watch(appInitializedProvider.future);
    return _service.getFolders();
  }

  /// Добавить папку для отслеживания
  Future<void> addFolder(
    String path, {
    String? name,
    Visibility visibility = Visibility.family,
    bool autoScan = true,
  }) async {
    // Get services BEFORE invalidating providers
    final contentIndexer = ref.read(contentIndexerServiceProvider);
    
    state = const AsyncLoading();
    try {
      final folderId = await _service.addFolder(path, name: name, visibility: visibility);

      if (autoScan && folderId > 0) {
        await _service.scanFolder(folderId);
        
        // Restart content indexer (will process any pending + new files)
        contentIndexer.enqueueUnprocessed();
        contentIndexer.start();
      }

      // Одна строка — все зависимые провайдеры обновятся автоматически
      _incrementIndexVersion(ref);

      state = AsyncData(await _service.getFolders());
    } catch (e, st) {
      state = AsyncError(e, st);
    }
  }

  /// Удалить папку из отслеживания
  Future<void> removeFolder(int folderId) async {
    // Stop background text extraction before deleting
    final contentIndexer = ref.read(contentIndexerServiceProvider);
    contentIndexer.stop();
    
    state = const AsyncLoading();
    try {
      await _service.removeFolder(folderId);
      
      // Restart content indexer for remaining files
      contentIndexer.enqueueUnprocessed();
      contentIndexer.start();
      
      _incrementIndexVersion(ref);
      state = AsyncData(await _service.getFolders());
    } catch (e, st) {
      state = AsyncError(e, st);
    }
  }

  /// Изменить видимость папки
  Future<void> setVisibility(int folderId, Visibility visibility) async {
    try {
      await _service.setFolderVisibility(folderId, visibility);
      _incrementIndexVersion(ref);
      state = AsyncData(await _service.getFolders());
    } catch (e, st) {
      state = AsyncError(e, st);
    }
  }

  /// Сканировать одну папку
  Future<void> scanFolder(int folderId) async {
    // Защита от параллельных сканирований
    final currentScanning = ref.read(scanningFolderIdProvider);
    if (currentScanning != null) {
      return; // Уже идёт сканирование
    }
    
    // Get services BEFORE invalidating providers
    final scanningNotifier = ref.read(scanningFolderIdProvider.notifier);
    final contentIndexer = ref.read(contentIndexerServiceProvider);
    
    // Остановить ContentIndexer чтобы избежать блокировки БД
    contentIndexer.stop();
    
    scanningNotifier.state = folderId;
    try {
      await _service.scanFolder(folderId);
      
      // Restart content indexer (will process any pending + new files)
      contentIndexer.enqueueUnprocessed();
      contentIndexer.start();
      
      _incrementIndexVersion(ref);
      state = AsyncData(await _service.getFolders());
    } catch (e, st) {
      state = AsyncError(e, st);
    } finally {
      scanningNotifier.state = null;
    }
  }

  /// Сканировать все папки
  Future<void> scanAll() async {
    // Защита от параллельных сканирований
    final currentScanning = ref.read(scanningFolderIdProvider);
    if (currentScanning != null) {
      return; // Уже идёт сканирование
    }
    
    // Get services BEFORE invalidating providers
    final scanningNotifier = ref.read(scanningFolderIdProvider.notifier);
    final contentIndexer = ref.read(contentIndexerServiceProvider);
    
    // Остановить ContentIndexer чтобы избежать блокировки БД
    contentIndexer.stop();
    
    // Используем специальный ID -1 для обозначения "сканируем все"
    scanningNotifier.state = -1;
    try {
      await _service.scanAll();
      
      // Restart content indexer (will process any pending + new files)
      contentIndexer.enqueueUnprocessed();
      contentIndexer.start();
      
      _incrementIndexVersion(ref);
      state = AsyncData(await _service.getFolders());
    } catch (e, st) {
      state = AsyncError(e, st);
    } finally {
      scanningNotifier.state = null;
    }
  }

  /// Обновить список папок
  Future<void> refresh() async {
    state = AsyncData(await _service.getFolders());
  }
}

// ═══════════════════════════════════════════════════════════
// Недавние файлы
// ═══════════════════════════════════════════════════════════

/// Недавние файлы (автоматически обновляется при изменении индекса)
final recentFilesProvider = FutureProvider<List<FileRecordCompact>>((ref) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final service = ref.watch(indexServiceProvider);
  return service.getRecentFilesCompact(limit: 50);
});

// ═══════════════════════════════════════════════════════════
// Поиск
// ═══════════════════════════════════════════════════════════

/// Текущий поисковый запрос
final searchQueryProvider = StateProvider<SearchQuery>((ref) => const SearchQuery());

/// Результаты поиска (обновляются при изменении запроса или индекса)
final searchResultsProvider = FutureProvider<List<FileRecordCompact>>((ref) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final query = ref.watch(searchQueryProvider);

  // Если запрос пустой, показываем недавние файлы
  if (query.isEmpty) {
    return ref.watch(recentFilesProvider.future);
  }

  final service = ref.watch(searchServiceProvider);
  final results = await service.searchCompact(query);
  return results.map((r) => r.file).toList();
});

/// Количество результатов поиска
final searchCountProvider = FutureProvider<int>((ref) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final query = ref.watch(searchQueryProvider);

  if (query.isEmpty) return 0;

  final service = ref.watch(searchServiceProvider);
  return service.count(query);
});

/// Автодополнение
final searchSuggestionsProvider = FutureProvider.family<List<String>, String>((ref, prefix) async {
  if (prefix.length < 2) return [];

  await ref.watch(appInitializedProvider.future);
  final service = ref.watch(searchServiceProvider);
  return service.suggest(prefix);
});

// ═══════════════════════════════════════════════════════════
// Комбинированный поиск (локальные + удалённые файлы)
// ═══════════════════════════════════════════════════════════

/// Результаты поиска по удалённым файлам
final remoteSearchResultsProvider = FutureProvider<List<RemoteFileRecord>>((ref) async {
  await ref.watch(appInitializedProvider.future);
  final query = ref.watch(searchQueryProvider);
  
  if (query.isEmpty || query.text.length < 2) return [];
  
  final networkService = ref.watch(networkServiceProvider);
  if (!networkService.isFamilyConfigured()) return [];
  
  return networkService.searchRemoteFiles(query.text, limit: 50);
});

/// Фильтр поиска: локальные/удалённые/все
enum SearchSource { all, local, remote }

/// Текущий фильтр источника поиска
final searchSourceFilterProvider = StateProvider<SearchSource>((ref) => SearchSource.all);

/// Комбинированные результаты поиска
final combinedSearchResultsProvider = FutureProvider<CombinedSearchResults>((ref) async {
  await ref.watch(appInitializedProvider.future);
  final query = ref.watch(searchQueryProvider);
  final sourceFilter = ref.watch(searchSourceFilterProvider);
  
  if (query.isEmpty) {
    final recentFiles = await ref.watch(recentFilesProvider.future);
    return CombinedSearchResults(
      localFiles: recentFiles,
      remoteFiles: [],
      query: '',
    );
  }
  
  // Получаем локальные результаты
  List<FileRecordCompact> localFiles = [];
  if (sourceFilter != SearchSource.remote) {
    final searchService = ref.watch(searchServiceProvider);
    final results = await searchService.searchCompact(query);
    localFiles = results.map((r) => r.file).toList();
  }
  
  // Получаем удалённые результаты
  List<RemoteFileRecord> remoteFiles = [];
  if (sourceFilter != SearchSource.local) {
    remoteFiles = await ref.watch(remoteSearchResultsProvider.future);
  }
  
  return CombinedSearchResults(
    localFiles: localFiles,
    remoteFiles: remoteFiles,
    query: query.text,
  );
});

/// Результаты комбинированного поиска
class CombinedSearchResults {
  final List<FileRecordCompact> localFiles;
  final List<RemoteFileRecord> remoteFiles;
  final String query;
  
  const CombinedSearchResults({
    required this.localFiles,
    required this.remoteFiles,
    required this.query,
  });
  
  int get totalCount => localFiles.length + remoteFiles.length;
  bool get isEmpty => localFiles.isEmpty && remoteFiles.isEmpty;
  bool get hasLocalResults => localFiles.isNotEmpty;
  bool get hasRemoteResults => remoteFiles.isNotEmpty;
}

// ═══════════════════════════════════════════════════════════
// Теги
// ═══════════════════════════════════════════════════════════

/// Все теги (обновляются при изменении индекса)
final allTagsProvider = FutureProvider<List<Tag>>((ref) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final service = ref.watch(tagServiceProvider);
  return service.getAllTags();
});

/// Популярные теги
final popularTagsProvider = FutureProvider<List<Tag>>((ref) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final service = ref.watch(tagServiceProvider);
  return service.getPopularTags();
});

/// Теги конкретного файла
final fileTagsProvider = FutureProvider.family<List<String>, int>((ref, fileId) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final service = ref.watch(tagServiceProvider);
  return service.getTagsForFile(fileId);
});

// ═══════════════════════════════════════════════════════════
// Дубликаты
// ═══════════════════════════════════════════════════════════

/// Статистика дубликатов
final duplicateStatsProvider = FutureProvider<DuplicateStats>((ref) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final service = ref.watch(duplicateServiceProvider);
  return service.getStats();
});

/// Группы дубликатов
final duplicateGroupsProvider = FutureProvider<List<DuplicateGroup>>((ref) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final service = ref.watch(duplicateServiceProvider);
  return service.findDuplicates();
});

/// Файлы без бэкапа
final filesWithoutBackupProvider = FutureProvider<List<FileRecord>>((ref) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final service = ref.watch(duplicateServiceProvider);
  return service.getFilesWithoutBackup();
});

// ═══════════════════════════════════════════════════════════
// Детали файла
// ═══════════════════════════════════════════════════════════

/// Детали файла по ID
final fileDetailsProvider = FutureProvider.family<FileRecord?, int>((ref, fileId) async {
  ref.watch(indexVersionProvider); // Реактивная зависимость
  await ref.watch(appInitializedProvider.future);
  final service = ref.watch(indexServiceProvider);
  return service.getFile(fileId);
});

// ═══════════════════════════════════════════════════════════
// Настройки отображения
// ═══════════════════════════════════════════════════════════

/// Режим отображения (grid/list)
final viewModeProvider = StateProvider<ViewMode>((ref) => ViewMode.grid);

/// Текущая сортировка
final sortByProvider = StateProvider<SortBy>((ref) => SortBy.date);

/// Направление сортировки
final sortAscProvider = StateProvider<bool>((ref) => false);

// ═══════════════════════════════════════════════════════════
// Прогресс сканирования
// ═══════════════════════════════════════════════════════════

/// Текущий прогресс сканирования
final scanProgressProvider = StateProvider<ScanProgress?>((ref) => null);

/// Идёт ли сканирование
final isScanningProvider = Provider<bool>((ref) {
  return ref.watch(scanningFolderIdProvider) != null;
});

// ═══════════════════════════════════════════════════════════
// Извлечение текста (Content Indexer)
// ═══════════════════════════════════════════════════════════

/// Количество файлов без извлечённого текста (автообновление каждые 2 сек)
final pendingContentCountProvider = StreamProvider<int>((ref) async* {
  await ref.watch(appInitializedProvider.future);
  final service = ref.read(contentIndexerServiceProvider);
  
  while (true) {
    yield service.pendingCount;
    await Future.delayed(const Duration(seconds: 2));
  }
});

/// Идёт ли извлечение текста
final isContentIndexingProvider = Provider<bool>((ref) {
  final service = ref.watch(contentIndexerServiceProvider);
  return service.isRunning;
});
