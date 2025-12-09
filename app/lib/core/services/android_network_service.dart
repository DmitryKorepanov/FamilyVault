// AndroidNetworkService — интеграция с Android Foreground Service для P2P

import 'dart:io';

import 'package:flutter/services.dart';

/// Сервис для управления Android-специфичным P2P функционалом.
/// 
/// На Android требуется:
/// - WiFi Multicast Lock для UDP discovery
/// - Foreground Service для стабильной фоновой работы
/// - Battery optimization exemption для непрерывной работы
class AndroidNetworkService {
  static const _channel = MethodChannel('com.familyvault/network_service');
  
  static AndroidNetworkService? _instance;
  static AndroidNetworkService get instance => _instance ??= AndroidNetworkService._();
  
  AndroidNetworkService._();
  
  /// Проверка, работает ли на Android
  bool get isAndroid => Platform.isAndroid;
  
  // ═══════════════════════════════════════════════════════════
  // Network Service Control
  // ═══════════════════════════════════════════════════════════
  
  /// Запустить foreground service для P2P.
  /// 
  /// На Android это:
  /// - Захватывает Multicast Lock для UDP discovery
  /// - Запускает foreground service с уведомлением
  /// - Захватывает Wake Lock для стабильности
  /// 
  /// На других платформах — no-op.
  Future<void> startService() async {
    if (!isAndroid) return;
    
    try {
      await _channel.invokeMethod('startNetworkService');
    } on PlatformException catch (e) {
      throw AndroidNetworkException('Failed to start service: ${e.message}');
    }
  }
  
  /// Остановить foreground service.
  Future<void> stopService() async {
    if (!isAndroid) return;
    
    try {
      await _channel.invokeMethod('stopNetworkService');
    } on PlatformException catch (e) {
      throw AndroidNetworkException('Failed to stop service: ${e.message}');
    }
  }
  
  /// Проверить, запущен ли сервис.
  Future<bool> isServiceRunning() async {
    if (!isAndroid) return true; // На desktop всегда "запущен"
    
    try {
      return await _channel.invokeMethod<bool>('isNetworkServiceRunning') ?? false;
    } on PlatformException {
      return false;
    }
  }
  
  // ═══════════════════════════════════════════════════════════
  // Network State
  // ═══════════════════════════════════════════════════════════
  
  /// Проверить, подключен ли WiFi.
  Future<bool> isWifiConnected() async {
    if (!isAndroid) return true; // На desktop предполагаем наличие сети
    
    try {
      return await _channel.invokeMethod<bool>('isWifiConnected') ?? false;
    } on PlatformException {
      return false;
    }
  }
  
  // ═══════════════════════════════════════════════════════════
  // Battery Optimization
  // ═══════════════════════════════════════════════════════════
  
  /// Запросить исключение из оптимизации батареи.
  /// 
  /// Это позволит приложению работать в фоне без ограничений.
  /// Пользователь увидит системный диалог.
  Future<void> requestBatteryOptimizationExemption() async {
    if (!isAndroid) return;
    
    try {
      await _channel.invokeMethod('requestBatteryOptimizationExemption');
    } on PlatformException catch (e) {
      throw AndroidNetworkException('Failed to request exemption: ${e.message}');
    }
  }
  
  /// Проверить, исключено ли приложение из оптимизации батареи.
  Future<bool> isBatteryOptimizationExempt() async {
    if (!isAndroid) return true; // На desktop нет ограничений
    
    try {
      return await _channel.invokeMethod<bool>('isBatteryOptimizationExempt') ?? false;
    } on PlatformException {
      return false;
    }
  }
  
  // ═══════════════════════════════════════════════════════════
  // Convenience Methods
  // ═══════════════════════════════════════════════════════════
  
  /// Инициализировать P2P для Android.
  /// 
  /// Вызывает:
  /// 1. Запуск foreground service
  /// 2. Проверку WiFi
  /// 3. Рекомендацию отключить оптимизацию батареи (если нужно)
  Future<AndroidNetworkStatus> initialize() async {
    if (!isAndroid) {
      return const AndroidNetworkStatus(
        isServiceRunning: true,
        isWifiConnected: true,
        isBatteryOptimizationExempt: true,
      );
    }
    
    // Запускаем сервис
    await startService();
    
    // Получаем статус
    final results = await Future.wait([
      isServiceRunning(),
      isWifiConnected(),
      isBatteryOptimizationExempt(),
    ]);
    
    return AndroidNetworkStatus(
      isServiceRunning: results[0],
      isWifiConnected: results[1],
      isBatteryOptimizationExempt: results[2],
    );
  }
  
  /// Остановить P2P на Android.
  Future<void> shutdown() async {
    if (!isAndroid) return;
    await stopService();
  }
}

/// Статус Android P2P сервиса
class AndroidNetworkStatus {
  final bool isServiceRunning;
  final bool isWifiConnected;
  final bool isBatteryOptimizationExempt;
  
  const AndroidNetworkStatus({
    required this.isServiceRunning,
    required this.isWifiConnected,
    required this.isBatteryOptimizationExempt,
  });
  
  bool get isReady => isServiceRunning && isWifiConnected;
  
  String get statusMessage {
    if (!isServiceRunning) return 'Сервис не запущен';
    if (!isWifiConnected) return 'WiFi не подключен';
    if (!isBatteryOptimizationExempt) return 'Рекомендуется отключить оптимизацию батареи';
    return 'Готов к работе';
  }
  
  @override
  String toString() => 'AndroidNetworkStatus('
      'service: $isServiceRunning, '
      'wifi: $isWifiConnected, '
      'battery: $isBatteryOptimizationExempt)';
}

/// Исключение для Android network операций
class AndroidNetworkException implements Exception {
  final String message;
  
  const AndroidNetworkException(this.message);
  
  @override
  String toString() => 'AndroidNetworkException: $message';
}

