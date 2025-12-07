import 'types.dart';

/// Отслеживаемая папка
class WatchedFolder {
  final int id;
  final String path;
  final String name;
  final bool enabled;
  final DateTime? lastScanAt;
  final int fileCount;
  final int totalSize;
  final DateTime createdAt;
  final Visibility defaultVisibility;

  const WatchedFolder({
    required this.id,
    required this.path,
    required this.name,
    this.enabled = true,
    this.lastScanAt,
    this.fileCount = 0,
    this.totalSize = 0,
    required this.createdAt,
    this.defaultVisibility = Visibility.family,
  });

  factory WatchedFolder.fromJson(Map<String, dynamic> json) {
    return WatchedFolder(
      id: json['id'] as int,
      path: json['path'] as String,
      name: json['name'] as String,
      enabled: json['enabled'] as bool? ?? true,
      lastScanAt: json['lastScanAt'] != null && json['lastScanAt'] != 0
          ? DateTime.fromMillisecondsSinceEpoch((json['lastScanAt'] as int) * 1000)
          : null,
      fileCount: json['fileCount'] as int? ?? 0,
      totalSize: json['totalSize'] as int? ?? 0,
      createdAt:
          DateTime.fromMillisecondsSinceEpoch((json['createdAt'] as int? ?? 0) * 1000),
      defaultVisibility: Visibility.fromValue(json['defaultVisibility'] as int? ?? 1),
    );
  }

  Map<String, dynamic> toJson() => {
        'id': id,
        'path': path,
        'name': name,
        'enabled': enabled,
        if (lastScanAt != null) 'lastScanAt': lastScanAt!.millisecondsSinceEpoch ~/ 1000,
        'fileCount': fileCount,
        'totalSize': totalSize,
        'createdAt': createdAt.millisecondsSinceEpoch ~/ 1000,
        'defaultVisibility': defaultVisibility.value,
      };

  bool get hasScanned => lastScanAt != null;
  bool get isEmpty => fileCount == 0;
  bool get isPrivate => defaultVisibility == Visibility.private;

  WatchedFolder copyWith({
    int? id,
    String? path,
    String? name,
    bool? enabled,
    DateTime? lastScanAt,
    int? fileCount,
    int? totalSize,
    DateTime? createdAt,
    Visibility? defaultVisibility,
  }) {
    return WatchedFolder(
      id: id ?? this.id,
      path: path ?? this.path,
      name: name ?? this.name,
      enabled: enabled ?? this.enabled,
      lastScanAt: lastScanAt ?? this.lastScanAt,
      fileCount: fileCount ?? this.fileCount,
      totalSize: totalSize ?? this.totalSize,
      createdAt: createdAt ?? this.createdAt,
      defaultVisibility: defaultVisibility ?? this.defaultVisibility,
    );
  }
}

