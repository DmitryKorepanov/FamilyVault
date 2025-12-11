// ═══════════════════════════════════════════════════════════
// Облачный аккаунт
// ═══════════════════════════════════════════════════════════

class CloudAccount {
  final int id;
  final String type;
  final String email;
  final String? displayName;
  final String? avatarUrl;
  final String? changeToken;
  final DateTime? lastSyncAt;
  final int fileCount;
  final bool enabled;
  final DateTime createdAt;

  CloudAccount({
    required this.id,
    required this.type,
    required this.email,
    this.displayName,
    this.avatarUrl,
    this.changeToken,
    this.lastSyncAt,
    required this.fileCount,
    required this.enabled,
    required this.createdAt,
  });

  factory CloudAccount.fromJson(Map<String, dynamic> json) {
    return CloudAccount(
      id: json['id'] as int,
      type: json['type'] as String,
      email: json['email'] as String,
      displayName: json['displayName'] as String?,
      avatarUrl: json['avatarUrl'] as String?,
      changeToken: json['changeToken'] as String?,
      lastSyncAt: json['lastSyncAt'] != null
          ? DateTime.fromMillisecondsSinceEpoch((json['lastSyncAt'] as int) * 1000)
          : null,
      fileCount: json['fileCount'] as int? ?? 0,
      enabled: json['enabled'] as bool? ?? true,
      createdAt: DateTime.fromMillisecondsSinceEpoch((json['createdAt'] as int) * 1000),
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'id': id,
      'type': type,
      'email': email,
      'displayName': displayName,
      'avatarUrl': avatarUrl,
      'changeToken': changeToken,
      'lastSyncAt': lastSyncAt != null ? (lastSyncAt!.millisecondsSinceEpoch ~/ 1000) : null,
      'fileCount': fileCount,
      'enabled': enabled,
      'createdAt': createdAt.millisecondsSinceEpoch ~/ 1000,
    };
  }
}

// ═══════════════════════════════════════════════════════════
// Облачная отслеживаемая папка
// ═══════════════════════════════════════════════════════════

class CloudWatchedFolder {
  final int id;
  final int accountId;
  final String cloudId;
  final String name;
  final String? path;
  final bool enabled;
  final DateTime? lastSyncAt;

  CloudWatchedFolder({
    required this.id,
    required this.accountId,
    required this.cloudId,
    required this.name,
    this.path,
    required this.enabled,
    this.lastSyncAt,
  });

  factory CloudWatchedFolder.fromJson(Map<String, dynamic> json) {
    return CloudWatchedFolder(
      id: json['id'] as int,
      accountId: json['accountId'] as int,
      cloudId: json['cloudId'] as String,
      name: json['name'] as String,
      path: json['path'] as String?,
      enabled: json['enabled'] as bool? ?? true,
      lastSyncAt: json['lastSyncAt'] != null
          ? DateTime.fromMillisecondsSinceEpoch((json['lastSyncAt'] as int) * 1000)
          : null,
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'id': id,
      'accountId': accountId,
      'cloudId': cloudId,
      'name': name,
      'path': path,
      'enabled': enabled,
      'lastSyncAt': lastSyncAt != null ? (lastSyncAt!.millisecondsSinceEpoch ~/ 1000) : null,
    };
  }
}
