import 'file_record.dart';

/// Группа дубликатов
class DuplicateGroup {
  final String checksum;
  final int fileSize;
  final List<FileRecord> localCopies;
  final List<FileRecord> remoteCopies;

  const DuplicateGroup({
    required this.checksum,
    required this.fileSize,
    required this.localCopies,
    required this.remoteCopies,
  });

  factory DuplicateGroup.fromJson(Map<String, dynamic> json) {
    return DuplicateGroup(
      checksum: json['checksum'] as String,
      fileSize: json['fileSize'] as int,
      localCopies: (json['localCopies'] as List<dynamic>?)
              ?.map((e) => FileRecord.fromJson(e as Map<String, dynamic>))
              .toList() ??
          [],
      remoteCopies: (json['remoteCopies'] as List<dynamic>?)
              ?.map((e) => FileRecord.fromJson(e as Map<String, dynamic>))
              .toList() ??
          [],
    );
  }

  Map<String, dynamic> toJson() => {
        'checksum': checksum,
        'fileSize': fileSize,
        'localCopies': localCopies.map((e) => e.toJson()).toList(),
        'remoteCopies': remoteCopies.map((e) => e.toJson()).toList(),
      };

  /// Количество локальных копий
  int get localCount => localCopies.length;

  /// Количество удалённых копий (бэкапов)
  int get remoteCount => remoteCopies.length;

  /// Общее количество копий
  int get totalCount => localCount + remoteCount;

  /// Есть ли бэкап на другом устройстве
  bool get hasRemoteBackup => remoteCopies.isNotEmpty;

  /// Потенциальная экономия места (если оставить одну копию)
  int get potentialSavings {
    if (localCopies.length <= 1) return 0;
    return fileSize * (localCopies.length - 1);
  }

  /// Первый файл группы (обычно оригинал)
  FileRecord? get firstFile => localCopies.isNotEmpty ? localCopies.first : null;

  /// Все файлы группы
  List<FileRecord> get allFiles => [...localCopies, ...remoteCopies];
}

/// Статистика дубликатов
class DuplicateStats {
  final int totalGroups;
  final int totalDuplicates;
  final int totalSize;
  final int potentialSavings;
  final int filesWithoutBackup;

  const DuplicateStats({
    required this.totalGroups,
    required this.totalDuplicates,
    required this.totalSize,
    required this.potentialSavings,
    required this.filesWithoutBackup,
  });

  factory DuplicateStats.fromJson(Map<String, dynamic> json) {
    return DuplicateStats(
      totalGroups: json['totalGroups'] as int? ?? 0,
      totalDuplicates: json['totalDuplicates'] as int? ?? 0,
      totalSize: json['totalSize'] as int? ?? 0,
      potentialSavings: json['potentialSavings'] as int? ?? 0,
      filesWithoutBackup: json['filesWithoutBackup'] as int? ?? 0,
    );
  }

  factory DuplicateStats.empty() => const DuplicateStats(
        totalGroups: 0,
        totalDuplicates: 0,
        totalSize: 0,
        potentialSavings: 0,
        filesWithoutBackup: 0,
      );

  Map<String, dynamic> toJson() => {
        'totalGroups': totalGroups,
        'totalDuplicates': totalDuplicates,
        'totalSize': totalSize,
        'potentialSavings': potentialSavings,
        'filesWithoutBackup': filesWithoutBackup,
      };

  bool get hasDuplicates => totalGroups > 0;
  bool get hasFilesWithoutBackup => filesWithoutBackup > 0;
}

