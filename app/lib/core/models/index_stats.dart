/// Статистика индекса
class IndexStats {
  final int totalFiles;
  final int totalFolders;
  final int totalSize;
  final int imageCount;
  final int videoCount;
  final int audioCount;
  final int documentCount;
  final int archiveCount;
  final int otherCount;
  final DateTime? lastScanAt;

  const IndexStats({
    required this.totalFiles,
    required this.totalFolders,
    required this.totalSize,
    required this.imageCount,
    required this.videoCount,
    required this.audioCount,
    required this.documentCount,
    required this.archiveCount,
    required this.otherCount,
    this.lastScanAt,
  });

  factory IndexStats.fromJson(Map<String, dynamic> json) {
    return IndexStats(
      totalFiles: json['totalFiles'] as int? ?? 0,
      totalFolders: json['totalFolders'] as int? ?? 0,
      totalSize: json['totalSize'] as int? ?? 0,
      imageCount: json['imageCount'] as int? ?? 0,
      videoCount: json['videoCount'] as int? ?? 0,
      audioCount: json['audioCount'] as int? ?? 0,
      documentCount: json['documentCount'] as int? ?? 0,
      archiveCount: json['archiveCount'] as int? ?? 0,
      otherCount: json['otherCount'] as int? ?? 0,
      lastScanAt: json['lastScanAt'] != null && json['lastScanAt'] != 0
          ? DateTime.fromMillisecondsSinceEpoch((json['lastScanAt'] as int) * 1000)
          : null,
    );
  }

  factory IndexStats.empty() => const IndexStats(
        totalFiles: 0,
        totalFolders: 0,
        totalSize: 0,
        imageCount: 0,
        videoCount: 0,
        audioCount: 0,
        documentCount: 0,
        archiveCount: 0,
        otherCount: 0,
      );

  Map<String, dynamic> toJson() => {
        'totalFiles': totalFiles,
        'totalFolders': totalFolders,
        'totalSize': totalSize,
        'imageCount': imageCount,
        'videoCount': videoCount,
        'audioCount': audioCount,
        'documentCount': documentCount,
        'archiveCount': archiveCount,
        'otherCount': otherCount,
        if (lastScanAt != null) 'lastScanAt': lastScanAt!.millisecondsSinceEpoch ~/ 1000,
      };

  bool get isEmpty => totalFiles == 0;
  bool get hasImages => imageCount > 0;
  bool get hasVideos => videoCount > 0;
  bool get hasAudio => audioCount > 0;
  bool get hasDocuments => documentCount > 0;

  /// Распределение по типам для UI (pie chart)
  Map<String, int> get typeDistribution => {
        if (imageCount > 0) 'Images': imageCount,
        if (videoCount > 0) 'Videos': videoCount,
        if (audioCount > 0) 'Audio': audioCount,
        if (documentCount > 0) 'Documents': documentCount,
        if (archiveCount > 0) 'Archives': archiveCount,
        if (otherCount > 0) 'Other': otherCount,
      };
}

