import 'types.dart';

/// Поисковый запрос
class SearchQuery {
  final String text;
  final ContentType? contentType;
  final String? extension;
  final int? folderId;
  final DateTime? dateFrom;
  final DateTime? dateTo;
  final int? minSize;
  final int? maxSize;
  final List<String> tags;
  final List<String> excludeTags;
  final Visibility? visibility;
  final bool includeRemote;
  final int limit;
  final int offset;
  final SortBy sortBy;
  final bool sortAsc;

  const SearchQuery({
    this.text = '',
    this.contentType,
    this.extension,
    this.folderId,
    this.dateFrom,
    this.dateTo,
    this.minSize,
    this.maxSize,
    this.tags = const [],
    this.excludeTags = const [],
    this.visibility,
    this.includeRemote = true,
    this.limit = 100,
    this.offset = 0,
    this.sortBy = SortBy.relevance,
    this.sortAsc = false,
  });

  factory SearchQuery.empty() => const SearchQuery();

  factory SearchQuery.fromJson(Map<String, dynamic> json) {
    return SearchQuery(
      text: json['text'] as String? ?? '',
      contentType: json['contentType'] != null
          ? ContentType.fromValue(json['contentType'] as int)
          : null,
      extension: json['extension'] as String?,
      folderId: json['folderId'] as int?,
      dateFrom: json['dateFrom'] != null
          ? DateTime.fromMillisecondsSinceEpoch((json['dateFrom'] as int) * 1000)
          : null,
      dateTo: json['dateTo'] != null
          ? DateTime.fromMillisecondsSinceEpoch((json['dateTo'] as int) * 1000)
          : null,
      minSize: json['minSize'] as int?,
      maxSize: json['maxSize'] as int?,
      tags: (json['tags'] as List<dynamic>?)?.cast<String>() ?? [],
      excludeTags: (json['excludeTags'] as List<dynamic>?)?.cast<String>() ?? [],
      visibility: json['visibility'] != null
          ? Visibility.fromValue(json['visibility'] as int)
          : null,
      includeRemote: json['includeRemote'] as bool? ?? true,
      limit: json['limit'] as int? ?? 100,
      offset: json['offset'] as int? ?? 0,
      sortBy: SortBy.fromValue(json['sortBy'] as int? ?? 0),
      sortAsc: json['sortAsc'] as bool? ?? false,
    );
  }

  Map<String, dynamic> toJson() => {
        'text': text,
        if (contentType != null) 'contentType': contentType!.value,
        if (extension != null) 'extension': extension,
        if (folderId != null) 'folderId': folderId,
        if (dateFrom != null) 'dateFrom': dateFrom!.millisecondsSinceEpoch ~/ 1000,
        if (dateTo != null) 'dateTo': dateTo!.millisecondsSinceEpoch ~/ 1000,
        if (minSize != null) 'minSize': minSize,
        if (maxSize != null) 'maxSize': maxSize,
        'tags': tags,
        'excludeTags': excludeTags,
        if (visibility != null) 'visibility': visibility!.value,
        'includeRemote': includeRemote,
        'limit': limit,
        'offset': offset,
        'sortBy': sortBy.value,
        'sortAsc': sortAsc,
      };

  bool get isEmpty =>
      text.isEmpty &&
      !hasFilters;

  bool get hasFilters =>
      contentType != null ||
      extension != null ||
      folderId != null ||
      dateFrom != null ||
      dateTo != null ||
      minSize != null ||
      maxSize != null ||
      tags.isNotEmpty ||
      excludeTags.isNotEmpty ||
      visibility != null;

  int get activeFilterCount {
    int count = 0;
    if (contentType != null) count++;
    if (extension != null) count++;
    if (folderId != null) count++;
    if (dateFrom != null || dateTo != null) count++;
    if (minSize != null || maxSize != null) count++;
    if (visibility != null) count++;
    count += tags.length;
    return count;
  }

  SearchQuery copyWith({
    String? text,
    ContentType? contentType,
    bool clearContentType = false,
    String? extension,
    bool clearExtension = false,
    int? folderId,
    bool clearFolderId = false,
    DateTime? dateFrom,
    bool clearDateFrom = false,
    DateTime? dateTo,
    bool clearDateTo = false,
    int? minSize,
    bool clearMinSize = false,
    int? maxSize,
    bool clearMaxSize = false,
    List<String>? tags,
    List<String>? excludeTags,
    Visibility? visibility,
    bool clearVisibility = false,
    bool? includeRemote,
    int? limit,
    int? offset,
    SortBy? sortBy,
    bool? sortAsc,
  }) {
    return SearchQuery(
      text: text ?? this.text,
      contentType: clearContentType ? null : (contentType ?? this.contentType),
      extension: clearExtension ? null : (extension ?? this.extension),
      folderId: clearFolderId ? null : (folderId ?? this.folderId),
      dateFrom: clearDateFrom ? null : (dateFrom ?? this.dateFrom),
      dateTo: clearDateTo ? null : (dateTo ?? this.dateTo),
      minSize: clearMinSize ? null : (minSize ?? this.minSize),
      maxSize: clearMaxSize ? null : (maxSize ?? this.maxSize),
      tags: tags ?? this.tags,
      excludeTags: excludeTags ?? this.excludeTags,
      visibility: clearVisibility ? null : (visibility ?? this.visibility),
      includeRemote: includeRemote ?? this.includeRemote,
      limit: limit ?? this.limit,
      offset: offset ?? this.offset,
      sortBy: sortBy ?? this.sortBy,
      sortAsc: sortAsc ?? this.sortAsc,
    );
  }

  SearchQuery addTag(String tag) {
    if (tags.contains(tag)) return this;
    return copyWith(tags: [...tags, tag]);
  }

  SearchQuery removeTag(String tag) {
    return copyWith(tags: tags.where((t) => t != tag).toList());
  }

  SearchQuery nextPage() {
    return copyWith(offset: offset + limit);
  }

  SearchQuery resetPagination() {
    return copyWith(offset: 0);
  }
}

