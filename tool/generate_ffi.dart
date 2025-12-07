import 'dart:io';

/// Генерация FFI биндингов из C заголовков
void main() async {
  print('🔄 Generating FFI bindings...');

  // Проверка наличия заголовочного файла
  final headerFile = File('core/include/familyvault/familyvault_c.h');
  if (!await headerFile.exists()) {
    print('❌ Error: Header file not found at ${headerFile.path}');
    print('   Make sure you are running this from the project root.');
    exit(1);
  }

  // Запуск ffigen
  final result = await Process.run(
    'dart',
    ['run', 'ffigen'],
    workingDirectory: 'app',
    runInShell: true,
  );

  stdout.write(result.stdout);
  stderr.write(result.stderr);

  if (result.exitCode == 0) {
    print('✅ Bindings generated successfully!');
    print('   Output: app/lib/core/ffi/bindings.dart');
  } else {
    print('❌ Error generating bindings (exit code: ${result.exitCode})');
    exit(result.exitCode);
  }
}

