import 'types.dart';

/// Тег
class Tag {
  final int id;
  final String name;
  final TagSource source;
  final int fileCount;

  const Tag({
    required this.id,
    required this.name,
    this.source = TagSource.user,
    this.fileCount = 0,
  });

  factory Tag.fromJson(Map<String, dynamic> json) {
    return Tag(
      id: json['id'] as int? ?? 0,
      name: json['name'] as String,
      source: TagSource.fromValue(json['source'] as int? ?? 0),
      fileCount: json['fileCount'] as int? ?? 0,
    );
  }

  Map<String, dynamic> toJson() => {
        'id': id,
        'name': name,
        'source': source.value,
        'fileCount': fileCount,
      };

  bool get isUserTag => source == TagSource.user;
  bool get isAutoTag => source == TagSource.auto;
  bool get isAiTag => source == TagSource.ai;

  Tag copyWith({
    int? id,
    String? name,
    TagSource? source,
    int? fileCount,
  }) {
    return Tag(
      id: id ?? this.id,
      name: name ?? this.name,
      source: source ?? this.source,
      fileCount: fileCount ?? this.fileCount,
    );
  }

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is Tag && runtimeType == other.runtimeType && name == other.name;

  @override
  int get hashCode => name.hashCode;
}

