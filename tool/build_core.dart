import 'dart:io';

/// Сборка C++ библиотеки и копирование артефактов
void main(List<String> args) async {
  final config = args.contains('--debug') ? 'Debug' : 'Release';
  final isWindows = Platform.isWindows;
  final preset = isWindows ? 'windows-x64' : 'linux-x64';

  print('🔧 Building FamilyVault Core');
  print('   Platform: ${Platform.operatingSystem}');
  print('   Configuration: $config');
  print('');

  // 1. Конфигурация CMake
  print('📋 Configuring CMake ($preset)...');
  final configResult = await _run('cmake', ['--preset', preset]);
  if (configResult != 0) {
    print('❌ CMake configuration failed');
    exit(configResult);
  }

  // 2. Сборка
  print('');
  print('🔨 Building Core ($config)...');
  final buildResult = await _run('cmake', [
    '--build',
    '--preset',
    '$preset-${config.toLowerCase()}',
  ]);
  if (buildResult != 0) {
    print('❌ Build failed');
    exit(buildResult);
  }

  // 3. Копирование артефактов
  print('');
  print('📦 Copying artifacts...');

  final buildDir = 'build/$preset';
  final appDir = isWindows ? 'app/windows' : 'app/linux';
  final libName = isWindows ? 'familyvault.dll' : 'libfamilyvault.so';

  final srcPath = '$buildDir/$libName';
  final srcFile = File(srcPath);

  if (!await srcFile.exists()) {
    // Попробуем найти в подпапке Release/Debug
    final altPath = '$buildDir/$config/$libName';
    final altFile = File(altPath);
    if (await altFile.exists()) {
      await _copyLib(altFile, appDir, libName);
    } else {
      print('⚠️  Library not found at $srcPath or $altPath');
      print('   Post-build copy may have already handled this.');
    }
  } else {
    await _copyLib(srcFile, appDir, libName);
  }

  print('');
  print('✅ Core build completed successfully!');
}

Future<void> _copyLib(File srcFile, String appDir, String libName) async {
  final dstDir = Directory(appDir);
  if (!await dstDir.exists()) {
    await dstDir.create(recursive: true);
  }

  final dstPath = '$appDir/$libName';
  await srcFile.copy(dstPath);
  print('   Copied: ${srcFile.path} -> $dstPath');
}

Future<int> _run(String cmd, List<String> args) async {
  print('   > $cmd ${args.join(" ")}');

  final result = await Process.run(
    cmd,
    args,
    runInShell: true,
  );

  if (result.stdout.toString().isNotEmpty) {
    stdout.write(result.stdout);
  }
  if (result.stderr.toString().isNotEmpty) {
    stderr.write(result.stderr);
  }

  return result.exitCode;
}

