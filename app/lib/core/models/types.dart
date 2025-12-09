/// Тип контента файла (контракт с C++ — см. SPECIFICATIONS.md)
enum ContentType {
  unknown(0),
  image(1),
  video(2),
  audio(3),
  document(4),
  archive(5),
  other(99);

  final int value;
  const ContentType(this.value);

  static ContentType fromValue(int v) =>
      ContentType.values.firstWhere((e) => e.value == v, orElse: () => unknown);

  String get displayName {
    switch (this) {
      case ContentType.image:
        return 'Images';
      case ContentType.video:
        return 'Videos';
      case ContentType.audio:
        return 'Audio';
      case ContentType.document:
        return 'Documents';
      case ContentType.archive:
        return 'Archives';
      case ContentType.other:
        return 'Other';
      case ContentType.unknown:
        return 'Unknown';
    }
  }

  String get singularName {
    switch (this) {
      case ContentType.image:
        return 'Image';
      case ContentType.video:
        return 'Video';
      case ContentType.audio:
        return 'Audio';
      case ContentType.document:
        return 'Document';
      case ContentType.archive:
        return 'Archive';
      case ContentType.other:
        return 'Other';
      case ContentType.unknown:
        return 'Unknown';
    }
  }
}

/// Видимость файла
enum FileVisibility {
  private(0), // Только на этом устройстве
  family(1); // Виден семье, синхронизируется

  final int value;
  const FileVisibility(this.value);

  static FileVisibility fromValue(int v) =>
      FileVisibility.values.firstWhere((e) => e.value == v, orElse: () => family);

  String get displayName {
    switch (this) {
      case FileVisibility.private:
        return 'Private';
      case FileVisibility.family:
        return 'Family';
    }
  }
}

/// Alias для обратной совместимости
typedef Visibility = FileVisibility;

/// Источник тега
enum TagSource {
  user(0),
  auto(1),
  ai(2);

  final int value;
  const TagSource(this.value);

  static TagSource fromValue(int v) =>
      TagSource.values.firstWhere((e) => e.value == v, orElse: () => user);

  String get displayName {
    switch (this) {
      case TagSource.user:
        return 'User';
      case TagSource.auto:
        return 'Auto';
      case TagSource.ai:
        return 'AI';
    }
  }
}

/// Тип устройства
enum DeviceType {
  desktop(0),
  mobile(1),
  tablet(2);

  final int value;
  const DeviceType(this.value);

  static DeviceType fromValue(int v) =>
      DeviceType.values.firstWhere((e) => e.value == v, orElse: () => desktop);

  String get displayName {
    switch (this) {
      case DeviceType.desktop:
        return 'Desktop';
      case DeviceType.mobile:
        return 'Mobile';
      case DeviceType.tablet:
        return 'Tablet';
    }
  }
}

/// Порядок сортировки
enum SortBy {
  relevance(0),
  name(1),
  date(2),
  size(3);

  final int value;
  const SortBy(this.value);

  static SortBy fromValue(int v) =>
      SortBy.values.firstWhere((e) => e.value == v, orElse: () => relevance);

  String get displayName {
    switch (this) {
      case SortBy.relevance:
        return 'Relevance';
      case SortBy.name:
        return 'Name';
      case SortBy.date:
        return 'Date';
      case SortBy.size:
        return 'Size';
    }
  }
}

/// Режим отображения
enum ViewMode {
  grid,
  list;

  String get displayName {
    switch (this) {
      case ViewMode.grid:
        return 'Grid';
      case ViewMode.list:
        return 'List';
    }
  }
}
