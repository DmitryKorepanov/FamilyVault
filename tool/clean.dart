import 'dart:io';

/// Очистка всех артефактов сборки
void main() async {
  print('🧹 Cleaning FamilyVault build artifacts...');
  print('');

  final dirs = [
    'build',
    'app/build',
    'app/.dart_tool',
  ];

  int cleaned = 0;

  for (final dir in dirs) {
    final directory = Directory(dir);
    if (await directory.exists()) {
      print('   🗑️  Deleting $dir...');
      try {
        await directory.delete(recursive: true);
        cleaned++;
      } catch (e) {
        print('   ⚠️  Failed to delete $dir: $e');
      }
    } else {
      print('   ⏭️  Skipping $dir (not found)');
    }
  }

  // Также удаляем скопированные библиотеки
  final libs = [
    'app/windows/familyvault.dll',
    'app/linux/libfamilyvault.so',
  ];

  for (final lib in libs) {
    final file = File(lib);
    if (await file.exists()) {
      print('   🗑️  Deleting $lib...');
      try {
        await file.delete();
        cleaned++;
      } catch (e) {
        print('   ⚠️  Failed to delete $lib: $e');
      }
    }
  }

  print('');
  print('✨ Clean completed. Removed $cleaned items.');
}

