// Модель для записи о файле с удалённого устройства

import 'types.dart';

/// Запись о файле с удалённого устройства (из таблицы remote_files)
class RemoteFileRecord {
  final int localId;           // ID в локальной БД
  final int remoteId;          // ID на исходном устройстве
  final String sourceDeviceId; // UUID устройства-источника
  final String path;           // Путь на исходном устройстве
  final String name;           // Имя файла
  final String mimeType;       // MIME тип
  final int size;              // Размер в байтах
  final int modifiedAt;        // Unix timestamp модификации
  final String checksum;       // SHA-256 контрольная сумма
  final int syncedAt;          // Когда получена запись
  final bool isDeleted;        // Помечен как удалённый

  const RemoteFileRecord({
    required this.localId,
    required this.remoteId,
    required this.sourceDeviceId,
    required this.path,
    required this.name,
    required this.mimeType,
    required this.size,
    required this.modifiedAt,
    required this.checksum,
    required this.syncedAt,
    this.isDeleted = false,
  });

  /// Расширение файла
  String get extension {
    final dotIndex = name.lastIndexOf('.');
    return dotIndex != -1 ? name.substring(dotIndex + 1).toLowerCase() : '';
  }

  /// Тип контента по MIME типу
  ContentType get contentType {
    if (mimeType.startsWith('image/')) return ContentType.image;
    if (mimeType.startsWith('video/')) return ContentType.video;
    if (mimeType.startsWith('audio/')) return ContentType.audio;
    if (mimeType.startsWith('text/') || 
        mimeType == 'application/pdf' ||
        mimeType.contains('document') ||
        mimeType.contains('word') ||
        mimeType.contains('excel') ||
        mimeType.contains('spreadsheet')) {
      return ContentType.document;
    }
    return ContentType.other;
  }

  /// Человекочитаемый размер
  String get formattedSize {
    if (size < 1024) return '$size B';
    if (size < 1024 * 1024) return '${(size / 1024).toStringAsFixed(1)} KB';
    if (size < 1024 * 1024 * 1024) {
      return '${(size / (1024 * 1024)).toStringAsFixed(1)} MB';
    }
    return '${(size / (1024 * 1024 * 1024)).toStringAsFixed(2)} GB';
  }

  /// Дата модификации
  DateTime get modifiedDate => DateTime.fromMillisecondsSinceEpoch(modifiedAt * 1000);

  /// Дата синхронизации
  DateTime get syncedDate => DateTime.fromMillisecondsSinceEpoch(syncedAt * 1000);

  factory RemoteFileRecord.fromJson(Map<String, dynamic> json) {
    return RemoteFileRecord(
      localId: json['localId'] as int? ?? 0,
      remoteId: json['remoteId'] as int? ?? 0,
      sourceDeviceId: json['sourceDeviceId'] as String? ?? '',
      path: json['path'] as String? ?? '',
      name: json['name'] as String? ?? '',
      mimeType: json['mimeType'] as String? ?? '',
      size: json['size'] as int? ?? 0,
      modifiedAt: json['modifiedAt'] as int? ?? 0,
      checksum: json['checksum'] as String? ?? '',
      syncedAt: json['syncedAt'] as int? ?? 0,
      isDeleted: json['isDeleted'] as bool? ?? false,
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'localId': localId,
      'remoteId': remoteId,
      'sourceDeviceId': sourceDeviceId,
      'path': path,
      'name': name,
      'mimeType': mimeType,
      'size': size,
      'modifiedAt': modifiedAt,
      'checksum': checksum,
      'syncedAt': syncedAt,
      'isDeleted': isDeleted,
    };
  }

  @override
  String toString() {
    return 'RemoteFileRecord(name: $name, source: $sourceDeviceId, size: $formattedSize)';
  }

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is RemoteFileRecord &&
          runtimeType == other.runtimeType &&
          localId == other.localId &&
          sourceDeviceId == other.sourceDeviceId;

  @override
  int get hashCode => localId.hashCode ^ sourceDeviceId.hashCode;
}

