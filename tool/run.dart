import 'dart:io';

/// Полный цикл: сборка ядра -> запуск Flutter приложения
void main(List<String> args) async {
  final platform = Platform.isWindows
      ? 'windows'
      : (Platform.isAndroid ? 'apk' : 'linux');

  print('🚀 FamilyVault Development Runner');
  print('   Platform: $platform');
  print('');

  // 1. Сборка Core
  print('═══════════════════════════════════════════════════════════');
  print(' Step 1: Building C++ Core');
  print('═══════════════════════════════════════════════════════════');

  final buildResult = await Process.run(
    'dart',
    ['run', 'tool/build_core.dart', ...args],
    runInShell: true,
  );

  stdout.write(buildResult.stdout);
  stderr.write(buildResult.stderr);

  if (buildResult.exitCode != 0) {
    print('');
    print('❌ Core build failed. Cannot continue.');
    exit(buildResult.exitCode);
  }

  // 2. Запуск Flutter
  print('');
  print('═══════════════════════════════════════════════════════════');
  print(' Step 2: Launching Flutter App');
  print('═══════════════════════════════════════════════════════════');
  print('');

  final flutterProcess = await Process.start(
    'flutter',
    ['run', '-d', platform],
    workingDirectory: 'app',
    runInShell: true,
    mode: ProcessStartMode.inheritStdio,
  );

  // Ожидаем завершения Flutter (или Ctrl+C)
  final exitCode = await flutterProcess.exitCode;
  exit(exitCode);
}

