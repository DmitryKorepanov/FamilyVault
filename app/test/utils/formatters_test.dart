import 'package:flutter_test/flutter_test.dart';
import 'package:familyvault/shared/utils/formatters.dart';

void main() {
  group('formatFileSize', () {
    test('formats bytes', () {
      expect(formatFileSize(0), '0 B');
      expect(formatFileSize(500), '500 B');
      expect(formatFileSize(1023), '1023 B');
    });

    test('formats kilobytes', () {
      expect(formatFileSize(1024), '1.0 KB');
      expect(formatFileSize(1536), '1.5 KB');
      expect(formatFileSize(10240), '10.0 KB');
      expect(formatFileSize(1024 * 1024 - 1), '1024.0 KB');
    });

    test('formats megabytes', () {
      expect(formatFileSize(1024 * 1024), '1.0 MB');
      expect(formatFileSize(1024 * 1024 * 5), '5.0 MB');
      expect(formatFileSize(1024 * 1024 * 500), '500.0 MB');
    });

    test('formats gigabytes', () {
      expect(formatFileSize(1024 * 1024 * 1024), '1.00 GB');
      expect(formatFileSize(1024 * 1024 * 1024 * 2), '2.00 GB');
      expect(formatFileSize((1024 * 1024 * 1024 * 1.5).round()), '1.50 GB');
    });
  });

  group('formatFileCount', () {
    test('singular form', () {
      expect(formatFileCount(1), '1 file');
    });

    test('plural form', () {
      expect(formatFileCount(0), '0 files');
      expect(formatFileCount(2), '2 files');
      expect(formatFileCount(100), '100 files');
    });
  });

  group('formatFolderCount', () {
    test('singular form', () {
      expect(formatFolderCount(1), '1 folder');
    });

    test('plural form', () {
      expect(formatFolderCount(0), '0 folders');
      expect(formatFolderCount(5), '5 folders');
    });
  });

  group('formatItemCount', () {
    test('singular form', () {
      expect(formatItemCount(1), '1 item');
    });

    test('plural form', () {
      expect(formatItemCount(0), '0 items');
      expect(formatItemCount(10), '10 items');
    });
  });

  group('formatCompactNumber', () {
    test('formats small numbers as-is', () {
      expect(formatCompactNumber(0), '0');
      expect(formatCompactNumber(999), '999');
    });

    test('formats thousands', () {
      expect(formatCompactNumber(1000), '1.0K');
      expect(formatCompactNumber(1500), '1.5K');
      expect(formatCompactNumber(10000), '10K');
      expect(formatCompactNumber(999999), '1000K');
    });

    test('formats millions', () {
      expect(formatCompactNumber(1000000), '1.0M');
      expect(formatCompactNumber(1500000), '1.5M');
      expect(formatCompactNumber(10000000), '10M');
    });
  });

  group('formatPercent', () {
    test('formats percentage correctly', () {
      expect(formatPercent(0), '0.0%');
      expect(formatPercent(0.5), '50.0%');
      expect(formatPercent(1.0), '100.0%');
      expect(formatPercent(0.123), '12.3%');
    });
  });

  group('getFileExtension', () {
    test('returns extension for normal filename', () {
      expect(getFileExtension('photo.jpg'), 'jpg');
      expect(getFileExtension('document.PDF'), 'pdf');
      expect(getFileExtension('archive.tar.gz'), 'gz');
    });

    test('returns empty string for no extension', () {
      expect(getFileExtension('Makefile'), '');
      expect(getFileExtension('README'), '');
    });

    test('handles dot files', () {
      expect(getFileExtension('.gitignore'), 'gitignore');
      expect(getFileExtension('.env'), 'env');
    });
  });

  group('getFileNameWithoutExtension', () {
    test('removes extension', () {
      expect(getFileNameWithoutExtension('photo.jpg'), 'photo');
      expect(getFileNameWithoutExtension('document.pdf'), 'document');
    });

    test('handles multiple dots', () {
      expect(getFileNameWithoutExtension('archive.tar.gz'), 'archive.tar');
      expect(getFileNameWithoutExtension('my.file.name.txt'), 'my.file.name');
    });

    test('returns filename if no extension', () {
      expect(getFileNameWithoutExtension('Makefile'), 'Makefile');
      expect(getFileNameWithoutExtension('README'), 'README');
    });

    test('handles dot at start', () {
      expect(getFileNameWithoutExtension('.gitignore'), '.gitignore');
    });
  });

  group('formatDate', () {
    test('returns Just now for recent dates', () {
      final now = DateTime.now();
      expect(formatDate(now), 'Just now');
      expect(formatDate(now.subtract(const Duration(seconds: 30))), 'Just now');
    });

    test('returns minutes ago', () {
      final now = DateTime.now();
      final fiveMinAgo = now.subtract(const Duration(minutes: 5));
      expect(formatDate(fiveMinAgo), '5m ago');
    });

    test('returns hours ago', () {
      final now = DateTime.now();
      final threeHoursAgo = now.subtract(const Duration(hours: 3));
      expect(formatDate(threeHoursAgo), '3h ago');
    });

    test('returns Yesterday for yesterday', () {
      final now = DateTime.now();
      final yesterday = now.subtract(const Duration(days: 1));
      expect(formatDate(yesterday), 'Yesterday');
    });

    test('returns days ago for recent past', () {
      final now = DateTime.now();
      final threeDaysAgo = now.subtract(const Duration(days: 3));
      expect(formatDate(threeDaysAgo), '3d ago');
    });
  });

  group('formatDateTime', () {
    test('returns time for today', () {
      final today = DateTime.now().copyWith(hour: 14, minute: 30);
      final result = formatDateTime(today);
      expect(result, contains('14:30'));
    });

    test('returns Yesterday with time', () {
      final now = DateTime.now();
      final yesterday = now.subtract(const Duration(days: 1));
      final result = formatDateTime(yesterday);
      expect(result, startsWith('Yesterday,'));
    });
  });
}

