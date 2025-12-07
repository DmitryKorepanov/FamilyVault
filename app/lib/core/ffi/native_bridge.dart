import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';

import 'package:ffi/ffi.dart';
import 'package:path_provider/path_provider.dart';

import '../models/models.dart';

/// Коды ошибок из C API
enum FVError {
  ok(0),
  invalidArgument(1),
  database(2),
  io(3),
  notFound(4),
  alreadyExists(5),
  authFailed(6),
  network(7),
  busy(8),
  internal(99);

  final int value;
  const FVError(this.value);

  static FVError fromValue(int v) =>
      FVError.values.firstWhere((e) => e.value == v, orElse: () => internal);

  bool get isOk => this == FVError.ok;
}

/// Исключение при ошибке C API
class NativeException implements Exception {
  final FVError error;
  final String message;

  NativeException(this.error, this.message);

  @override
  String toString() => 'NativeException($error): $message';
}

/// Прогресс сканирования
class ScanProgress {
  final int processed;
  final int total;
  final String? currentFile;
  final bool isCountingPhase;

  ScanProgress({
    required this.processed,
    required this.total,
    this.currentFile,
    this.isCountingPhase = false,
  });

  double get progress => total > 0 ? processed / total : 0;
}

/// Прогресс вычисления checksums
class ChecksumProgress {
  final int processed;
  final int total;

  ChecksumProgress({required this.processed, required this.total});

  double get progress => total > 0 ? processed / total : 0;
}

/// Мост к нативной C++ библиотеке через FFI
class NativeBridge {
  static NativeBridge? _instance;
  static NativeBridge get instance => _instance ??= NativeBridge._();

  late final DynamicLibrary _lib;

  // Opaque handles
  Pointer<Void>? _database;
  Pointer<Void>? _indexManager;
  Pointer<Void>? _searchEngine;
  Pointer<Void>? _tagManager;
  Pointer<Void>? _duplicateFinder;

  // Database path (needed for background isolate operations)
  String? _databasePath;

  // Function typedefs
  late final _FvVersion _fvVersion;
  late final _FvErrorMessage _fvErrorMessage;
  late final _FvLastError _fvLastError;
  late final _FvLastErrorMessage _fvLastErrorMessage;
  late final _FvClearError _fvClearError;
  late final _FvFreeString _fvFreeString;

  // Database
  late final _FvDatabaseOpen _fvDatabaseOpen;
  late final _FvDatabaseClose _fvDatabaseClose;
  late final _FvDatabaseInitialize _fvDatabaseInitialize;
  late final _FvDatabaseIsInitialized _fvDatabaseIsInitialized;

  // Index Manager
  late final _FvIndexCreate _fvIndexCreate;
  late final _FvIndexDestroy _fvIndexDestroy;
  late final _FvIndexAddFolder _fvIndexAddFolder;
  late final _FvIndexRemoveFolder _fvIndexRemoveFolder;
  late final _FvIndexGetFolders _fvIndexGetFolders;
  late final _FvIndexScanFolder _fvIndexScanFolder;
  late final _FvIndexScanAll _fvIndexScanAll;
  late final _FvIndexStopScan _fvIndexStopScan;
  late final _FvIndexIsScanning _fvIndexIsScanning;
  late final _FvIndexGetFile _fvIndexGetFile;
  late final _FvIndexDeleteFile _fvIndexDeleteFile;
  late final _FvIndexGetRecent _fvIndexGetRecent;
  late final _FvIndexGetRecentCompact _fvIndexGetRecentCompact;
  late final _FvIndexGetByFolderCompact _fvIndexGetByFolderCompact;
  late final _FvIndexGetStats _fvIndexGetStats;
  late final _FvIndexSetFolderVisibility _fvIndexSetFolderVisibility;
  late final _FvIndexSetFileVisibility _fvIndexSetFileVisibility;

  // Search Engine
  late final _FvSearchCreate _fvSearchCreate;
  late final _FvSearchDestroy _fvSearchDestroy;
  late final _FvSearchQuery _fvSearchQuery;
  late final _FvSearchQueryCompact _fvSearchQueryCompact;
  late final _FvSearchCount _fvSearchCount;
  late final _FvSearchSuggest _fvSearchSuggest;

  // Tag Manager
  late final _FvTagsCreate _fvTagsCreate;
  late final _FvTagsDestroy _fvTagsDestroy;
  late final _FvTagsAdd _fvTagsAdd;
  late final _FvTagsRemove _fvTagsRemove;
  late final _FvTagsGetForFile _fvTagsGetForFile;
  late final _FvTagsGetAll _fvTagsGetAll;
  late final _FvTagsGetPopular _fvTagsGetPopular;

  // Duplicate Finder
  late final _FvDuplicatesCreate _fvDuplicatesCreate;
  late final _FvDuplicatesDestroy _fvDuplicatesDestroy;
  late final _FvDuplicatesFind _fvDuplicatesFind;
  late final _FvDuplicatesStats _fvDuplicatesStats;
  late final _FvDuplicatesWithoutBackup _fvDuplicatesWithoutBackup;
  late final _FvDuplicatesDeleteFile _fvDuplicatesDeleteFile;
  late final _FvDuplicatesComputeChecksums _fvDuplicatesComputeChecksums;

  NativeBridge._() {
    _lib = _loadLibrary();
    _bindFunctions();
  }

  /// Private constructor for creating instances in background isolates
  NativeBridge._forIsolate() {
    _lib = _loadLibrary();
    _bindFunctions();
  }

  bool get isInitialized => _database != null;
  String? get databasePath => _databasePath;

  /// Загрузка библиотеки в зависимости от платформы
  DynamicLibrary _loadLibrary() {
    if (Platform.isWindows) {
      return DynamicLibrary.open('familyvault.dll');
    } else if (Platform.isAndroid) {
      return DynamicLibrary.open('libfamilyvault.so');
    } else if (Platform.isLinux) {
      return DynamicLibrary.open('libfamilyvault.so');
    } else if (Platform.isMacOS) {
      return DynamicLibrary.open('libfamilyvault.dylib');
    }
    throw UnsupportedError('Platform not supported');
  }

  /// Привязка всех функций из C API
  void _bindFunctions() {
    // General
    _fvVersion = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function()>>('fv_version')
        .asFunction();

    _fvErrorMessage = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Int32)>>(
            'fv_error_message')
        .asFunction();

    _fvLastError = _lib
        .lookup<NativeFunction<Int32 Function()>>('fv_last_error')
        .asFunction();

    _fvLastErrorMessage = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function()>>('fv_last_error_message')
        .asFunction();

    _fvClearError = _lib
        .lookup<NativeFunction<Void Function()>>('fv_clear_error')
        .asFunction();

    _fvFreeString = _lib
        .lookup<NativeFunction<Void Function(Pointer<Utf8>)>>('fv_free_string')
        .asFunction();

    // Database
    _fvDatabaseOpen = _lib
        .lookup<
            NativeFunction<
                Pointer<Void> Function(
                    Pointer<Utf8>, Pointer<Int32>)>>('fv_database_open')
        .asFunction();

    _fvDatabaseClose = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_database_close')
        .asFunction();

    _fvDatabaseInitialize = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_database_initialize')
        .asFunction();

    _fvDatabaseIsInitialized = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_database_is_initialized')
        .asFunction();

    // Index Manager
    _fvIndexCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>)>>(
            'fv_index_create')
        .asFunction();

    _fvIndexDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>(
            'fv_index_destroy')
        .asFunction();

    _fvIndexAddFolder = _lib
        .lookup<
            NativeFunction<
                Int64 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>,
                    Int32)>>('fv_index_add_folder')
        .asFunction();

    _fvIndexRemoveFolder = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64)>>(
            'fv_index_remove_folder')
        .asFunction();

    _fvIndexGetFolders = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_index_get_folders')
        .asFunction();

    _fvIndexScanFolder = _lib
        .lookup<
            NativeFunction<
                Int32 Function(Pointer<Void>, Int64,
                    Pointer<NativeFunction<ScanCallbackNative>>, Pointer<Void>)>>('fv_index_scan_folder')
        .asFunction();

    _fvIndexScanAll = _lib
        .lookup<
            NativeFunction<
                Int32 Function(Pointer<Void>,
                    Pointer<NativeFunction<ScanCallbackNative>>, Pointer<Void>)>>('fv_index_scan_all')
        .asFunction();

    _fvIndexStopScan = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>(
            'fv_index_stop_scan')
        .asFunction();

    _fvIndexIsScanning = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_index_is_scanning')
        .asFunction();

    _fvIndexGetFile = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int64)>>(
            'fv_index_get_file')
        .asFunction();

    _fvIndexDeleteFile = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64, Int32)>>(
            'fv_index_delete_file')
        .asFunction();

    _fvIndexGetRecent = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int32)>>(
            'fv_index_get_recent')
        .asFunction();

    _fvIndexGetRecentCompact = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int32)>>(
            'fv_index_get_recent_compact')
        .asFunction();

    _fvIndexGetByFolderCompact = _lib
        .lookup<
            NativeFunction<
                Pointer<Utf8> Function(
                    Pointer<Void>, Int64, Int32, Int32)>>('fv_index_get_by_folder_compact')
        .asFunction();

    _fvIndexGetStats = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_index_get_stats')
        .asFunction();

    _fvIndexSetFolderVisibility = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64, Int32)>>(
            'fv_index_set_folder_visibility')
        .asFunction();

    _fvIndexSetFileVisibility = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64, Int32)>>(
            'fv_index_set_file_visibility')
        .asFunction();

    // Search Engine
    _fvSearchCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>)>>(
            'fv_search_create')
        .asFunction();

    _fvSearchDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>(
            'fv_search_destroy')
        .asFunction();

    _fvSearchQuery = _lib
        .lookup<
            NativeFunction<
                Pointer<Utf8> Function(
                    Pointer<Void>, Pointer<Utf8>)>>('fv_search_query')
        .asFunction();

    _fvSearchQueryCompact = _lib
        .lookup<
            NativeFunction<
                Pointer<Utf8> Function(
                    Pointer<Void>, Pointer<Utf8>)>>('fv_search_query_compact')
        .asFunction();

    _fvSearchCount = _lib
        .lookup<NativeFunction<Int64 Function(Pointer<Void>, Pointer<Utf8>)>>(
            'fv_search_count')
        .asFunction();

    _fvSearchSuggest = _lib
        .lookup<
            NativeFunction<
                Pointer<Utf8> Function(
                    Pointer<Void>, Pointer<Utf8>, Int32)>>('fv_search_suggest')
        .asFunction();

    // Tag Manager
    _fvTagsCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>)>>(
            'fv_tags_create')
        .asFunction();

    _fvTagsDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>(
            'fv_tags_destroy')
        .asFunction();

    _fvTagsAdd = _lib
        .lookup<
            NativeFunction<
                Int32 Function(
                    Pointer<Void>, Int64, Pointer<Utf8>)>>('fv_tags_add')
        .asFunction();

    _fvTagsRemove = _lib
        .lookup<
            NativeFunction<
                Int32 Function(
                    Pointer<Void>, Int64, Pointer<Utf8>)>>('fv_tags_remove')
        .asFunction();

    _fvTagsGetForFile = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int64)>>(
            'fv_tags_get_for_file')
        .asFunction();

    _fvTagsGetAll = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_tags_get_all')
        .asFunction();

    _fvTagsGetPopular = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int32)>>(
            'fv_tags_get_popular')
        .asFunction();

    // Duplicate Finder
    _fvDuplicatesCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>, Pointer<Void>)>>(
            'fv_duplicates_create')
        .asFunction();

    _fvDuplicatesDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>(
            'fv_duplicates_destroy')
        .asFunction();

    _fvDuplicatesFind = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_duplicates_find')
        .asFunction();

    _fvDuplicatesStats = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_duplicates_stats')
        .asFunction();

    _fvDuplicatesWithoutBackup = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_duplicates_without_backup')
        .asFunction();

    _fvDuplicatesDeleteFile = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64)>>(
            'fv_duplicates_delete_file')
        .asFunction();

    _fvDuplicatesComputeChecksums = _lib
        .lookup<
            NativeFunction<
                Int32 Function(
                    Pointer<Void>,
                    Pointer<NativeFunction<ChecksumCallbackNative>>,
                    Pointer<Void>)>>('fv_duplicates_compute_checksums')
        .asFunction();
  }

  // ═══════════════════════════════════════════════════════════
  // Helpers
  // ═══════════════════════════════════════════════════════════

  void _checkError(int errorCode, [String context = '']) {
    if (errorCode != 0) {
      final error = FVError.fromValue(errorCode);
      final message = getErrorMessage(errorCode);
      throw NativeException(error, context.isEmpty ? message : '$context: $message');
    }
  }

  /// Check for errors using the thread-local last error
  void _checkLastError([String context = '']) {
    final errorCode = _fvLastError();
    if (errorCode != 0) {
      final error = FVError.fromValue(errorCode);
      final messagePtr = _fvLastErrorMessage();
      final message = messagePtr.toDartString();
      _fvClearError();
      throw NativeException(error, context.isEmpty ? message : '$context: $message');
    }
  }

  String _readAndFreeString(Pointer<Utf8> ptr) {
    if (ptr == nullptr) return '';
    final str = ptr.toDartString();
    _fvFreeString(ptr);
    return str;
  }

  /// Read a string from native and check for null (indicates error)
  String _readAndFreeJsonStringOrThrow(Pointer<Utf8> ptr, [String context = '']) {
    if (ptr == nullptr) {
      _checkLastError(context);
      // If no error was set but ptr is null, throw generic error
      throw NativeException(FVError.internal, context.isEmpty ? 'Unknown error' : context);
    }
    return _readAndFreeString(ptr);
  }

  // ═══════════════════════════════════════════════════════════
  // General
  // ═══════════════════════════════════════════════════════════

  /// Получить версию библиотеки
  String getVersion() {
    final ptr = _fvVersion();
    return ptr.toDartString();
  }

  /// Получить текст ошибки
  String getErrorMessage(int errorCode) {
    final ptr = _fvErrorMessage(errorCode);
    return ptr.toDartString();
  }

  // ═══════════════════════════════════════════════════════════
  // Database
  // ═══════════════════════════════════════════════════════════

  /// Инициализировать базу данных
  Future<void> initDatabase([String? path]) async {
    if (_database != null) return;

    final dbPath = path ?? await _getDefaultDbPath();
    _databasePath = dbPath;
    
    final pathPtr = dbPath.toNativeUtf8();
    final errorPtr = calloc<Int32>();

    try {
      _database = _fvDatabaseOpen(pathPtr, errorPtr);
      _checkError(errorPtr.value, 'Failed to open database');

      final initError = _fvDatabaseInitialize(_database!);
      _checkError(initError, 'Failed to initialize database');

      // Создаём менеджеры
      _indexManager = _fvIndexCreate(_database!);
      if (_indexManager == nullptr) _checkLastError('Failed to create index manager');
      
      _searchEngine = _fvSearchCreate(_database!);
      if (_searchEngine == nullptr) _checkLastError('Failed to create search engine');
      
      _tagManager = _fvTagsCreate(_database!);
      if (_tagManager == nullptr) _checkLastError('Failed to create tag manager');
      
      // DuplicateFinder gets IndexManager reference for consistent deletion
      _duplicateFinder = _fvDuplicatesCreate(_database!, _indexManager!);
      if (_duplicateFinder == nullptr) _checkLastError('Failed to create duplicate finder');
    } finally {
      calloc.free(pathPtr);
      calloc.free(errorPtr);
    }
  }

  /// Initialize database for use in a background isolate (skip migrations)
  void _initDatabaseSyncForWorker(String dbPath) {
    if (_database != null) return;

    _databasePath = dbPath;
    final pathPtr = dbPath.toNativeUtf8();
    final errorPtr = calloc<Int32>();

    try {
      _database = _fvDatabaseOpen(pathPtr, errorPtr);
      if (errorPtr.value != 0) {
        throw NativeException(
          FVError.fromValue(errorPtr.value),
          'Failed to open database',
        );
      }

      // Check if already initialized - don't re-run migrations
      if (_fvDatabaseIsInitialized(_database!) == 0) {
        final initError = _fvDatabaseInitialize(_database!);
        if (initError != 0) {
          throw NativeException(FVError.fromValue(initError), 'Failed to initialize database');
        }
      }

      _indexManager = _fvIndexCreate(_database!);
      _searchEngine = _fvSearchCreate(_database!);
      _duplicateFinder = _fvDuplicatesCreate(_database!, _indexManager ?? nullptr);
    } finally {
      calloc.free(pathPtr);
      calloc.free(errorPtr);
    }
  }

  Future<String> _getDefaultDbPath() async {
    final dir = await getApplicationSupportDirectory();
    final dbDir = Directory('${dir.path}/FamilyVault');
    if (!await dbDir.exists()) {
      await dbDir.create(recursive: true);
    }
    return '${dbDir.path}/familyvault.db';
  }

  /// Закрыть базу данных
  void closeDatabase() {
    // Destroy managers first (in reverse order of creation)
    // Each manager automatically decrements database ref count
    if (_duplicateFinder != null) {
      _fvDuplicatesDestroy(_duplicateFinder!);
      _duplicateFinder = null;
    }
    if (_tagManager != null) {
      _fvTagsDestroy(_tagManager!);
      _tagManager = null;
    }
    if (_searchEngine != null) {
      _fvSearchDestroy(_searchEngine!);
      _searchEngine = null;
    }
    if (_indexManager != null) {
      _fvIndexDestroy(_indexManager!);
      _indexManager = null;
    }
    if (_database != null) {
      final result = _fvDatabaseClose(_database!);
      if (result != 0) {
        // Log but don't throw - we're cleaning up
        final error = FVError.fromValue(result);
        if (error == FVError.busy) {
          // This shouldn't happen if we destroyed all managers
          throw NativeException(error, 'Database still has active references');
        }
      }
      _database = null;
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Index Manager
  // ═══════════════════════════════════════════════════════════

  /// Добавить папку для отслеживания
  int addFolder(String path, String name, int visibility) {
    _ensureIndexManager();
    final pathPtr = path.toNativeUtf8();
    final namePtr = name.toNativeUtf8();
    try {
      final id = _fvIndexAddFolder(_indexManager!, pathPtr, namePtr, visibility);
      if (id < 0) {
        _checkLastError('Failed to add folder');
      }
      return id;
    } finally {
      calloc.free(pathPtr);
      calloc.free(namePtr);
    }
  }

  /// Удалить папку из отслеживания
  void removeFolder(int folderId) {
    _ensureIndexManager();
    final error = _fvIndexRemoveFolder(_indexManager!, folderId);
    _checkError(error, 'Failed to remove folder');
  }

  /// Получить список папок
  List<WatchedFolder> getFolders() {
    _ensureIndexManager();
    final ptr = _fvIndexGetFolders(_indexManager!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get folders');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => WatchedFolder.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Проверить, идёт ли сканирование
  bool isScanning() {
    _ensureIndexManager();
    return _fvIndexIsScanning(_indexManager!) != 0;
  }

  /// Сканировать папку (runs in background isolate with cooperative cancellation)
  Future<void> scanFolder(int folderId, {void Function(ScanProgress)? onProgress}) async {
    _ensureIndexManager();
    
    final dbPath = _databasePath;
    if (dbPath == null) {
      throw StateError('Database path not set');
    }

    final progressPort = ReceivePort();
    final completer = Completer<void>();

    progressPort.listen((message) {
      if (message is Map) {
        if (message['type'] == 'progress' && onProgress != null) {
          onProgress(ScanProgress(
            processed: message['processed'] as int,
            total: message['total'] as int,
            currentFile: message['currentFile'] as String?,
            isCountingPhase: message['isCountingPhase'] as bool? ?? false,
          ));
        } else if (message['type'] == 'done') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.complete();
          }
        } else if (message['type'] == 'error') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.completeError(
              NativeException(FVError.internal, message['message'] as String),
            );
          }
        }
      }
    });

    await Isolate.spawn(
      _scanFolderIsolate,
      _ScanFolderParams(
        dbPath: dbPath,
        folderId: folderId,
        progressPort: progressPort.sendPort,
      ),
    );

    await completer.future;
  }

  /// Сканировать все папки (runs in background isolate)
  Future<void> scanAll({void Function(ScanProgress)? onProgress}) async {
    _ensureIndexManager();
    
    final dbPath = _databasePath;
    if (dbPath == null) {
      throw StateError('Database path not set');
    }

    final progressPort = ReceivePort();
    final completer = Completer<void>();

    progressPort.listen((message) {
      if (message is Map) {
        if (message['type'] == 'progress' && onProgress != null) {
          onProgress(ScanProgress(
            processed: message['processed'] as int,
            total: message['total'] as int,
            currentFile: message['currentFile'] as String?,
            isCountingPhase: message['isCountingPhase'] as bool? ?? false,
          ));
        } else if (message['type'] == 'done') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.complete();
          }
        } else if (message['type'] == 'error') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.completeError(
              NativeException(FVError.internal, message['message'] as String),
            );
          }
        }
      }
    });

    await Isolate.spawn(
      _scanAllIsolate,
      _ScanAllParams(
        dbPath: dbPath,
        progressPort: progressPort.sendPort,
      ),
    );

    await completer.future;
  }

  /// Остановить сканирование (cooperative - scan checks for cancellation)
  void stopScan() {
    _ensureIndexManager();
    _fvIndexStopScan(_indexManager!);
  }

  /// Получить файл по ID
  FileRecord? getFile(int fileId) {
    _ensureIndexManager();
    final ptr = _fvIndexGetFile(_indexManager!, fileId);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get file');
    if (json.isEmpty || json == 'null') return null;
    return FileRecord.fromJson(jsonDecode(json) as Map<String, dynamic>);
  }

  /// Удалить файл из индекса (и опционально с диска)
  void deleteFile(int fileId, {bool deleteFromDisk = true}) {
    _ensureIndexManager();
    final error = _fvIndexDeleteFile(_indexManager!, fileId, deleteFromDisk ? 1 : 0);
    _checkError(error, 'Failed to delete file');
  }

  /// Получить недавние файлы (полная версия)
  List<FileRecord> getRecentFiles(int limit) {
    _ensureIndexManager();
    final ptr = _fvIndexGetRecent(_indexManager!, limit);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get recent files');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => FileRecord.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Получить недавние файлы (компактная версия)
  List<FileRecordCompact> getRecentFilesCompact(int limit) {
    _ensureIndexManager();
    final ptr = _fvIndexGetRecentCompact(_indexManager!, limit);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get recent files');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => FileRecordCompact.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Получить файлы папки (компактная версия)
  List<FileRecordCompact> getFilesByFolderCompact(int folderId, int limit, int offset) {
    _ensureIndexManager();
    final ptr = _fvIndexGetByFolderCompact(_indexManager!, folderId, limit, offset);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get folder files');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => FileRecordCompact.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Получить статистику индекса
  IndexStats getStats() {
    _ensureIndexManager();
    final ptr = _fvIndexGetStats(_indexManager!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get stats');
    return IndexStats.fromJson(jsonDecode(json) as Map<String, dynamic>);
  }

  /// Установить видимость папки
  void setFolderVisibility(int folderId, int visibility) {
    _ensureIndexManager();
    final error = _fvIndexSetFolderVisibility(_indexManager!, folderId, visibility);
    _checkError(error, 'Failed to set folder visibility');
  }

  /// Установить видимость файла
  void setFileVisibility(int fileId, int visibility) {
    _ensureIndexManager();
    final error = _fvIndexSetFileVisibility(_indexManager!, fileId, visibility);
    _checkError(error, 'Failed to set file visibility');
  }

  void _ensureIndexManager() {
    if (_indexManager == null) {
      throw StateError('Database not initialized. Call initDatabase() first.');
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Search Engine
  // ═══════════════════════════════════════════════════════════

  /// Поиск (полная версия) - runs in background isolate
  Future<List<SearchResult>> search(SearchQuery query) async {
    _ensureSearchEngine();
    
    final dbPath = _databasePath;
    if (dbPath == null) {
      throw StateError('Database path not set');
    }
    
    final queryJson = jsonEncode(query.toJson());

    final json = await Isolate.run(() {
      final bridge = NativeBridge._forIsolate();
      bridge._initDatabaseSyncForWorker(dbPath);
      try {
        final queryPtr = queryJson.toNativeUtf8();
        try {
          final ptr = bridge._fvSearchQuery(bridge._searchEngine!, queryPtr);
          return bridge._readAndFreeJsonStringOrThrow(ptr, 'Search failed');
        } finally {
          calloc.free(queryPtr);
        }
      } finally {
        bridge.closeDatabase();
      }
    });
    
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => SearchResult.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Поиск (компактная версия) - runs in background isolate
  Future<List<SearchResultCompact>> searchCompact(SearchQuery query) async {
    _ensureSearchEngine();
    
    final dbPath = _databasePath;
    if (dbPath == null) {
      throw StateError('Database path not set');
    }
    
    final queryJson = jsonEncode(query.toJson());

    final json = await Isolate.run(() {
      final bridge = NativeBridge._forIsolate();
      bridge._initDatabaseSyncForWorker(dbPath);
      try {
        final queryPtr = queryJson.toNativeUtf8();
        try {
          final ptr = bridge._fvSearchQueryCompact(bridge._searchEngine!, queryPtr);
          return bridge._readAndFreeJsonStringOrThrow(ptr, 'Compact search failed');
        } finally {
          calloc.free(queryPtr);
        }
      } finally {
        bridge.closeDatabase();
      }
    });
    
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => SearchResultCompact.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Подсчёт результатов
  int searchCount(SearchQuery query) {
    _ensureSearchEngine();
    final queryPtr = jsonEncode(query.toJson()).toNativeUtf8();
    try {
      final result = _fvSearchCount(_searchEngine!, queryPtr);
      if (result < 0) {
        _checkLastError('Search count failed');
      }
      return result;
    } finally {
      calloc.free(queryPtr);
    }
  }

  /// Автодополнение
  List<String> searchSuggest(String prefix, int limit) {
    _ensureSearchEngine();
    final prefixPtr = prefix.toNativeUtf8();
    try {
      final ptr = _fvSearchSuggest(_searchEngine!, prefixPtr, limit);
      final json = _readAndFreeJsonStringOrThrow(ptr, 'Suggest failed');
      if (json.isEmpty) return [];
      final list = jsonDecode(json) as List<dynamic>;
      return list.cast<String>();
    } finally {
      calloc.free(prefixPtr);
    }
  }

  void _ensureSearchEngine() {
    if (_searchEngine == null) {
      throw StateError('Database not initialized. Call initDatabase() first.');
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Tag Manager
  // ═══════════════════════════════════════════════════════════

  /// Добавить тег к файлу
  void addTag(int fileId, String tag) {
    _ensureTagManager();
    final tagPtr = tag.toNativeUtf8();
    try {
      final error = _fvTagsAdd(_tagManager!, fileId, tagPtr);
      _checkError(error, 'Failed to add tag');
    } finally {
      calloc.free(tagPtr);
    }
  }

  /// Удалить тег с файла
  void removeTag(int fileId, String tag) {
    _ensureTagManager();
    final tagPtr = tag.toNativeUtf8();
    try {
      final error = _fvTagsRemove(_tagManager!, fileId, tagPtr);
      _checkError(error, 'Failed to remove tag');
    } finally {
      calloc.free(tagPtr);
    }
  }

  /// Получить теги файла
  List<String> getTagsForFile(int fileId) {
    _ensureTagManager();
    final ptr = _fvTagsGetForFile(_tagManager!, fileId);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get file tags');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.cast<String>();
  }

  /// Получить все теги
  List<Tag> getAllTags() {
    _ensureTagManager();
    final ptr = _fvTagsGetAll(_tagManager!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get all tags');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => Tag.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Получить популярные теги
  List<Tag> getPopularTags(int limit) {
    _ensureTagManager();
    final ptr = _fvTagsGetPopular(_tagManager!, limit);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get popular tags');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => Tag.fromJson(e as Map<String, dynamic>)).toList();
  }

  void _ensureTagManager() {
    if (_tagManager == null) {
      throw StateError('Database not initialized. Call initDatabase() first.');
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Duplicate Finder
  // ═══════════════════════════════════════════════════════════

  /// Найти дубликаты
  List<DuplicateGroup> findDuplicates() {
    _ensureDuplicateFinder();
    final ptr = _fvDuplicatesFind(_duplicateFinder!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to find duplicates');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => DuplicateGroup.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Статистика дубликатов
  DuplicateStats getDuplicatesStats() {
    _ensureDuplicateFinder();
    final ptr = _fvDuplicatesStats(_duplicateFinder!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get duplicate stats');
    return DuplicateStats.fromJson(jsonDecode(json) as Map<String, dynamic>);
  }

  /// Файлы без бэкапа
  List<FileRecord> getFilesWithoutBackup() {
    _ensureDuplicateFinder();
    final ptr = _fvDuplicatesWithoutBackup(_duplicateFinder!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to find files without backup');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => FileRecord.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Удалить файл через DuplicateFinder (uses IndexManager internally)
  void deleteDuplicateFile(int fileId) {
    _ensureDuplicateFinder();
    final error = _fvDuplicatesDeleteFile(_duplicateFinder!, fileId);
    _checkError(error, 'Failed to delete file');
  }

  /// Вычислить недостающие checksums (runs in background isolate)
  Future<void> computeChecksums({void Function(int processed, int total)? onProgress}) async {
    _ensureDuplicateFinder();
    
    final dbPath = _databasePath;
    if (dbPath == null) {
      throw StateError('Database path not set');
    }

    final progressPort = ReceivePort();
    final completer = Completer<void>();

    progressPort.listen((message) {
      if (message is Map) {
        if (message['type'] == 'progress' && onProgress != null) {
          onProgress(
            message['processed'] as int,
            message['total'] as int,
          );
        } else if (message['type'] == 'done') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.complete();
          }
        } else if (message['type'] == 'error') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.completeError(
              NativeException(FVError.internal, message['message'] as String),
            );
          }
        }
      }
    });

    await Isolate.spawn(
      _computeChecksumsIsolate,
      _ComputeChecksumsParams(
        dbPath: dbPath,
        progressPort: progressPort.sendPort,
      ),
    );

    await completer.future;
  }

  void _ensureDuplicateFinder() {
    if (_duplicateFinder == null) {
      throw StateError('Database not initialized. Call initDatabase() first.');
    }
  }
}

// ═══════════════════════════════════════════════════════════
// Background Isolate Functions
// ═══════════════════════════════════════════════════════════

/// Parameters for scanFolder isolate
class _ScanFolderParams {
  final String dbPath;
  final int folderId;
  final SendPort progressPort;

  _ScanFolderParams({
    required this.dbPath,
    required this.folderId,
    required this.progressPort,
  });
}

/// Parameters for scanAll isolate
class _ScanAllParams {
  final String dbPath;
  final SendPort progressPort;

  _ScanAllParams({
    required this.dbPath,
    required this.progressPort,
  });
}

/// Parameters for computeChecksums isolate
class _ComputeChecksumsParams {
  final String dbPath;
  final SendPort progressPort;

  _ComputeChecksumsParams({
    required this.dbPath,
    required this.progressPort,
  });
}

// Store send ports for callbacks keyed by isolate
final Map<int, SendPort> _isolateSendPorts = {};
int _nextIsolateId = 1;

int _registerSendPort(SendPort port) {
  final id = _nextIsolateId++;
  _isolateSendPorts[id] = port;
  return id;
}

void _unregisterSendPort(int id) {
  _isolateSendPorts.remove(id);
}

SendPort? _getSendPort(int id) {
  return _isolateSendPorts[id];
}

/// Static callback function for scan progress
void _scanProgressCallback(
  int processed,
  int total,
  Pointer<Utf8> currentFile,
  Pointer<Void> userData,
) {
  final portId = userData.address;
  final port = _getSendPort(portId);
  if (port != null) {
    port.send({
      'type': 'progress',
      'processed': processed,
      'total': total,
      'currentFile': currentFile != nullptr ? currentFile.toDartString() : null,
    });
  }
}

/// Static callback function for checksum progress
void _checksumProgressCallback(int processed, int total, Pointer<Void> userData) {
  final portId = userData.address;
  final port = _getSendPort(portId);
  if (port != null) {
    port.send({
      'type': 'progress',
      'processed': processed,
      'total': total,
    });
  }
}

/// Isolate entry point for scanning a folder
void _scanFolderIsolate(_ScanFolderParams params) {
  final portId = _registerSendPort(params.progressPort);
  
  try {
    final bridge = NativeBridge._forIsolate();
    bridge._initDatabaseSyncForWorker(params.dbPath);
    
    final callback = NativeCallable<ScanCallbackNative>.isolateLocal(
      _scanProgressCallback,
    );
    
    try {
      final userDataPtr = Pointer<Void>.fromAddress(portId);
      
      final error = bridge._fvIndexScanFolder(
        bridge._indexManager!,
        params.folderId,
        callback.nativeFunction,
        userDataPtr,
      );
      
      if (error != 0) {
        params.progressPort.send({
          'type': 'error',
          'message': bridge.getErrorMessage(error),
        });
      } else {
        params.progressPort.send({'type': 'done'});
      }
    } finally {
      callback.close();
      bridge.closeDatabase();
    }
  } catch (e) {
    params.progressPort.send({
      'type': 'error',
      'message': e.toString(),
    });
  } finally {
    _unregisterSendPort(portId);
  }
}

/// Isolate entry point for scanning all folders
void _scanAllIsolate(_ScanAllParams params) {
  final portId = _registerSendPort(params.progressPort);
  
  try {
    final bridge = NativeBridge._forIsolate();
    bridge._initDatabaseSyncForWorker(params.dbPath);
    
    final callback = NativeCallable<ScanCallbackNative>.isolateLocal(
      _scanProgressCallback,
    );
    
    try {
      final userDataPtr = Pointer<Void>.fromAddress(portId);
      
      final error = bridge._fvIndexScanAll(
        bridge._indexManager!,
        callback.nativeFunction,
        userDataPtr,
      );
      
      if (error != 0) {
        params.progressPort.send({
          'type': 'error',
          'message': bridge.getErrorMessage(error),
        });
      } else {
        params.progressPort.send({'type': 'done'});
      }
    } finally {
      callback.close();
      bridge.closeDatabase();
    }
  } catch (e) {
    params.progressPort.send({
      'type': 'error',
      'message': e.toString(),
    });
  } finally {
    _unregisterSendPort(portId);
  }
}

/// Isolate entry point for computing checksums
void _computeChecksumsIsolate(_ComputeChecksumsParams params) {
  final portId = _registerSendPort(params.progressPort);
  
  try {
    final bridge = NativeBridge._forIsolate();
    bridge._initDatabaseSyncForWorker(params.dbPath);
    
    final callback = NativeCallable<ChecksumCallbackNative>.isolateLocal(
      _checksumProgressCallback,
    );
    
    try {
      final userDataPtr = Pointer<Void>.fromAddress(portId);
      
      final error = bridge._fvDuplicatesComputeChecksums(
        bridge._duplicateFinder!,
        callback.nativeFunction,
        userDataPtr,
      );
      
      if (error != 0) {
        params.progressPort.send({
          'type': 'error',
          'message': bridge.getErrorMessage(error),
        });
      } else {
        params.progressPort.send({'type': 'done'});
      }
    } finally {
      callback.close();
      bridge.closeDatabase();
    }
  } catch (e) {
    params.progressPort.send({
      'type': 'error',
      'message': e.toString(),
    });
  } finally {
    _unregisterSendPort(portId);
  }
}

// ═══════════════════════════════════════════════════════════
// Native function typedefs
// ═══════════════════════════════════════════════════════════

// Callback typedefs
typedef ScanCallbackNative = Void Function(
    Int64 processed, Int64 total, Pointer<Utf8> currentFile, Pointer<Void> userData);
typedef ChecksumCallbackNative = Void Function(
    Int32 processed, Int32 total, Pointer<Void> userData);

// General
typedef _FvVersion = Pointer<Utf8> Function();
typedef _FvErrorMessage = Pointer<Utf8> Function(int error);
typedef _FvLastError = int Function();
typedef _FvLastErrorMessage = Pointer<Utf8> Function();
typedef _FvClearError = void Function();
typedef _FvFreeString = void Function(Pointer<Utf8> str);

// Database
typedef _FvDatabaseOpen = Pointer<Void> Function(Pointer<Utf8> path, Pointer<Int32> outError);
typedef _FvDatabaseClose = int Function(Pointer<Void> db);
typedef _FvDatabaseInitialize = int Function(Pointer<Void> db);
typedef _FvDatabaseIsInitialized = int Function(Pointer<Void> db);

// Index Manager
typedef _FvIndexCreate = Pointer<Void> Function(Pointer<Void> db);
typedef _FvIndexDestroy = void Function(Pointer<Void> mgr);
typedef _FvIndexAddFolder = int Function(
    Pointer<Void> mgr, Pointer<Utf8> path, Pointer<Utf8> name, int visibility);
typedef _FvIndexRemoveFolder = int Function(Pointer<Void> mgr, int folderId);
typedef _FvIndexGetFolders = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvIndexScanFolder = int Function(Pointer<Void> mgr, int folderId,
    Pointer<NativeFunction<ScanCallbackNative>> cb, Pointer<Void> userData);
typedef _FvIndexScanAll = int Function(Pointer<Void> mgr,
    Pointer<NativeFunction<ScanCallbackNative>> cb, Pointer<Void> userData);
typedef _FvIndexStopScan = void Function(Pointer<Void> mgr);
typedef _FvIndexIsScanning = int Function(Pointer<Void> mgr);
typedef _FvIndexGetFile = Pointer<Utf8> Function(Pointer<Void> mgr, int fileId);
typedef _FvIndexDeleteFile = int Function(Pointer<Void> mgr, int fileId, int deleteFromDisk);
typedef _FvIndexGetRecent = Pointer<Utf8> Function(Pointer<Void> mgr, int limit);
typedef _FvIndexGetRecentCompact = Pointer<Utf8> Function(Pointer<Void> mgr, int limit);
typedef _FvIndexGetByFolderCompact = Pointer<Utf8> Function(
    Pointer<Void> mgr, int folderId, int limit, int offset);
typedef _FvIndexGetStats = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvIndexSetFolderVisibility = int Function(
    Pointer<Void> mgr, int folderId, int visibility);
typedef _FvIndexSetFileVisibility = int Function(
    Pointer<Void> mgr, int fileId, int visibility);

// Search Engine
typedef _FvSearchCreate = Pointer<Void> Function(Pointer<Void> db);
typedef _FvSearchDestroy = void Function(Pointer<Void> engine);
typedef _FvSearchQuery = Pointer<Utf8> Function(Pointer<Void> engine, Pointer<Utf8> queryJson);
typedef _FvSearchQueryCompact = Pointer<Utf8> Function(
    Pointer<Void> engine, Pointer<Utf8> queryJson);
typedef _FvSearchCount = int Function(Pointer<Void> engine, Pointer<Utf8> queryJson);
typedef _FvSearchSuggest = Pointer<Utf8> Function(
    Pointer<Void> engine, Pointer<Utf8> prefix, int limit);

// Tag Manager
typedef _FvTagsCreate = Pointer<Void> Function(Pointer<Void> db);
typedef _FvTagsDestroy = void Function(Pointer<Void> mgr);
typedef _FvTagsAdd = int Function(Pointer<Void> mgr, int fileId, Pointer<Utf8> tag);
typedef _FvTagsRemove = int Function(Pointer<Void> mgr, int fileId, Pointer<Utf8> tag);
typedef _FvTagsGetForFile = Pointer<Utf8> Function(Pointer<Void> mgr, int fileId);
typedef _FvTagsGetAll = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvTagsGetPopular = Pointer<Utf8> Function(Pointer<Void> mgr, int limit);

// Duplicate Finder
typedef _FvDuplicatesCreate = Pointer<Void> Function(Pointer<Void> db, Pointer<Void> indexMgr);
typedef _FvDuplicatesDestroy = void Function(Pointer<Void> finder);
typedef _FvDuplicatesFind = Pointer<Utf8> Function(Pointer<Void> finder);
typedef _FvDuplicatesStats = Pointer<Utf8> Function(Pointer<Void> finder);
typedef _FvDuplicatesWithoutBackup = Pointer<Utf8> Function(Pointer<Void> finder);
typedef _FvDuplicatesDeleteFile = int Function(Pointer<Void> finder, int fileId);
typedef _FvDuplicatesComputeChecksums = int Function(Pointer<Void> finder,
    Pointer<NativeFunction<ChecksumCallbackNative>> cb, Pointer<Void> userData);
