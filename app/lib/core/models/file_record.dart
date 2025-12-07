import 'types.dart';

/// Метаданные изображений (EXIF)
class ImageMetadata {
  final int width;
  final int height;
  final DateTime? takenAt;
  final String? cameraMake;
  final String? cameraModel;
  final double? latitude;
  final double? longitude;
  final int orientation;

  const ImageMetadata({
    required this.width,
    required this.height,
    this.takenAt,
    this.cameraMake,
    this.cameraModel,
    this.latitude,
    this.longitude,
    this.orientation = 1,
  });

  factory ImageMetadata.fromJson(Map<String, dynamic> json) {
    return ImageMetadata(
      width: json['width'] as int? ?? 0,
      height: json['height'] as int? ?? 0,
      takenAt: json['takenAt'] != null
          ? DateTime.fromMillisecondsSinceEpoch((json['takenAt'] as int) * 1000)
          : null,
      cameraMake: json['cameraMake'] as String?,
      cameraModel: json['cameraModel'] as String?,
      latitude: (json['latitude'] as num?)?.toDouble(),
      longitude: (json['longitude'] as num?)?.toDouble(),
      orientation: json['orientation'] as int? ?? 1,
    );
  }

  Map<String, dynamic> toJson() => {
        'width': width,
        'height': height,
        if (takenAt != null) 'takenAt': takenAt!.millisecondsSinceEpoch ~/ 1000,
        if (cameraMake != null) 'cameraMake': cameraMake,
        if (cameraModel != null) 'cameraModel': cameraModel,
        if (latitude != null) 'latitude': latitude,
        if (longitude != null) 'longitude': longitude,
        'orientation': orientation,
      };

  bool get hasLocation => latitude != null && longitude != null;
  String get resolution => '${width}x$height';
  String? get camera =>
      cameraMake != null || cameraModel != null ? '$cameraMake $cameraModel'.trim() : null;
}

/// Компактная версия записи о файле (для списков)
class FileRecordCompact {
  final int id;
  final String name;
  final String extension;
  final int size;
  final ContentType contentType;
  final DateTime modifiedAt;
  final bool isRemote;
  final bool hasThumbnail;

  const FileRecordCompact({
    required this.id,
    required this.name,
    required this.extension,
    required this.size,
    required this.contentType,
    required this.modifiedAt,
    required this.isRemote,
    required this.hasThumbnail,
  });

  factory FileRecordCompact.fromJson(Map<String, dynamic> json) {
    return FileRecordCompact(
      id: json['id'] as int,
      name: json['name'] as String,
      extension: json['extension'] as String? ?? '',
      size: json['size'] as int,
      contentType: ContentType.fromValue(json['contentType'] as int? ?? 0),
      modifiedAt:
          DateTime.fromMillisecondsSinceEpoch((json['modifiedAt'] as int) * 1000),
      isRemote: json['isRemote'] as bool? ?? false,
      hasThumbnail: json['hasThumbnail'] as bool? ?? false,
    );
  }

  Map<String, dynamic> toJson() => {
        'id': id,
        'name': name,
        'extension': extension,
        'size': size,
        'contentType': contentType.value,
        'modifiedAt': modifiedAt.millisecondsSinceEpoch ~/ 1000,
        'isRemote': isRemote,
        'hasThumbnail': hasThumbnail,
      };

  bool get isLocal => !isRemote;
  bool get isImage => contentType == ContentType.image;
  bool get isVideo => contentType == ContentType.video;
  bool get isAudio => contentType == ContentType.audio;
  bool get isDocument => contentType == ContentType.document;
}

/// Полная версия записи о файле (для детального view)
class FileRecord {
  final int id;
  final int folderId;
  final String relativePath;
  final String name;
  final String extension;
  final int size;
  final String mimeType;
  final ContentType contentType;
  final String? checksum;
  final DateTime createdAt;
  final DateTime modifiedAt;
  final DateTime indexedAt;
  final Visibility visibility;
  final String? sourceDeviceId;
  final bool isRemote;
  final int syncVersion;
  final String? lastModifiedBy;
  final List<String> tags;
  final ImageMetadata? imageMeta;

  const FileRecord({
    required this.id,
    required this.folderId,
    required this.relativePath,
    required this.name,
    required this.extension,
    required this.size,
    required this.mimeType,
    required this.contentType,
    this.checksum,
    required this.createdAt,
    required this.modifiedAt,
    required this.indexedAt,
    required this.visibility,
    this.sourceDeviceId,
    required this.isRemote,
    this.syncVersion = 0,
    this.lastModifiedBy,
    this.tags = const [],
    this.imageMeta,
  });

  factory FileRecord.fromJson(Map<String, dynamic> json) {
    return FileRecord(
      id: json['id'] as int,
      folderId: json['folderId'] as int? ?? 0,
      relativePath: json['relativePath'] as String? ?? '',
      name: json['name'] as String,
      extension: json['extension'] as String? ?? '',
      size: json['size'] as int,
      mimeType: json['mimeType'] as String? ?? '',
      contentType: ContentType.fromValue(json['contentType'] as int? ?? 0),
      checksum: json['checksum'] as String?,
      createdAt:
          DateTime.fromMillisecondsSinceEpoch((json['createdAt'] as int? ?? 0) * 1000),
      modifiedAt:
          DateTime.fromMillisecondsSinceEpoch((json['modifiedAt'] as int? ?? 0) * 1000),
      indexedAt:
          DateTime.fromMillisecondsSinceEpoch((json['indexedAt'] as int? ?? 0) * 1000),
      visibility: Visibility.fromValue(json['visibility'] as int? ?? 1),
      sourceDeviceId: json['sourceDeviceId'] as String?,
      isRemote: json['isRemote'] as bool? ?? false,
      syncVersion: json['syncVersion'] as int? ?? 0,
      lastModifiedBy: json['lastModifiedBy'] as String?,
      tags: (json['tags'] as List<dynamic>?)?.cast<String>() ?? [],
      imageMeta: json['imageMeta'] != null
          ? ImageMetadata.fromJson(json['imageMeta'] as Map<String, dynamic>)
          : null,
    );
  }

  Map<String, dynamic> toJson() => {
        'id': id,
        'folderId': folderId,
        'relativePath': relativePath,
        'name': name,
        'extension': extension,
        'size': size,
        'mimeType': mimeType,
        'contentType': contentType.value,
        if (checksum != null) 'checksum': checksum,
        'createdAt': createdAt.millisecondsSinceEpoch ~/ 1000,
        'modifiedAt': modifiedAt.millisecondsSinceEpoch ~/ 1000,
        'indexedAt': indexedAt.millisecondsSinceEpoch ~/ 1000,
        'visibility': visibility.value,
        if (sourceDeviceId != null) 'sourceDeviceId': sourceDeviceId,
        'isRemote': isRemote,
        'syncVersion': syncVersion,
        if (lastModifiedBy != null) 'lastModifiedBy': lastModifiedBy,
        'tags': tags,
        if (imageMeta != null) 'imageMeta': imageMeta!.toJson(),
      };

  bool get isLocal => !isRemote;
  bool get isPrivate => visibility == Visibility.private;
  bool get isImage => contentType == ContentType.image;
  bool get isVideo => contentType == ContentType.video;
  bool get isAudio => contentType == ContentType.audio;
  bool get isDocument => contentType == ContentType.document;

  FileRecordCompact toCompact() => FileRecordCompact(
        id: id,
        name: name,
        extension: extension,
        size: size,
        contentType: contentType,
        modifiedAt: modifiedAt,
        isRemote: isRemote,
        hasThumbnail: imageMeta != null,
      );

  FileRecord copyWith({
    int? id,
    int? folderId,
    String? relativePath,
    String? name,
    String? extension,
    int? size,
    String? mimeType,
    ContentType? contentType,
    String? checksum,
    DateTime? createdAt,
    DateTime? modifiedAt,
    DateTime? indexedAt,
    Visibility? visibility,
    String? sourceDeviceId,
    bool? isRemote,
    int? syncVersion,
    String? lastModifiedBy,
    List<String>? tags,
    ImageMetadata? imageMeta,
  }) {
    return FileRecord(
      id: id ?? this.id,
      folderId: folderId ?? this.folderId,
      relativePath: relativePath ?? this.relativePath,
      name: name ?? this.name,
      extension: extension ?? this.extension,
      size: size ?? this.size,
      mimeType: mimeType ?? this.mimeType,
      contentType: contentType ?? this.contentType,
      checksum: checksum ?? this.checksum,
      createdAt: createdAt ?? this.createdAt,
      modifiedAt: modifiedAt ?? this.modifiedAt,
      indexedAt: indexedAt ?? this.indexedAt,
      visibility: visibility ?? this.visibility,
      sourceDeviceId: sourceDeviceId ?? this.sourceDeviceId,
      isRemote: isRemote ?? this.isRemote,
      syncVersion: syncVersion ?? this.syncVersion,
      lastModifiedBy: lastModifiedBy ?? this.lastModifiedBy,
      tags: tags ?? this.tags,
      imageMeta: imageMeta ?? this.imageMeta,
    );
  }
}

