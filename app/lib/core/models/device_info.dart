import 'types.dart';

/// Информация об устройстве в семье (для P2P)
class DeviceInfo {
  /// UUID устройства
  final String deviceId;

  /// Человекочитаемое имя ("John's PC")
  final String deviceName;

  /// Тип устройства
  final DeviceType deviceType;

  /// IP адрес в локальной сети
  final String ipAddress;

  /// Порт сервиса P2P
  final int servicePort;

  /// Когда последний раз видели (Unix timestamp)
  final int lastSeenAt;

  /// Онлайн ли сейчас (виден через discovery)
  final bool isOnline;

  /// Установлено ли соединение
  final bool isConnected;

  /// Количество файлов на устройстве
  final int fileCount;

  /// Когда последний раз синхронизировались
  final int lastSyncAt;

  const DeviceInfo({
    required this.deviceId,
    required this.deviceName,
    this.deviceType = DeviceType.desktop,
    this.ipAddress = '',
    this.servicePort = 0,
    this.lastSeenAt = 0,
    this.isOnline = false,
    this.isConnected = false,
    this.fileCount = 0,
    this.lastSyncAt = 0,
  });

  /// Это текущее устройство?
  bool get isThisDevice => ipAddress.isEmpty && servicePort == 0;

  /// Последний раз видели как DateTime
  DateTime? get lastSeenAtDateTime => lastSeenAt > 0
      ? DateTime.fromMillisecondsSinceEpoch(lastSeenAt * 1000)
      : null;

  /// Последняя синхронизация как DateTime
  DateTime? get lastSyncAtDateTime => lastSyncAt > 0
      ? DateTime.fromMillisecondsSinceEpoch(lastSyncAt * 1000)
      : null;

  factory DeviceInfo.fromJson(Map<String, dynamic> json) {
    return DeviceInfo(
      deviceId: json['deviceId'] as String,
      deviceName: json['deviceName'] as String,
      deviceType: DeviceType.fromValue(json['deviceType'] as int? ?? 0),
      ipAddress: json['ipAddress'] as String? ?? '',
      servicePort: json['servicePort'] as int? ?? 0,
      lastSeenAt: json['lastSeenAt'] as int? ?? 0,
      isOnline: json['isOnline'] as bool? ?? false,
      isConnected: json['isConnected'] as bool? ?? false,
      fileCount: json['fileCount'] as int? ?? 0,
      lastSyncAt: json['lastSyncAt'] as int? ?? 0,
    );
  }

  Map<String, dynamic> toJson() => {
        'deviceId': deviceId,
        'deviceName': deviceName,
        'deviceType': deviceType.value,
        'ipAddress': ipAddress,
        'servicePort': servicePort,
        'lastSeenAt': lastSeenAt,
        'isOnline': isOnline,
        'isConnected': isConnected,
        'fileCount': fileCount,
        'lastSyncAt': lastSyncAt,
      };

  DeviceInfo copyWith({
    String? deviceId,
    String? deviceName,
    DeviceType? deviceType,
    String? ipAddress,
    int? servicePort,
    int? lastSeenAt,
    bool? isOnline,
    bool? isConnected,
    int? fileCount,
    int? lastSyncAt,
  }) {
    return DeviceInfo(
      deviceId: deviceId ?? this.deviceId,
      deviceName: deviceName ?? this.deviceName,
      deviceType: deviceType ?? this.deviceType,
      ipAddress: ipAddress ?? this.ipAddress,
      servicePort: servicePort ?? this.servicePort,
      lastSeenAt: lastSeenAt ?? this.lastSeenAt,
      isOnline: isOnline ?? this.isOnline,
      isConnected: isConnected ?? this.isConnected,
      fileCount: fileCount ?? this.fileCount,
      lastSyncAt: lastSyncAt ?? this.lastSyncAt,
    );
  }

  @override
  String toString() =>
      'DeviceInfo(name: $deviceName, online: $isOnline, connected: $isConnected)';

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is DeviceInfo &&
          runtimeType == other.runtimeType &&
          deviceId == other.deviceId;

  @override
  int get hashCode => deviceId.hashCode;
}

