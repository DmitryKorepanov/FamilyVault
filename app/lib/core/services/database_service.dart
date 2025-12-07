import 'dart:io';

import 'package:path_provider/path_provider.dart';

import '../ffi/native_bridge.dart';

/// Сервис для работы с базой данных
class DatabaseService {
  final NativeBridge _bridge;
  bool _initialized = false;

  DatabaseService({NativeBridge? bridge}) : _bridge = bridge ?? NativeBridge.instance;

  bool get isInitialized => _initialized;

  /// Инициализировать базу данных
  Future<void> initialize([String? path]) async {
    if (_initialized) return;
    await _bridge.initDatabase(path);
    _initialized = true;
  }

  /// Закрыть базу данных
  void close() {
    if (!_initialized) return;
    _bridge.closeDatabase();
    _initialized = false;
  }

  /// Получить версию библиотеки
  String getVersion() => _bridge.getVersion();

  /// Пересоздать базу данных (удалить и создать заново)
  Future<void> rebuildDatabase() async {
    // Close current database
    _bridge.closeDatabase();
    _initialized = false;
    
    // Delete database file
    final appDir = await getApplicationSupportDirectory();
    final dbPath = '${appDir.path}/FamilyVault/familyvault.db';
    final dbFile = File(dbPath);
    if (await dbFile.exists()) {
      await dbFile.delete();
    }
    
    // Re-initialize (creates fresh database)
    await _bridge.initDatabase();
    _initialized = true;
  }
}

