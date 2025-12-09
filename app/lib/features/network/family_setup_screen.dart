// FamilySetupScreen — создание или присоединение к семье

import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:mobile_scanner/mobile_scanner.dart';
import 'package:qr_flutter/qr_flutter.dart';

import '../../core/models/models.dart';
import '../../core/providers/network_providers.dart';
import '../../theme/app_theme.dart';

/// Экран настройки семьи
class FamilySetupScreen extends ConsumerStatefulWidget {
  const FamilySetupScreen({super.key});

  @override
  ConsumerState<FamilySetupScreen> createState() => _FamilySetupScreenState();
}

class _FamilySetupScreenState extends ConsumerState<FamilySetupScreen> {
  @override
  Widget build(BuildContext context) {
    final isConfigured = ref.watch(isFamilyConfiguredProvider);
    final pairingState = ref.watch(pairingStateProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Семья'),
        actions: [
          if (isConfigured)
            IconButton(
              icon: const Icon(Icons.logout),
              tooltip: 'Покинуть семью',
              onPressed: _showLeaveDialog,
            ),
        ],
      ),
      body: AnimatedSwitcher(
        duration: const Duration(milliseconds: 300),
        child: isConfigured
            ? _FamilyConfiguredView(onLeave: _showLeaveDialog)
            : _FamilySetupView(pairingState: pairingState),
      ),
    );
  }

  void _showLeaveDialog() {
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Покинуть семью?'),
        content: const Text(
          'Вы больше не сможете видеть файлы других устройств и '
          'они не смогут видеть ваши. Локальные файлы останутся на месте.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Отмена'),
          ),
          FilledButton(
            style: FilledButton.styleFrom(
              backgroundColor: Theme.of(context).colorScheme.error,
            ),
            onPressed: () {
              ref.read(networkServiceProvider).resetFamily();
              Navigator.pop(context);
              ref.invalidate(isFamilyConfiguredProvider);
            },
            child: const Text('Покинуть'),
          ),
        ],
      ),
    );
  }
}

/// Вид когда семья настроена
class _FamilyConfiguredView extends ConsumerWidget {
  final VoidCallback onLeave;

  const _FamilyConfiguredView({required this.onLeave});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final deviceInfo = ref.watch(thisDeviceInfoProvider);
    final localIps = ref.watch(localIpAddressesProvider);
    final theme = Theme.of(context);

    return SingleChildScrollView(
      padding: AppSizes.contentPadding(context),
      child: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 600),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Статус
              Card(
                child: ListTile(
                  leading: Container(
                    padding: const EdgeInsets.all(12),
                    decoration: BoxDecoration(
                      color: Colors.green.withValues(alpha: 0.1),
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: const Icon(Icons.check_circle, color: Colors.green),
                  ),
                  title: const Text('Семья настроена'),
                  subtitle: const Text('Устройства в одной WiFi сети будут видеть друг друга'),
                ),
              ),
              const SizedBox(height: 24),

              // Это устройство
              Text('Это устройство', style: theme.textTheme.titleMedium),
              const SizedBox(height: 12),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    children: [
                      Row(
                        children: [
                          Container(
                            padding: const EdgeInsets.all(12),
                            decoration: BoxDecoration(
                              color: theme.colorScheme.primaryContainer,
                              borderRadius: BorderRadius.circular(12),
                            ),
                            child: Icon(
                              _deviceTypeIcon(deviceInfo?.deviceType),
                              color: theme.colorScheme.onPrimaryContainer,
                            ),
                          ),
                          const SizedBox(width: 16),
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  deviceInfo?.deviceName ?? 'Неизвестно',
                                  style: theme.textTheme.titleMedium,
                                ),
                                Text(
                                  'ID: ${_shortId(deviceInfo?.deviceId)}',
                                  style: theme.textTheme.bodySmall?.copyWith(
                                    color: theme.colorScheme.outline,
                                  ),
                                ),
                              ],
                            ),
                          ),
                          IconButton(
                            icon: const Icon(Icons.edit),
                            tooltip: 'Изменить имя',
                            onPressed: () => _showRenameDialog(context, ref, deviceInfo?.deviceName ?? ''),
                          ),
                        ],
                      ),
                      if (localIps.isNotEmpty) ...[
                        const Divider(height: 24),
                        Row(
                          children: [
                            Icon(Icons.wifi, size: 16, color: theme.colorScheme.outline),
                            const SizedBox(width: 8),
                            Text(
                              localIps.first,
                              style: theme.textTheme.bodyMedium?.copyWith(
                                fontFamily: 'monospace',
                              ),
                            ),
                            if (localIps.length > 1)
                              Text(
                                ' (+${localIps.length - 1})',
                                style: theme.textTheme.bodySmall?.copyWith(
                                  color: theme.colorScheme.outline,
                                ),
                              ),
                          ],
                        ),
                      ],
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 24),

              // Добавить устройство
              Text('Добавить устройство', style: theme.textTheme.titleMedium),
              const SizedBox(height: 12),
              Card(
                child: ListTile(
                  leading: const Icon(Icons.add_circle_outline),
                  title: const Text('Пригласить в семью'),
                  subtitle: const Text('Показать PIN или QR-код'),
                  trailing: const Icon(Icons.chevron_right),
                  onTap: () => _showInviteDialog(context, ref),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  IconData _deviceTypeIcon(DeviceType? type) {
    switch (type) {
      case DeviceType.desktop:
        return Icons.computer;
      case DeviceType.mobile:
        return Icons.smartphone;
      case DeviceType.tablet:
        return Icons.tablet;
      default:
        return Icons.devices;
    }
  }

  String _shortId(String? id) {
    if (id == null || id.length < 8) return id ?? '';
    return '${id.substring(0, 8)}...';
  }

  void _showRenameDialog(BuildContext context, WidgetRef ref, String currentName) {
    final controller = TextEditingController(text: currentName);
    
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Имя устройства'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: const InputDecoration(
            hintText: 'Введите имя',
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Отмена'),
          ),
          FilledButton(
            onPressed: () {
              final name = controller.text.trim();
              if (name.isNotEmpty) {
                ref.read(networkServiceProvider).setDeviceName(name);
                ref.invalidate(thisDeviceInfoProvider);
              }
              Navigator.pop(context);
            },
            child: const Text('Сохранить'),
          ),
        ],
      ),
    );
  }

  void _showInviteDialog(BuildContext context, WidgetRef ref) {
    final notifier = ref.read(pairingStateProvider.notifier);
    notifier.createFamily();

    showDialog(
      context: context,
      builder: (context) => const _InviteDialog(),
    );
  }
}

/// Вид настройки семьи (создание/присоединение)
class _FamilySetupView extends ConsumerWidget {
  final PairingState pairingState;

  const _FamilySetupView({required this.pairingState});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);

    return SingleChildScrollView(
      padding: AppSizes.contentPadding(context),
      child: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 500),
          child: Column(
            children: [
              const SizedBox(height: 32),
              
              // Иконка
              Container(
                padding: const EdgeInsets.all(24),
                decoration: BoxDecoration(
                  color: theme.colorScheme.primaryContainer,
                  shape: BoxShape.circle,
                ),
                child: Icon(
                  Icons.family_restroom,
                  size: 48,
                  color: theme.colorScheme.onPrimaryContainer,
                ),
              ),
              const SizedBox(height: 24),

              // Заголовок
              Text(
                'Семейный доступ',
                style: theme.textTheme.headlineSmall,
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 8),
              Text(
                'Объедините устройства чтобы искать файлы на всех устройствах сразу',
                style: theme.textTheme.bodyMedium?.copyWith(
                  color: theme.colorScheme.outline,
                ),
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 48),

              // Кнопки
              SizedBox(
                width: double.infinity,
                child: FilledButton.icon(
                  onPressed: () => _showCreateFamilyDialog(context, ref),
                  icon: const Icon(Icons.add),
                  label: const Text('Создать семью'),
                ),
              ),
              const SizedBox(height: 12),
              SizedBox(
                width: double.infinity,
                child: OutlinedButton.icon(
                  onPressed: () => _showJoinDialog(context, ref),
                  icon: const Icon(Icons.login),
                  label: const Text('Присоединиться'),
                ),
              ),
              const SizedBox(height: 48),

              // Информация
              Container(
                padding: const EdgeInsets.all(16),
                decoration: BoxDecoration(
                  color: theme.colorScheme.surfaceContainerHighest.withValues(alpha: 0.5),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Column(
                  children: [
                    _InfoRow(
                      icon: Icons.wifi,
                      text: 'Работает в локальной WiFi сети',
                    ),
                    const SizedBox(height: 8),
                    _InfoRow(
                      icon: Icons.lock,
                      text: 'Шифрование TLS 1.3',
                    ),
                    const SizedBox(height: 8),
                    _InfoRow(
                      icon: Icons.visibility_off,
                      text: 'Приватные файлы не видны другим',
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  void _showCreateFamilyDialog(BuildContext context, WidgetRef ref) {
    final notifier = ref.read(pairingStateProvider.notifier);
    notifier.createFamily();

    showDialog(
      context: context,
      builder: (context) => const _InviteDialog(),
    );
  }

  void _showJoinDialog(BuildContext context, WidgetRef ref) {
    showDialog(
      context: context,
      builder: (context) => const _JoinDialog(),
    );
  }
}

/// Ряд информации
class _InfoRow extends StatelessWidget {
  final IconData icon;
  final String text;

  const _InfoRow({required this.icon, required this.text});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Row(
      children: [
        Icon(icon, size: 18, color: theme.colorScheme.outline),
        const SizedBox(width: 12),
        Expanded(
          child: Text(
            text,
            style: theme.textTheme.bodySmall?.copyWith(
              color: theme.colorScheme.outline,
            ),
          ),
        ),
      ],
    );
  }
}

/// Диалог приглашения (показ PIN/QR)
class _InviteDialog extends ConsumerStatefulWidget {
  const _InviteDialog();

  @override
  ConsumerState<_InviteDialog> createState() => _InviteDialogState();
}

class _InviteDialogState extends ConsumerState<_InviteDialog> {
  Timer? _timer;
  int _secondsRemaining = 300;

  @override
  void initState() {
    super.initState();
    _startTimer();
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  void _startTimer() {
    final notifier = ref.read(pairingStateProvider.notifier);
    final info = notifier.pairingInfo;
    if (info != null) {
      _secondsRemaining = info.secondsRemaining;
    }

    _timer = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted) {
        setState(() {
          _secondsRemaining--;
          if (_secondsRemaining <= 0) {
            _timer?.cancel();
          }
        });
      }
    });
  }

  /// Генерирует QR URL для сканирования
  String _generateQrUrl(PairingInfo info, List<String> localIps) {
    final host = localIps.isNotEmpty ? localIps.first : '0.0.0.0';
    return 'fv://join?pin=${info.pin}&host=$host&port=45680';
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final notifier = ref.watch(pairingStateProvider.notifier);
    final info = notifier.pairingInfo;
    final localIps = ref.watch(localIpAddressesProvider);

    return AlertDialog(
      title: const Text('Пригласить в семью'),
      content: SizedBox(
        width: 320,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            // QR код
            if (info != null)
              Container(
                padding: const EdgeInsets.all(16),
                decoration: BoxDecoration(
                  color: Colors.white,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: QrImageView(
                  data: _generateQrUrl(info, localIps),
                  version: QrVersions.auto,
                  size: 180,
                  errorCorrectionLevel: QrErrorCorrectLevel.M,
                ),
              ),
            const SizedBox(height: 16),

            // PIN
            if (info != null) ...[
              Text('или введите PIN:', style: theme.textTheme.bodySmall),
              const SizedBox(height: 8),
              InkWell(
                onTap: () {
                  Clipboard.setData(ClipboardData(text: info.pin));
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text('PIN скопирован')),
                  );
                },
                borderRadius: BorderRadius.circular(8),
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
                  decoration: BoxDecoration(
                    color: theme.colorScheme.surfaceContainerHighest,
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(
                        info.pin,
                        style: theme.textTheme.headlineMedium?.copyWith(
                          fontFamily: 'monospace',
                          letterSpacing: 4,
                        ),
                      ),
                      const SizedBox(width: 8),
                      const Icon(Icons.copy, size: 18),
                    ],
                  ),
                ),
              ),
            ],
            const SizedBox(height: 16),

            // IP для ручного подключения
            if (localIps.isNotEmpty)
              Text(
                'IP: ${localIps.first}:45680',
                style: theme.textTheme.bodySmall?.copyWith(
                  color: theme.colorScheme.outline,
                  fontFamily: 'monospace',
                ),
              ),

            // Таймер
            const SizedBox(height: 8),
            Text(
              _secondsRemaining > 0
                  ? 'Истекает через ${_secondsRemaining ~/ 60}:${(_secondsRemaining % 60).toString().padLeft(2, '0')}'
                  : 'Код истёк',
              style: theme.textTheme.bodySmall?.copyWith(
                color: _secondsRemaining > 60 
                    ? theme.colorScheme.outline 
                    : theme.colorScheme.error,
              ),
            ),
          ],
        ),
      ),
      actions: [
        if (_secondsRemaining <= 0)
          TextButton(
            onPressed: () {
              notifier.regeneratePin();
              setState(() {
                _secondsRemaining = 300;
                _startTimer();
              });
            },
            child: const Text('Обновить код'),
          ),
        TextButton(
          onPressed: () {
            notifier.cancel();
            Navigator.pop(context);
          },
          child: const Text('Закрыть'),
        ),
      ],
    );
  }
}

/// Диалог присоединения (PIN или QR)
class _JoinDialog extends ConsumerStatefulWidget {
  const _JoinDialog();

  @override
  ConsumerState<_JoinDialog> createState() => _JoinDialogState();
}

class _JoinDialogState extends ConsumerState<_JoinDialog> with SingleTickerProviderStateMixin {
  final _pinController = TextEditingController();
  final _ipController = TextEditingController();
  bool _isJoining = false;
  String? _error;
  late TabController _tabController;
  
  // Поддерживает ли устройство камеру
  bool get _hasCamera => Platform.isAndroid || Platform.isIOS;

  @override
  void initState() {
    super.initState();
    _tabController = TabController(length: _hasCamera ? 2 : 1, vsync: this);
  }

  @override
  void dispose() {
    _pinController.dispose();
    _ipController.dispose();
    _tabController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('Присоединиться к семье'),
      contentPadding: const EdgeInsets.fromLTRB(0, 20, 0, 0),
      content: SizedBox(
        width: 340,
        height: _hasCamera ? 420 : 280,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            // Tabs (только если есть камера)
            if (_hasCamera)
              TabBar(
                controller: _tabController,
                tabs: const [
                  Tab(icon: Icon(Icons.qr_code_scanner), text: 'QR-код'),
                  Tab(icon: Icon(Icons.dialpad), text: 'PIN'),
                ],
              ),
            
            // Контент
            Expanded(
              child: _hasCamera
                  ? TabBarView(
                      controller: _tabController,
                      children: [
                        _QRScannerTab(
                          onScanned: _onQrScanned,
                          isJoining: _isJoining,
                          error: _error,
                        ),
                        _PinEntryTab(
                          pinController: _pinController,
                          ipController: _ipController,
                          error: _error,
                          isJoining: _isJoining,
                          onJoin: _joinByPin,
                        ),
                      ],
                    )
                  : _PinEntryTab(
                      pinController: _pinController,
                      ipController: _ipController,
                      error: _error,
                      isJoining: _isJoining,
                      onJoin: _joinByPin,
                    ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: _isJoining ? null : () => Navigator.pop(context),
          child: const Text('Отмена'),
        ),
        // Кнопка присоединения только для PIN таба
        if (!_hasCamera || _tabController.index == 1)
          FilledButton(
            onPressed: _isJoining ? null : _joinByPin,
            child: _isJoining
                ? const SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Text('Присоединиться'),
          ),
      ],
    );
  }

  void _onQrScanned(String qrData) {
    if (_isJoining) return;
    
    // Парсим QR данные: "fv://join?pin=123456&host=192.168.1.100&port=45678"
    final uri = Uri.tryParse(qrData);
    if (uri == null || uri.scheme != 'fv' || uri.host != 'join') {
      setState(() => _error = 'Неверный QR-код');
      return;
    }
    
    final pin = uri.queryParameters['pin'];
    final host = uri.queryParameters['host'];
    final port = int.tryParse(uri.queryParameters['port'] ?? '45678') ?? 45678;
    
    if (pin == null || pin.length != 6) {
      setState(() => _error = 'Неверный PIN в QR-коде');
      return;
    }
    
    _joinWithData(pin, host, port);
  }

  void _joinByPin() {
    final pin = _pinController.text.trim();
    if (pin.length != 6) {
      setState(() => _error = 'Введите 6-значный PIN');
      return;
    }
    
    final ip = _ipController.text.trim();
    _joinWithData(pin, ip.isNotEmpty ? ip : null, 45678);
  }
  
  void _joinWithData(String pin, String? host, int port) {
    setState(() {
      _isJoining = true;
      _error = null;
    });

    final service = ref.read(networkServiceProvider);

    try {
      final result = service.joinFamilyByPin(
        pin,
        host: host,
        port: port,
      );

      if (result.isSuccess) {
        ref.invalidate(isFamilyConfiguredProvider);
        Navigator.pop(context);
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Успешно присоединились к семье!')),
        );
      } else {
        setState(() {
          _isJoining = false;
          _error = result.message;
        });
      }
    } catch (e) {
      setState(() {
        _isJoining = false;
        _error = e.toString();
      });
    }
  }
}

/// Таб со сканером QR
class _QRScannerTab extends StatefulWidget {
  final ValueChanged<String> onScanned;
  final bool isJoining;
  final String? error;

  const _QRScannerTab({
    required this.onScanned,
    required this.isJoining,
    this.error,
  });

  @override
  State<_QRScannerTab> createState() => _QRScannerTabState();
}

class _QRScannerTabState extends State<_QRScannerTab> {
  MobileScannerController? _controller;
  bool _hasScanned = false;

  @override
  void initState() {
    super.initState();
    _controller = MobileScannerController(
      detectionSpeed: DetectionSpeed.normal,
      facing: CameraFacing.back,
    );
  }

  @override
  void dispose() {
    _controller?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    if (widget.isJoining) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const CircularProgressIndicator(),
            const SizedBox(height: 16),
            Text('Подключение...', style: theme.textTheme.bodyMedium),
          ],
        ),
      );
    }

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        children: [
          // Сканер
          Expanded(
            child: ClipRRect(
              borderRadius: BorderRadius.circular(12),
              child: Stack(
                alignment: Alignment.center,
                children: [
                  MobileScanner(
                    controller: _controller,
                    onDetect: (capture) {
                      if (_hasScanned) return;
                      
                      final barcodes = capture.barcodes;
                      for (final barcode in barcodes) {
                        final value = barcode.rawValue;
                        if (value != null && value.startsWith('fv://')) {
                          setState(() => _hasScanned = true);
                          widget.onScanned(value);
                          break;
                        }
                      }
                    },
                  ),
                  // Рамка
                  Container(
                    width: 200,
                    height: 200,
                    decoration: BoxDecoration(
                      border: Border.all(
                        color: Colors.white.withOpacity(0.5),
                        width: 2,
                      ),
                      borderRadius: BorderRadius.circular(12),
                    ),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 12),
          
          // Подсказка
          Text(
            'Наведите камеру на QR-код',
            style: theme.textTheme.bodySmall?.copyWith(
              color: theme.colorScheme.outline,
            ),
          ),
          
          // Ошибка
          if (widget.error != null) ...[
            const SizedBox(height: 8),
            Container(
              padding: const EdgeInsets.all(8),
              decoration: BoxDecoration(
                color: theme.colorScheme.errorContainer,
                borderRadius: BorderRadius.circular(8),
              ),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.error, color: theme.colorScheme.error, size: 16),
                  const SizedBox(width: 8),
                  Flexible(
                    child: Text(
                      widget.error!,
                      style: TextStyle(color: theme.colorScheme.error, fontSize: 12),
                    ),
                  ),
                ],
              ),
            ),
          ],
        ],
      ),
    );
  }
}

/// Таб с вводом PIN
class _PinEntryTab extends StatelessWidget {
  final TextEditingController pinController;
  final TextEditingController ipController;
  final String? error;
  final bool isJoining;
  final VoidCallback onJoin;

  const _PinEntryTab({
    required this.pinController,
    required this.ipController,
    required this.error,
    required this.isJoining,
    required this.onJoin,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // PIN ввод
          Text('PIN-код:', style: theme.textTheme.labelLarge),
          const SizedBox(height: 8),
          TextField(
            controller: pinController,
            autofocus: true,
            keyboardType: TextInputType.number,
            maxLength: 6,
            textAlign: TextAlign.center,
            style: theme.textTheme.headlineSmall?.copyWith(
              fontFamily: 'monospace',
              letterSpacing: 8,
            ),
            decoration: const InputDecoration(
              counterText: '',
              hintText: '000000',
            ),
            inputFormatters: [FilteringTextInputFormatter.digitsOnly],
            onSubmitted: (_) => onJoin(),
          ),
          const SizedBox(height: 16),

          // IP (опционально)
          Text('IP адрес (опционально):', style: theme.textTheme.labelLarge),
          const SizedBox(height: 8),
          TextField(
            controller: ipController,
            keyboardType: TextInputType.text,
            decoration: const InputDecoration(
              hintText: '192.168.1.100',
            ),
            onSubmitted: (_) => onJoin(),
          ),

          // Ошибка
          if (error != null) ...[
            const SizedBox(height: 16),
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: theme.colorScheme.errorContainer,
                borderRadius: BorderRadius.circular(8),
              ),
              child: Row(
                children: [
                  Icon(Icons.error, color: theme.colorScheme.error, size: 18),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      error!,
                      style: TextStyle(color: theme.colorScheme.error),
                    ),
                  ),
                ],
              ),
            ),
          ],
        ],
      ),
    );
  }
}

