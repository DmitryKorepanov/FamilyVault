/// Информация о pairing сессии
class PairingInfo {
  /// 6-значный PIN код
  final String pin;

  /// Base64 данные для QR-кода
  final String qrData;

  /// Время истечения (Unix timestamp в секундах)
  final int expiresAt;

  /// Время создания (Unix timestamp в секундах)
  final int createdAt;

  const PairingInfo({
    required this.pin,
    required this.qrData,
    required this.expiresAt,
    required this.createdAt,
  });

  /// Сколько секунд осталось до истечения
  int get secondsRemaining {
    final now = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    return expiresAt - now;
  }

  /// Истёк ли PIN
  bool get isExpired => secondsRemaining <= 0;

  /// Время истечения как DateTime
  DateTime get expiresAtDateTime =>
      DateTime.fromMillisecondsSinceEpoch(expiresAt * 1000);

  /// Время создания как DateTime
  DateTime get createdAtDateTime =>
      DateTime.fromMillisecondsSinceEpoch(createdAt * 1000);

  factory PairingInfo.fromJson(Map<String, dynamic> json) {
    final pin = json['pin'] as String? ?? '';
    final qrData = json['qrData'] as String? ?? '';
    final expiresAtRaw = json['expiresAt'];
    final createdAtRaw = json['createdAt'];

    final expiresAt = expiresAtRaw is num ? expiresAtRaw.toInt() : 0;
    final createdAt = createdAtRaw is num ? createdAtRaw.toInt() : 0;

    return PairingInfo(
      pin: pin,
      qrData: qrData,
      expiresAt: expiresAt,
      createdAt: createdAt,
    );
  }

  Map<String, dynamic> toJson() => {
        'pin': pin,
        'qrData': qrData,
        'expiresAt': expiresAt,
        'createdAt': createdAt,
      };

  @override
  String toString() =>
      'PairingInfo(pin: $pin, expiresIn: ${secondsRemaining}s)';
}

/// Результат присоединения к семье
enum JoinResult {
  success(0),
  invalidPin(1),
  expired(2),
  rateLimited(3),
  networkError(4),
  alreadyConfigured(5),
  internalError(99);

  final int value;
  const JoinResult(this.value);

  static JoinResult fromValue(int v) =>
      JoinResult.values.firstWhere((e) => e.value == v,
          orElse: () => internalError);

  bool get isSuccess => this == JoinResult.success;

  String get message {
    switch (this) {
      case JoinResult.success:
        return 'Успешно присоединились к семье';
      case JoinResult.invalidPin:
        return 'Неверный PIN-код';
      case JoinResult.expired:
        return 'Код истёк, попросите новый';
      case JoinResult.rateLimited:
        return 'Слишком много попыток, подождите';
      case JoinResult.networkError:
        return 'Ошибка сети';
      case JoinResult.alreadyConfigured:
        return 'Устройство уже в семье';
      case JoinResult.internalError:
        return 'Внутренняя ошибка';
    }
  }
}

