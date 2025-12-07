import 'file_record.dart';

/// Результат поиска
class SearchResult {
  final FileRecord file;
  final double score;
  final String? snippet;

  const SearchResult({
    required this.file,
    this.score = 0.0,
    this.snippet,
  });

  factory SearchResult.fromJson(Map<String, dynamic> json) {
    // C++ returns file fields directly with score/snippet mixed in
    final score = (json['score'] as num?)?.toDouble() ?? 0.0;
    final snippet = json['snippet'] as String?;
    return SearchResult(
      file: FileRecord.fromJson(json),
      score: score,
      snippet: snippet,
    );
  }

  Map<String, dynamic> toJson() => {
        ...file.toJson(),
        'score': score,
        if (snippet != null) 'snippet': snippet,
      };
}

/// Компактный результат поиска (для списков)
class SearchResultCompact {
  final FileRecordCompact file;
  final double score;
  final String? snippet;

  const SearchResultCompact({
    required this.file,
    this.score = 0.0,
    this.snippet,
  });

  factory SearchResultCompact.fromJson(Map<String, dynamic> json) {
    // C++ returns file fields directly with score/snippet mixed in
    final score = (json['score'] as num?)?.toDouble() ?? 0.0;
    final snippet = json['snippet'] as String?;
    return SearchResultCompact(
      file: FileRecordCompact.fromJson(json),
      score: score,
      snippet: snippet,
    );
  }

  Map<String, dynamic> toJson() => {
        ...file.toJson(),
        'score': score,
        if (snippet != null) 'snippet': snippet,
      };
}

