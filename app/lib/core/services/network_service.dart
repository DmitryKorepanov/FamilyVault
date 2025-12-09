// NetworkService — сервис для P2P сети
// Обёртка над NativeBridge для работы с P2P

import 'dart:async';
import 'dart:convert';

import '../ffi/native_bridge.dart';
import '../models/models.dart';

/// События сети
/// ВАЖНО: Порядок должен соответствовать NetworkEvent в ffi_network_manager.cpp!
enum NetworkEventType {
  deviceDiscovered,    // 0
  deviceLost,          // 1
  deviceConnected,     // 2
  deviceDisconnected,  // 3
  stateChanged,        // 4
  error,               // 5
  syncProgress,        // 6
  syncComplete,        // 7
  fileTransferProgress,// 8
  fileTransferComplete,// 9
  fileTransferError,   // 10
}

/// Событие сети
class NetworkEvent {
  final NetworkEventType type;
  final Map<String, dynamic> data;

  NetworkEvent(this.type, this.data);

  String? get deviceId => data['deviceId'] as String?;
  String? get error => data['error'] as String?;
  DeviceInfo? get deviceInfo {
    if (data.containsKey('deviceName')) {
      return DeviceInfo.fromJson(data);
    }
    return null;
  }
}

/// Состояние сети
enum NetworkState {
  stopped,
  starting,
  running,
  stopping,
  error,
}

/// Сервис для управления P2P сетью
class NetworkService {
  final NativeBridge _bridge;
  
  final _eventController = StreamController<NetworkEvent>.broadcast();
  
  NetworkService(this._bridge);
  
  /// Поток событий сети
  Stream<NetworkEvent> get events => _eventController.stream;

  /// Инициализировать SecureStorage и FamilyPairing
  void ensurePairingInitialized() {
    _bridge.initPairing();
  }

  // ═══════════════════════════════════════════════════════════
  // Family Pairing
  // ═══════════════════════════════════════════════════════════

  /// Проверить, настроена ли семья
  bool isFamilyConfigured() {
    ensurePairingInitialized();
    return _bridge.isFamilyConfigured();
  }

  /// Получить ID этого устройства
  String getDeviceId() {
    ensurePairingInitialized();
    return _bridge.getDeviceId();
  }

  /// Получить имя этого устройства
  String getDeviceName() {
    ensurePairingInitialized();
    return _bridge.getDeviceName();
  }

  /// Установить имя этого устройства
  void setDeviceName(String name) {
    ensurePairingInitialized();
    _bridge.setDeviceName(name);
  }

  /// Получить тип устройства
  DeviceType getDeviceType() {
    ensurePairingInitialized();
    return _bridge.getDeviceType();
  }

  /// Создать новую семью
  /// Возвращает информацию для pairing (PIN, QR)
  PairingInfo? createFamily() {
    ensurePairingInitialized();
    return _bridge.createFamily();
  }

  /// Перегенерировать PIN
  PairingInfo? regeneratePin() {
    ensurePairingInitialized();
    return _bridge.regeneratePin();
  }

  /// Отменить активную pairing сессию
  void cancelPairing() {
    ensurePairingInitialized();
    _bridge.cancelPairing();
  }

  /// Есть ли активная pairing сессия
  bool hasPendingPairing() {
    ensurePairingInitialized();
    return _bridge.hasPendingPairing();
  }

  /// Присоединиться к семье по PIN
  JoinResult joinFamilyByPin(String pin, {String? host, int port = 0}) {
    ensurePairingInitialized();
    return _bridge.joinFamilyByPin(pin, host: host, port: port);
  }

  /// Присоединиться к семье по QR
  JoinResult joinFamilyByQR(String qrData) {
    ensurePairingInitialized();
    return _bridge.joinFamilyByQR(qrData);
  }

  /// Сбросить настройки семьи
  void resetFamily() {
    ensurePairingInitialized();
    _bridge.resetFamily();
  }

  /// Запустить pairing сервер
  void startPairingServer({int port = 0}) {
    ensurePairingInitialized();
    _bridge.startPairingServer(port: port);
  }

  /// Остановить pairing сервер
  void stopPairingServer() {
    ensurePairingInitialized();
    _bridge.stopPairingServer();
  }

  /// Проверить, запущен ли pairing сервер
  bool isPairingServerRunning() {
    ensurePairingInitialized();
    return _bridge.isPairingServerRunning();
  }

  /// Получить порт pairing сервера
  int getPairingServerPort() {
    ensurePairingInitialized();
    return _bridge.getPairingServerPort();
  }

  /// Получить информацию о текущем устройстве
  DeviceInfo getThisDeviceInfo() {
    ensurePairingInitialized();
    return _bridge.getThisDeviceInfo();
  }

  // ═══════════════════════════════════════════════════════════
  // Network Discovery
  // ═══════════════════════════════════════════════════════════

  /// Запустить обнаружение устройств
  void startDiscovery() {
    ensurePairingInitialized();
    _bridge.startDiscovery();
  }

  /// Остановить обнаружение устройств
  void stopDiscovery() {
    _bridge.stopDiscovery();
  }

  /// Проверить, запущено ли обнаружение
  bool isDiscoveryRunning() {
    return _bridge.isDiscoveryRunning();
  }

  /// Получить список обнаруженных устройств
  List<DeviceInfo> getDiscoveredDevices() {
    return _bridge.getDiscoveredDevices();
  }

  /// Получить количество обнаруженных устройств
  int getDiscoveredDeviceCount() {
    return _bridge.getDiscoveredDeviceCount();
  }

  /// Получить информацию об устройстве по ID
  DeviceInfo? getDiscoveredDevice(String deviceId) {
    return _bridge.getDiscoveredDevice(deviceId);
  }

  /// Получить локальные IP адреса
  List<String> getLocalIpAddresses() {
    return _bridge.getLocalIpAddresses();
  }

  // ═══════════════════════════════════════════════════════════
  // Network Manager
  // ═══════════════════════════════════════════════════════════

  /// Запустить P2P сеть (discovery + server)
  void startNetwork({int port = 0}) {
    ensurePairingInitialized();
    _bridge.startNetwork(
      port: port,
      onEvent: _handleNetworkEvent,
    );
  }

  /// Обработка событий сети из native callback
  void _handleNetworkEvent(int eventCode, String jsonData) {
    final idx = eventCode.clamp(0, NetworkEventType.values.length - 1);
    final type = NetworkEventType.values[idx];
    
    Map<String, dynamic> data = {};
    if (jsonData.isNotEmpty && jsonData != '{}') {
      try {
        data = Map<String, dynamic>.from(
          const JsonDecoder().convert(jsonData) as Map,
        );
      } catch (_) {
        // Ignore JSON parse errors
      }
    }
    
    _eventController.add(NetworkEvent(type, data));
  }

  /// Остановить P2P сеть
  void stopNetwork() {
    _bridge.stopNetwork();
  }

  /// Проверить, запущена ли сеть
  bool isNetworkRunning() {
    return _bridge.isNetworkRunning();
  }

  /// Получить состояние сети
  NetworkState getNetworkState() {
    final state = _bridge.getNetworkState();
    final idx = state.clamp(0, NetworkState.values.length - 1);
    return NetworkState.values[idx];
  }

  /// Получить порт сервера
  int getNetworkPort() {
    return _bridge.getNetworkPort();
  }

  /// Получить обнаруженные устройства (через NetworkManager)
  List<DeviceInfo> getNetworkDiscoveredDevices() {
    return _bridge.getNetworkDiscoveredDevices();
  }

  /// Получить подключённые устройства
  List<DeviceInfo> getConnectedDevices() {
    return _bridge.getConnectedDevices();
  }

  /// Подключиться к устройству по ID
  /// 
  /// Returns immediately - connection happens asynchronously in C++.
  /// Listen to [onDeviceConnected] stream for completion.
  /// Listen to [onError] stream for failures.
  void connectToDevice(String deviceId) {
    _bridge.connectToDevice(deviceId);
  }

  /// Подключиться к устройству по адресу
  /// 
  /// Returns immediately - connection happens asynchronously in C++.
  /// Listen to [onDeviceConnected] stream for completion.
  /// Listen to [onError] stream for failures.
  void connectToAddress(String host, int port) {
    _bridge.connectToAddress(host, port);
  }

  /// Отключиться от устройства
  void disconnectFromDevice(String deviceId) {
    _bridge.disconnectFromDevice(deviceId);
  }

  /// Отключиться от всех устройств
  void disconnectAllDevices() {
    _bridge.disconnectAllDevices();
  }

  /// Проверить подключение к устройству
  bool isConnectedTo(String deviceId) {
    return _bridge.isConnectedTo(deviceId);
  }

  /// Получить последнюю ошибку сети
  String? getNetworkLastError() {
    return _bridge.getNetworkLastError();
  }

  // ═══════════════════════════════════════════════════════════
  // Index Sync
  // ═══════════════════════════════════════════════════════════

  /// Настроить синхронизацию индекса (связать базу данных с сетью)
  /// Должен вызываться после startNetwork() и наличия базы данных
  void setupIndexSync() {
    _bridge.setNetworkDatabase();
  }

  /// Запросить синхронизацию индекса с устройством
  void requestSync(String deviceId, {bool fullSync = false}) {
    _bridge.requestIndexSync(deviceId, fullSync: fullSync);
  }

  /// Получить все удалённые файлы
  List<RemoteFileRecord> getRemoteFiles() {
    final networkFiles = _bridge.getNetworkRemoteFiles();
    if (networkFiles.isNotEmpty) return networkFiles;
    return _bridge.getRemoteFiles();
  }

  /// Получить удалённые файлы с устройства
  List<RemoteFileRecord> getRemoteFilesFrom(String deviceId) {
    return _bridge.getRemoteFilesFrom(deviceId);
  }

  /// Поиск по удалённым файлам
  List<RemoteFileRecord> searchRemoteFiles(String query, {int limit = 50}) {
    final networkFiles = _bridge.searchNetworkRemoteFiles(query, limit: limit);
    if (networkFiles.isNotEmpty) return networkFiles;
    return _bridge.searchRemoteFiles(query, limit: limit);
  }

  /// Количество удалённых файлов
  int getRemoteFileCount() {
    final networkCount = _bridge.getNetworkRemoteFileCount();
    if (networkCount > 0) return networkCount;
    return _bridge.getRemoteFileCount();
  }

  /// Проверить, идёт ли синхронизация
  bool isSyncing() {
    return _bridge.isSyncing();
  }

  // ═══════════════════════════════════════════════════════════
  // File Transfer
  // ═══════════════════════════════════════════════════════════

  /// Установить директорию кэша для файлов
  void setFileCacheDir(String cacheDir) {
    _bridge.setFileCacheDir(cacheDir);
  }

  /// Запросить файл с удалённого устройства
  /// @return FileRequestResult с requestId (pending) или cachedPath (cache hit), или null при ошибке
  FileRequestResult? requestRemoteFile({
    required String deviceId,
    required int fileId,
    required String fileName,
    required int expectedSize,
    String? checksum,
  }) {
    return _bridge.requestRemoteFile(
      deviceId: deviceId,
      fileId: fileId,
      fileName: fileName,
      expectedSize: expectedSize,
      checksum: checksum,
    );
  }

  /// Отменить запрос файла
  void cancelFileRequest(String requestId) {
    _bridge.cancelFileRequest(requestId);
  }

  /// Отменить все запросы файлов к устройству
  void cancelAllFileRequests(String deviceId) {
    _bridge.cancelAllFileRequests(deviceId);
  }

  /// Получить активные передачи
  List<FileTransferProgress> getActiveTransfers() {
    final jsonList = _bridge.getActiveTransfers();
    return jsonList.map((json) => FileTransferProgress.fromJson(json)).toList();
  }

  /// Получить прогресс передачи
  FileTransferProgress? getTransferProgress(String requestId) {
    final json = _bridge.getTransferProgress(requestId);
    if (json == null) return null;
    return FileTransferProgress.fromJson(json);
  }

  /// Проверить, есть ли файл в кэше
  /// @param deviceId ID устройства-источника
  /// @param fileId ID файла
  /// @param checksum Контрольная сумма (опционально)
  bool isFileCached(String deviceId, int fileId, {String? checksum}) {
    return _bridge.isFileCached(deviceId, fileId, checksum: checksum);
  }

  /// Получить путь к кэшированному файлу
  /// @param deviceId ID устройства-источника
  /// @param fileId ID файла
  String? getCachedFilePath(String deviceId, int fileId) {
    return _bridge.getCachedFilePath(deviceId, fileId);
  }

  /// Очистить кэш файлов
  void clearFileCache() {
    _bridge.clearFileCache();
  }

  /// Получить размер кэша (в байтах)
  int getFileCacheSize() {
    return _bridge.getFileCacheSize();
  }

  /// Проверить, есть ли активные передачи
  bool hasActiveTransfers() {
    return _bridge.hasActiveTransfers();
  }

  // ═══════════════════════════════════════════════════════════
  // Cleanup
  // ═══════════════════════════════════════════════════════════

  void dispose() {
    stopNetwork();
    stopDiscovery();
    _eventController.close();
  }
}

