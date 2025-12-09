// Network Providers — Riverpod провайдеры для P2P сети

import 'dart:async';
import 'dart:developer' as dev;

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../ffi/native_bridge.dart';
import '../models/models.dart';
import '../services/network_service.dart';
import '../services/android_network_service.dart';

// ═══════════════════════════════════════════════════════════
// Service Provider
// ═══════════════════════════════════════════════════════════

/// NetworkService provider
final networkServiceProvider = Provider<NetworkService>((ref) {
  final bridge = NativeBridge.instance;
  final service = NetworkService(bridge);
  ref.onDispose(() => service.dispose());
  return service;
});

// ═══════════════════════════════════════════════════════════
// Family Status
// ═══════════════════════════════════════════════════════════

/// Проверить, настроена ли семья
final isFamilyConfiguredProvider = Provider<bool>((ref) {
  final service = ref.watch(networkServiceProvider);
  try {
    return service.isFamilyConfigured();
  } catch (e) {
    return false;
  }
});

/// Информация о текущем устройстве
final thisDeviceInfoProvider = Provider<DeviceInfo?>((ref) {
  final service = ref.watch(networkServiceProvider);
  try {
    return service.getThisDeviceInfo();
  } catch (e) {
    return null;
  }
});

// ═══════════════════════════════════════════════════════════
// Discovery
// ═══════════════════════════════════════════════════════════

/// Состояние discovery
final discoveryRunningProvider = StateProvider<bool>((ref) => false);

/// Список обнаруженных устройств (обновляется периодически)
final discoveredDevicesProvider = StateNotifierProvider<DiscoveredDevicesNotifier, List<DeviceInfo>>((ref) {
  final service = ref.watch(networkServiceProvider);
  return DiscoveredDevicesNotifier(service);
});

/// Notifier для списка устройств (event-driven, no polling)
class DiscoveredDevicesNotifier extends StateNotifier<List<DeviceInfo>> {
  final NetworkService _service;
  StreamSubscription<NetworkEvent>? _subscription;

  DiscoveredDevicesNotifier(this._service) : super([]);

  /// Начать слушать события обнаружения устройств
  /// Сеть уже должна быть запущена через appInitializedProvider
  void start() {
    // Network is started at app init, just subscribe to events
    if (_service.isFamilyConfigured() && !_service.isNetworkRunning()) {
      _service.startNetwork();
    }
    _startListening();
    // Initial refresh
    refresh();
  }

  /// Остановить прослушивание (но НЕ останавливать сеть!)
  void stop() {
    _stopListening();
    state = [];
  }

  /// Обновить список вручную
  void refresh() {
    if (_service.isNetworkRunning()) {
      state = _service.getNetworkDiscoveredDevices();
    }
  }

  void _startListening() {
    _subscription?.cancel();
    _subscription = _service.events.listen(_handleEvent);
  }
  
  void _handleEvent(NetworkEvent event) {
    // Refresh list on device discovered/lost events
    if (event.type == NetworkEventType.deviceDiscovered ||
        event.type == NetworkEventType.deviceLost) {
      refresh();
    }
  }

  void _stopListening() {
    _subscription?.cancel();
    _subscription = null;
  }

  @override
  void dispose() {
    _stopListening();
    super.dispose();
  }
}

/// Количество обнаруженных устройств
final discoveredDeviceCountProvider = Provider<int>((ref) {
  return ref.watch(discoveredDevicesProvider).length;
});

// ═══════════════════════════════════════════════════════════
// Network Event Listener
// ═══════════════════════════════════════════════════════════

/// Состояние подключенных устройств (обновляется через события)
final connectedDevicesProvider = StateNotifierProvider<ConnectedDevicesNotifier, List<DeviceInfo>>((ref) {
  final service = ref.watch(networkServiceProvider);
  return ConnectedDevicesNotifier(service);
});

/// Notifier для отслеживания подключенных устройств через события
class ConnectedDevicesNotifier extends StateNotifier<List<DeviceInfo>> {
  final NetworkService _service;
  StreamSubscription<NetworkEvent>? _subscription;
  
  ConnectedDevicesNotifier(this._service) : super([]) {
    _startListening();
  }
  
  void _startListening() {
    _subscription = _service.events.listen(_handleEvent);
    // Initialize with current state
    refresh();
  }
  
  void _handleEvent(NetworkEvent event) {
    dev.log('NetworkEvent: ${event.type} - ${event.data}', name: 'P2P');
    
    switch (event.type) {
      case NetworkEventType.deviceConnected:
        _onDeviceConnected(event.data);
        break;
      case NetworkEventType.deviceDisconnected:
        _onDeviceDisconnected(event.data);
        break;
      case NetworkEventType.error:
        dev.log('Network error: ${event.data}', name: 'P2P', level: 900);
        break;
      default:
        break;
    }
  }
  
  void _onDeviceConnected(Map<String, dynamic> data) {
    final deviceId = data['deviceId'] as String?;
    if (deviceId == null) return;
    
    // Refresh full list from native
    refresh();
  }
  
  void _onDeviceDisconnected(Map<String, dynamic> data) {
    final deviceId = data['deviceId'] as String?;
    if (deviceId == null) return;
    
    // Remove from state
    state = state.where((d) => d.deviceId != deviceId).toList();
  }
  
  void refresh() {
    try {
      state = _service.getConnectedDevices();
    } catch (e) {
      dev.log('Error refreshing connected devices: $e', name: 'P2P');
    }
  }
  
  @override
  void dispose() {
    _subscription?.cancel();
    super.dispose();
  }
}

/// Количество подключенных устройств
final connectedDeviceCountProvider = Provider<int>((ref) {
  return ref.watch(connectedDevicesProvider).length;
});

// ═══════════════════════════════════════════════════════════
// Pairing State
// ═══════════════════════════════════════════════════════════

/// Состояние pairing (создание/присоединение к семье)
enum PairingState {
  idle,
  creatingFamily,
  waitingForJoin,
  joining,
  success,
  error,
}

/// Notifier для pairing процесса
class PairingNotifier extends StateNotifier<PairingState> {
  final NetworkService _service;
  PairingInfo? _currentPairingInfo;
  String? _errorMessage;

  PairingNotifier(this._service) : super(PairingState.idle);

  PairingInfo? get pairingInfo => _currentPairingInfo;
  String? get errorMessage => _errorMessage;

  /// Создать новую семью
  void createFamily() {
    state = PairingState.creatingFamily;
    _errorMessage = null;
    
    try {
      _currentPairingInfo = _service.createFamily();
      if (_currentPairingInfo != null) {
        state = PairingState.waitingForJoin;
      } else {
        state = PairingState.error;
        _errorMessage = 'Failed to create family';
      }
    } catch (e) {
      state = PairingState.error;
      _errorMessage = e.toString();
    }
  }

  /// Перегенерировать PIN
  void regeneratePin() {
    try {
      _currentPairingInfo = _service.regeneratePin();
    } catch (e) {
      _errorMessage = e.toString();
    }
  }

  /// Присоединиться по PIN
  void joinByPin(String pin, {String? host, int port = 0}) {
    state = PairingState.joining;
    _errorMessage = null;

    try {
      final result = _service.joinFamilyByPin(pin, host: host, port: port);
      if (result == JoinResult.success) {
        state = PairingState.success;
      } else {
        state = PairingState.error;
        _errorMessage = _joinResultToMessage(result);
      }
    } catch (e) {
      state = PairingState.error;
      _errorMessage = e.toString();
    }
  }

  /// Присоединиться по QR
  void joinByQR(String qrData) {
    state = PairingState.joining;
    _errorMessage = null;

    try {
      final result = _service.joinFamilyByQR(qrData);
      if (result == JoinResult.success) {
        state = PairingState.success;
      } else {
        state = PairingState.error;
        _errorMessage = _joinResultToMessage(result);
      }
    } catch (e) {
      state = PairingState.error;
      _errorMessage = e.toString();
    }
  }

  /// Отменить pairing
  void cancel() {
    _service.cancelPairing();
    _currentPairingInfo = null;
    state = PairingState.idle;
  }

  /// Сбросить состояние
  void reset() {
    _currentPairingInfo = null;
    _errorMessage = null;
    state = PairingState.idle;
  }

  String _joinResultToMessage(JoinResult result) {
    return result.message;
  }
}

/// Pairing state provider
final pairingStateProvider = StateNotifierProvider<PairingNotifier, PairingState>((ref) {
  final service = ref.watch(networkServiceProvider);
  return PairingNotifier(service);
});

// ═══════════════════════════════════════════════════════════
// Local IP Addresses
// ═══════════════════════════════════════════════════════════

/// Локальные IP адреса этого устройства
final localIpAddressesProvider = Provider<List<String>>((ref) {
  final service = ref.watch(networkServiceProvider);
  try {
    return service.getLocalIpAddresses();
  } catch (e) {
    return [];
  }
});

// ═══════════════════════════════════════════════════════════
// Index Sync
// ═══════════════════════════════════════════════════════════

/// Количество удалённых файлов (с других устройств семьи)
final remoteFileCountProvider = Provider<int>((ref) {
  final service = ref.watch(networkServiceProvider);
  try {
    return service.getRemoteFileCount();
  } catch (e) {
    return 0;
  }
});

/// Удалённые файлы (с других устройств семьи)
final remoteFilesProvider = FutureProvider<List<RemoteFileRecord>>((ref) async {
  final service = ref.watch(networkServiceProvider);
  return service.getRemoteFiles();
});

/// Notifier для управления синхронизацией индекса
class IndexSyncNotifier extends StateNotifier<bool> {
  final NetworkService _service;

  IndexSyncNotifier(this._service) : super(false);

  /// Настроить синхронизацию (связать базу данных)
  void setup() {
    try {
      _service.setupIndexSync();
    } catch (e) {
      // Логируем ошибку (может быть "уже настроено" или реальная проблема)
      dev.log('IndexSync setup: $e', name: 'NetworkProviders');
    }
  }

  /// Запросить синхронизацию с устройством
  void requestSync(String deviceId, {bool fullSync = false}) {
    state = true;
    try {
      _service.requestSync(deviceId, fullSync: fullSync);
    } catch (e) {
      state = false;
      rethrow;
    }
  }

  /// Запросить синхронизацию со всеми подключёнными устройствами
  void syncWithAllConnected() {
    final devices = _service.getConnectedDevices();
    for (final device in devices) {
      try {
        _service.requestSync(device.deviceId);
      } catch (e) {
        // Продолжаем с остальными устройствами
      }
    }
  }
}

/// Index sync state provider
final indexSyncProvider = StateNotifierProvider<IndexSyncNotifier, bool>((ref) {
  final service = ref.watch(networkServiceProvider);
  return IndexSyncNotifier(service);
});

// ═══════════════════════════════════════════════════════════
// File Transfer
// ═══════════════════════════════════════════════════════════

/// Состояние file transfer
class FileTransferState {
  final bool isInitialized;
  final List<FileTransferProgress> activeTransfers;
  final int cacheSize;
  final String? lastError;

  const FileTransferState({
    this.isInitialized = false,
    this.activeTransfers = const [],
    this.cacheSize = 0,
    this.lastError,
  });

  FileTransferState copyWith({
    bool? isInitialized,
    List<FileTransferProgress>? activeTransfers,
    int? cacheSize,
    String? lastError,
  }) {
    return FileTransferState(
      isInitialized: isInitialized ?? this.isInitialized,
      activeTransfers: activeTransfers ?? this.activeTransfers,
      cacheSize: cacheSize ?? this.cacheSize,
      lastError: lastError,
    );
  }
}

/// Notifier для управления file transfer (event-driven, no polling)
class FileTransferNotifier extends StateNotifier<FileTransferState> {
  final NetworkService _service;
  StreamSubscription<NetworkEvent>? _subscription;

  FileTransferNotifier(this._service) : super(const FileTransferState());

  /// Инициализировать file transfer (установить кэш)
  void initialize(String cacheDir) {
    try {
      _service.setFileCacheDir(cacheDir);
      state = state.copyWith(
        isInitialized: true,
        cacheSize: _service.getFileCacheSize(),
      );
      _startListening();
    } catch (e) {
      state = state.copyWith(lastError: e.toString());
    }
  }

  /// Запросить файл с удалённого устройства
  /// @return FileRequestResult с requestId (pending) или cachedPath (cache hit)
  FileRequestResult? requestFile({
    required String deviceId,
    required int fileId,
    required String fileName,
    required int expectedSize,
    String? checksum,
  }) {
    final result = _service.requestRemoteFile(
      deviceId: deviceId,
      fileId: fileId,
      fileName: fileName,
      expectedSize: expectedSize,
      checksum: checksum,
    );
    
    if (result != null && result.isPending) {
      _refresh();
    }
    
    return result;
  }

  /// Отменить запрос файла
  void cancelRequest(String requestId) {
    _service.cancelFileRequest(requestId);
    _refresh();
  }

  /// Отменить все запросы к устройству
  void cancelAllRequests(String deviceId) {
    _service.cancelAllFileRequests(deviceId);
    _refresh();
  }

  /// Очистить кэш
  void clearCache() {
    _service.clearFileCache();
    state = state.copyWith(cacheSize: 0);
  }

  /// Проверить, есть ли файл в кэше
  bool isFileCached(String deviceId, int fileId, {String? checksum}) {
    return _service.isFileCached(deviceId, fileId, checksum: checksum);
  }

  /// Получить путь к кэшированному файлу
  String? getCachedFilePath(String deviceId, int fileId) {
    return _service.getCachedFilePath(deviceId, fileId);
  }

  void _refresh() {
    state = state.copyWith(
      activeTransfers: _service.getActiveTransfers(),
      cacheSize: _service.getFileCacheSize(),
    );
  }

  void _startListening() {
    _subscription?.cancel();
    _subscription = _service.events.listen(_handleEvent);
  }
  
  void _handleEvent(NetworkEvent event) {
    // Update state on file transfer events
    switch (event.type) {
      case NetworkEventType.fileTransferProgress:
      case NetworkEventType.fileTransferComplete:
      case NetworkEventType.fileTransferError:
        _refresh();
        break;
      default:
        break;
    }
  }

  @override
  void dispose() {
    _subscription?.cancel();
    super.dispose();
  }
}

/// File transfer state provider
final fileTransferProvider = StateNotifierProvider<FileTransferNotifier, FileTransferState>((ref) {
  final service = ref.watch(networkServiceProvider);
  return FileTransferNotifier(service);
});

/// Активные передачи файлов
final activeTransfersProvider = Provider<List<FileTransferProgress>>((ref) {
  return ref.watch(fileTransferProvider).activeTransfers;
});

/// Есть ли активные передачи
final hasActiveTransfersProvider = Provider<bool>((ref) {
  return ref.watch(activeTransfersProvider).isNotEmpty;
});

/// Размер кэша файлов (в байтах)
final fileCacheSizeProvider = Provider<int>((ref) {
  return ref.watch(fileTransferProvider).cacheSize;
});

// ═══════════════════════════════════════════════════════════
// Android-specific Network Service
// ═══════════════════════════════════════════════════════════

/// Android Network Service provider
final androidNetworkServiceProvider = Provider<AndroidNetworkService>((ref) {
  return AndroidNetworkService.instance;
});

/// Android network status
final androidNetworkStatusProvider = FutureProvider<AndroidNetworkStatus>((ref) async {
  final service = ref.watch(androidNetworkServiceProvider);
  return service.initialize();
});

/// Нужна ли настройка Android (отключение battery optimization)
final needsAndroidSetupProvider = FutureProvider<bool>((ref) async {
  final service = ref.watch(androidNetworkServiceProvider);
  if (!service.isAndroid) return false;
  return !(await service.isBatteryOptimizationExempt());
});

