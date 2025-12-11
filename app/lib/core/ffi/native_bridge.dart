import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';

import 'package:ffi/ffi.dart';
import 'package:path_provider/path_provider.dart';

import '../models/cloud_account.dart';
import '../models/models.dart';

/// Коды ошибок из C API
enum FVError {
  ok(0),
  invalidArgument(1),
  database(2),
  io(3),
  notFound(4),
  alreadyExists(5),
  authFailed(6),
  network(7),
  busy(8),
  internal(99);

  final int value;
  const FVError(this.value);

  static FVError fromValue(int v) =>
      FVError.values.firstWhere((e) => e.value == v, orElse: () => internal);

  bool get isOk => this == FVError.ok;
}

/// Исключение при ошибке C API
class NativeException implements Exception {
  final FVError error;
  final String message;

  NativeException(this.error, this.message);

  @override
  String toString() => 'NativeException($error): $message';
}

/// Прогресс сканирования
class ScanProgress {
  final int processed;
  final int total;
  final String? currentFile;
  final bool isCountingPhase;

  ScanProgress({
    required this.processed,
    required this.total,
    this.currentFile,
    this.isCountingPhase = false,
  });

  double get progress => total > 0 ? processed / total : 0;
}

/// Прогресс вычисления checksums
class ChecksumProgress {
  final int processed;
  final int total;

  ChecksumProgress({required this.processed, required this.total});

  double get progress => total > 0 ? processed / total : 0;
}

/// Мост к нативной C++ библиотеке через FFI
class NativeBridge {
  static NativeBridge? _instance;
  static NativeBridge get instance => _instance ??= NativeBridge._();

  late final DynamicLibrary _lib;

  // Opaque handles
  Pointer<Void>? _database;
  Pointer<Void>? _indexManager;
  Pointer<Void>? _searchEngine;
  Pointer<Void>? _tagManager;
  Pointer<Void>? _duplicateFinder;
  Pointer<Void>? _contentIndexer;
  Pointer<Void>? _secureStorage;
  Pointer<Void>? _familyPairing;
  Pointer<Void>? _networkDiscovery;
  Pointer<Void>? _networkManager;
  Pointer<Void>? _indexSyncManager;

  // Network callback (kept alive while network is running)
  NativeCallable<Void Function(Int32, Pointer<Utf8>, Pointer<Void>)>? _networkCallback;

  // Database path (needed for background isolate operations)
  String? _databasePath;

  // Function typedefs
  late final _FvVersion _fvVersion;
  late final _FvErrorMessage _fvErrorMessage;
  late final _FvLastError _fvLastError;
  late final _FvLastErrorMessage _fvLastErrorMessage;
  late final _FvClearError _fvClearError;
  late final _FvFreeString _fvFreeString;

  // Database
  late final _FvDatabaseOpen _fvDatabaseOpen;
  late final _FvDatabaseClose _fvDatabaseClose;
  late final _FvDatabaseInitialize _fvDatabaseInitialize;
  late final _FvDatabaseIsInitialized _fvDatabaseIsInitialized;

  // Index Manager
  late final _FvIndexCreate _fvIndexCreate;
  late final _FvIndexDestroy _fvIndexDestroy;
  late final _FvIndexAddFolder _fvIndexAddFolder;
  late final _FvIndexRemoveFolder _fvIndexRemoveFolder;
  late final _FvIndexGetFolders _fvIndexGetFolders;
  late final _FvIndexScanFolder _fvIndexScanFolder;
  late final _FvIndexScanAll _fvIndexScanAll;
  late final _FvIndexStopScan _fvIndexStopScan;
  late final _FvIndexIsScanning _fvIndexIsScanning;
  late final _FvIndexGetFile _fvIndexGetFile;
  late final _FvIndexDeleteFile _fvIndexDeleteFile;
  late final _FvIndexGetRecent _fvIndexGetRecent;
  late final _FvIndexGetRecentCompact _fvIndexGetRecentCompact;
  late final _FvIndexGetByFolderCompact _fvIndexGetByFolderCompact;
  late final _FvIndexGetStats _fvIndexGetStats;
  late final _FvIndexSetFolderVisibility _fvIndexSetFolderVisibility;
  late final _FvIndexSetFolderEnabled _fvIndexSetFolderEnabled;
  late final _FvIndexSetFileVisibility _fvIndexSetFileVisibility;
  late final _FvIndexOptimizeDatabase _fvIndexOptimizeDatabase;
  late final _FvIndexGetMaxTextSizeKB _fvIndexGetMaxTextSizeKB;
  late final _FvIndexSetMaxTextSizeKB _fvIndexSetMaxTextSizeKB;

  // Search Engine
  late final _FvSearchCreate _fvSearchCreate;
  late final _FvSearchDestroy _fvSearchDestroy;
  late final _FvSearchQuery _fvSearchQuery;
  late final _FvSearchQueryCompact _fvSearchQueryCompact;
  late final _FvSearchCount _fvSearchCount;
  late final _FvSearchSuggest _fvSearchSuggest;

  // Tag Manager
  late final _FvTagsCreate _fvTagsCreate;
  late final _FvTagsDestroy _fvTagsDestroy;
  late final _FvTagsAdd _fvTagsAdd;
  late final _FvTagsRemove _fvTagsRemove;
  late final _FvTagsGetForFile _fvTagsGetForFile;
  late final _FvTagsGetAll _fvTagsGetAll;
  late final _FvTagsGetPopular _fvTagsGetPopular;

  // Duplicate Finder
  late final _FvDuplicatesCreate _fvDuplicatesCreate;
  late final _FvDuplicatesDestroy _fvDuplicatesDestroy;
  late final _FvDuplicatesFind _fvDuplicatesFind;
  late final _FvDuplicatesStats _fvDuplicatesStats;
  late final _FvDuplicatesWithoutBackup _fvDuplicatesWithoutBackup;
  late final _FvDuplicatesDeleteFile _fvDuplicatesDeleteFile;
  late final _FvDuplicatesComputeChecksums _fvDuplicatesComputeChecksums;

  // Content Indexer
  late final _FvContentIndexerCreate _fvContentIndexerCreate;
  late final _FvContentIndexerDestroy _fvContentIndexerDestroy;
  late final _FvContentIndexerStart _fvContentIndexerStart;
  late final _FvContentIndexerStop _fvContentIndexerStop;
  late final _FvContentIndexerIsRunning _fvContentIndexerIsRunning;
  late final _FvContentIndexerProcessFile _fvContentIndexerProcessFile;
  late final _FvContentIndexerEnqueueUnprocessed _fvContentIndexerEnqueueUnprocessed;
  late final _FvContentIndexerGetStatus _fvContentIndexerGetStatus;
  late final _FvContentIndexerGetPendingCount _fvContentIndexerGetPendingCount;
  late final _FvContentIndexerReindexAll _fvContentIndexerReindexAll;
  late final _FvContentIndexerCanExtract _fvContentIndexerCanExtract;
  late final _FvContentIndexerSetMaxTextSizeKB _fvContentIndexerSetMaxTextSizeKB;
  late final _FvContentIndexerGetMaxTextSizeKB _fvContentIndexerGetMaxTextSizeKB;

  // Secure Storage
  late final _FvSecureCreate _fvSecureCreate;
  late final _FvSecureDestroy _fvSecureDestroy;
  late final _FvSecureStoreString _fvSecureStoreString;
  late final _FvSecureRetrieveString _fvSecureRetrieveString;
  late final _FvSecureRemove _fvSecureRemove;
  late final _FvSecureExists _fvSecureExists;

  // Family Pairing
  late final _FvPairingCreate _fvPairingCreate;
  late final _FvPairingDestroy _fvPairingDestroy;
  late final _FvPairingIsConfigured _fvPairingIsConfigured;
  late final _FvPairingGetDeviceId _fvPairingGetDeviceId;
  late final _FvPairingGetDeviceName _fvPairingGetDeviceName;
  late final _FvPairingSetDeviceName _fvPairingSetDeviceName;
  late final _FvPairingGetDeviceType _fvPairingGetDeviceType;
  late final _FvPairingCreateFamily _fvPairingCreateFamily;
  late final _FvPairingRegeneratePin _fvPairingRegeneratePin;
  late final _FvPairingCancel _fvPairingCancel;
  late final _FvPairingHasPending _fvPairingHasPending;
  late final _FvPairingJoinPin _fvPairingJoinPin;
  late final _FvPairingJoinQr _fvPairingJoinQr;
  late final _FvPairingReset _fvPairingReset;
  late final _FvPairingStartServer _fvPairingStartServer;
  late final _FvPairingStopServer _fvPairingStopServer;
  late final _FvPairingIsServerRunning _fvPairingIsServerRunning;
  late final _FvPairingGetServerPort _fvPairingGetServerPort;

  // Network Discovery
  late final _FvDiscoveryCreate _fvDiscoveryCreate;
  late final _FvDiscoveryDestroy _fvDiscoveryDestroy;
  late final _FvDiscoveryStart _fvDiscoveryStart;
  late final _FvDiscoveryStop _fvDiscoveryStop;
  late final _FvDiscoveryIsRunning _fvDiscoveryIsRunning;
  late final _FvDiscoveryGetDevices _fvDiscoveryGetDevices;
  late final _FvDiscoveryGetDeviceCount _fvDiscoveryGetDeviceCount;
  late final _FvDiscoveryGetDevice _fvDiscoveryGetDevice;
  late final _FvDiscoveryGetLocalIps _fvDiscoveryGetLocalIps;

  // Network Manager
  late final _FvNetworkCreate _fvNetworkCreate;
  late final _FvNetworkDestroy _fvNetworkDestroy;
  late final _FvNetworkStart _fvNetworkStart;
  late final _FvNetworkStop _fvNetworkStop;
  late final _FvNetworkGetState _fvNetworkGetState;
  late final _FvNetworkIsRunning _fvNetworkIsRunning;
  late final _FvNetworkGetPort _fvNetworkGetPort;
  late final _FvNetworkGetDiscoveredDevices _fvNetworkGetDiscoveredDevices;
  late final _FvNetworkGetConnectedDevices _fvNetworkGetConnectedDevices;
  late final _FvNetworkConnectToDevice _fvNetworkConnectToDevice;
  late final _FvNetworkConnectToAddress _fvNetworkConnectToAddress;
  late final _FvNetworkDisconnectDevice _fvNetworkDisconnectDevice;
  late final _FvNetworkDisconnectAll _fvNetworkDisconnectAll;
  late final _FvNetworkIsConnectedTo _fvNetworkIsConnectedTo;
  late final _FvNetworkGetLastError _fvNetworkGetLastError;
  late final _FvNetworkSetDatabase _fvNetworkSetDatabase;
  late final _FvNetworkRequestSync _fvNetworkRequestSync;
  late final _FvNetworkGetRemoteFiles _fvNetworkGetRemoteFiles;
  late final _FvNetworkSearchRemoteFiles _fvNetworkSearchRemoteFiles;
  late final _FvNetworkGetRemoteFileCount _fvNetworkGetRemoteFileCount;
  
  // File Transfer
  late final _FvNetworkSetCacheDir _fvNetworkSetCacheDir;
  late final _FvNetworkRequestFile _fvNetworkRequestFile;
  late final _FvNetworkCancelFileRequest _fvNetworkCancelFileRequest;
  late final _FvNetworkCancelAllFileRequests _fvNetworkCancelAllFileRequests;
  late final _FvNetworkGetActiveTransfers _fvNetworkGetActiveTransfers;
  late final _FvNetworkGetTransferProgress _fvNetworkGetTransferProgress;
  late final _FvNetworkIsFileCached _fvNetworkIsFileCached;
  late final _FvNetworkGetCachedPath _fvNetworkGetCachedPath;
  late final _FvNetworkClearCache _fvNetworkClearCache;
  late final _FvNetworkGetCacheSize _fvNetworkGetCacheSize;
  late final _FvNetworkHasActiveTransfers _fvNetworkHasActiveTransfers;

  // Cloud Accounts
  late final _FvCloudAccountAdd _fvCloudAccountAdd;
  late final _FvCloudAccountList _fvCloudAccountList;
  late final _FvCloudAccountRemove _fvCloudAccountRemove;
  late final _FvCloudAccountSetEnabled _fvCloudAccountSetEnabled;
  late final _FvCloudAccountUpdateSync _fvCloudAccountUpdateSync;

  late final _FvCloudFolderAdd _fvCloudFolderAdd;
  late final _FvCloudFolderRemove _fvCloudFolderRemove;
  late final _FvCloudFolderList _fvCloudFolderList;
  late final _FvCloudFolderSetEnabled _fvCloudFolderSetEnabled;

  late final _FvCloudFileUpsert _fvCloudFileUpsert;
  late final _FvCloudFileRemove _fvCloudFileRemove;
  late final _FvCloudFileRemoveAll _fvCloudFileRemoveAll;

  // Index Sync Manager
  late final _FvSyncCreate _fvSyncCreate;
  late final _FvSyncDestroy _fvSyncDestroy;
  late final _FvSyncGetRemoteFiles _fvSyncGetRemoteFiles;
  late final _FvSyncGetRemoteFilesFrom _fvSyncGetRemoteFilesFrom;
  late final _FvSyncSearchRemote _fvSyncSearchRemote;
  late final _FvSyncGetRemoteCount _fvSyncGetRemoteCount;
  late final _FvSyncIsSyncing _fvSyncIsSyncing;

  NativeBridge._() {
    _lib = _loadLibrary();
    _bindFunctions();
  }

  /// Private constructor for creating instances in background isolates
  NativeBridge._forIsolate() {
    _lib = _loadLibrary();
    _bindFunctions();
  }

  bool get isInitialized => _database != null;
  String? get databasePath => _databasePath;

  /// Загрузка библиотеки в зависимости от платформы
  DynamicLibrary _loadLibrary() {
    if (Platform.isWindows) {
      return DynamicLibrary.open('familyvault.dll');
    } else if (Platform.isAndroid) {
      return DynamicLibrary.open('libfamilyvault.so');
    } else if (Platform.isLinux) {
      return DynamicLibrary.open('libfamilyvault.so');
    } else if (Platform.isMacOS) {
      return DynamicLibrary.open('libfamilyvault.dylib');
    }
    throw UnsupportedError('Platform not supported');
  }

  /// Привязка всех функций из C API
  void _bindFunctions() {
    // General
    _fvVersion = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function()>>('fv_version')
        .asFunction();

    _fvErrorMessage = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Int32)>>(
            'fv_error_message')
        .asFunction();

    _fvLastError = _lib
        .lookup<NativeFunction<Int32 Function()>>('fv_last_error')
        .asFunction();

    _fvLastErrorMessage = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function()>>('fv_last_error_message')
        .asFunction();

    _fvClearError = _lib
        .lookup<NativeFunction<Void Function()>>('fv_clear_error')
        .asFunction();

    _fvFreeString = _lib
        .lookup<NativeFunction<Void Function(Pointer<Utf8>)>>('fv_free_string')
        .asFunction();

    // Database
    _fvDatabaseOpen = _lib
        .lookup<
            NativeFunction<
                Pointer<Void> Function(
                    Pointer<Utf8>, Pointer<Int32>)>>('fv_database_open')
        .asFunction();

    _fvDatabaseClose = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_database_close')
        .asFunction();

    _fvDatabaseInitialize = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_database_initialize')
        .asFunction();

    _fvDatabaseIsInitialized = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_database_is_initialized')
        .asFunction();

    // Index Manager
    _fvIndexCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>)>>(
            'fv_index_create')
        .asFunction();

    _fvIndexDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>(
            'fv_index_destroy')
        .asFunction();

    _fvIndexAddFolder = _lib
        .lookup<
            NativeFunction<
                Int64 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>,
                    Int32)>>('fv_index_add_folder')
        .asFunction();

    _fvIndexRemoveFolder = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64)>>(
            'fv_index_remove_folder')
        .asFunction();

    _fvIndexGetFolders = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_index_get_folders')
        .asFunction();

    _fvIndexScanFolder = _lib
        .lookup<
            NativeFunction<
                Int32 Function(Pointer<Void>, Int64,
                    Pointer<NativeFunction<ScanCallbackNative>>, Pointer<Void>)>>('fv_index_scan_folder')
        .asFunction();

    _fvIndexScanAll = _lib
        .lookup<
            NativeFunction<
                Int32 Function(Pointer<Void>,
                    Pointer<NativeFunction<ScanCallbackNative>>, Pointer<Void>)>>('fv_index_scan_all')
        .asFunction();

    _fvIndexStopScan = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>(
            'fv_index_stop_scan')
        .asFunction();

    _fvIndexIsScanning = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_index_is_scanning')
        .asFunction();

    _fvIndexGetFile = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int64)>>(
            'fv_index_get_file')
        .asFunction();

    _fvIndexDeleteFile = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64, Int32)>>(
            'fv_index_delete_file')
        .asFunction();

    _fvIndexGetRecent = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int32)>>(
            'fv_index_get_recent')
        .asFunction();

    _fvIndexGetRecentCompact = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int32)>>(
            'fv_index_get_recent_compact')
        .asFunction();

    _fvIndexGetByFolderCompact = _lib
        .lookup<
            NativeFunction<
                Pointer<Utf8> Function(
                    Pointer<Void>, Int64, Int32, Int32)>>('fv_index_get_by_folder_compact')
        .asFunction();

    _fvIndexGetStats = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_index_get_stats')
        .asFunction();

    _fvIndexSetFolderVisibility = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64, Int32)>>(
            'fv_index_set_folder_visibility')
        .asFunction();

    _fvIndexSetFolderEnabled = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64, Int32)>>(
            'fv_index_set_folder_enabled')
        .asFunction();

    _fvIndexSetFileVisibility = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64, Int32)>>(
            'fv_index_set_file_visibility')
        .asFunction();

    _fvIndexOptimizeDatabase = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_index_optimize_database')
        .asFunction();

    _fvIndexGetMaxTextSizeKB = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_index_get_max_text_size_kb')
        .asFunction();

    _fvIndexSetMaxTextSizeKB = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int32)>>(
            'fv_index_set_max_text_size_kb')
        .asFunction();

    // Search Engine
    _fvSearchCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>)>>(
            'fv_search_create')
        .asFunction();

    _fvSearchDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>(
            'fv_search_destroy')
        .asFunction();

    _fvSearchQuery = _lib
        .lookup<
            NativeFunction<
                Pointer<Utf8> Function(
                    Pointer<Void>, Pointer<Utf8>)>>('fv_search_query')
        .asFunction();

    _fvSearchQueryCompact = _lib
        .lookup<
            NativeFunction<
                Pointer<Utf8> Function(
                    Pointer<Void>, Pointer<Utf8>)>>('fv_search_query_compact')
        .asFunction();

    _fvSearchCount = _lib
        .lookup<NativeFunction<Int64 Function(Pointer<Void>, Pointer<Utf8>)>>(
            'fv_search_count')
        .asFunction();

    _fvSearchSuggest = _lib
        .lookup<
            NativeFunction<
                Pointer<Utf8> Function(
                    Pointer<Void>, Pointer<Utf8>, Int32)>>('fv_search_suggest')
        .asFunction();

    // Tag Manager
    _fvTagsCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>)>>(
            'fv_tags_create')
        .asFunction();

    _fvTagsDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>(
            'fv_tags_destroy')
        .asFunction();

    _fvTagsAdd = _lib
        .lookup<
            NativeFunction<
                Int32 Function(
                    Pointer<Void>, Int64, Pointer<Utf8>)>>('fv_tags_add')
        .asFunction();

    _fvTagsRemove = _lib
        .lookup<
            NativeFunction<
                Int32 Function(
                    Pointer<Void>, Int64, Pointer<Utf8>)>>('fv_tags_remove')
        .asFunction();

    _fvTagsGetForFile = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int64)>>(
            'fv_tags_get_for_file')
        .asFunction();

    _fvTagsGetAll = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_tags_get_all')
        .asFunction();

    _fvTagsGetPopular = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int32)>>(
            'fv_tags_get_popular')
        .asFunction();

    // Duplicate Finder
    _fvDuplicatesCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>, Pointer<Void>)>>(
            'fv_duplicates_create')
        .asFunction();

    _fvDuplicatesDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>(
            'fv_duplicates_destroy')
        .asFunction();

    _fvDuplicatesFind = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_duplicates_find')
        .asFunction();

    _fvDuplicatesStats = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_duplicates_stats')
        .asFunction();

    _fvDuplicatesWithoutBackup = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_duplicates_without_backup')
        .asFunction();

    _fvDuplicatesDeleteFile = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64)>>(
            'fv_duplicates_delete_file')
        .asFunction();

    _fvDuplicatesComputeChecksums = _lib
        .lookup<
            NativeFunction<
                Int32 Function(
                    Pointer<Void>,
                    Pointer<NativeFunction<ChecksumCallbackNative>>,
                    Pointer<Void>)>>('fv_duplicates_compute_checksums')
        .asFunction();

    // Content Indexer
    _fvContentIndexerCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>)>>(
            'fv_content_indexer_create')
        .asFunction();

    _fvContentIndexerDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>(
            'fv_content_indexer_destroy')
        .asFunction();

    _fvContentIndexerStart = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_content_indexer_start')
        .asFunction();

    _fvContentIndexerStop = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int32)>>(
            'fv_content_indexer_stop')
        .asFunction();

    _fvContentIndexerIsRunning = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_content_indexer_is_running')
        .asFunction();

    _fvContentIndexerProcessFile = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Int64)>>(
            'fv_content_indexer_process_file')
        .asFunction();

    _fvContentIndexerEnqueueUnprocessed = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_content_indexer_enqueue_unprocessed')
        .asFunction();

    _fvContentIndexerGetStatus = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>(
            'fv_content_indexer_get_status')
        .asFunction();

    _fvContentIndexerGetPendingCount = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_content_indexer_get_pending_count')
        .asFunction();

    _fvContentIndexerReindexAll = _lib
        .lookup<
            NativeFunction<
                Int32 Function(
                    Pointer<Void>,
                    Pointer<NativeFunction<ContentProgressCallbackNative>>,
                    Pointer<Void>)>>('fv_content_indexer_reindex_all')
        .asFunction();

    _fvContentIndexerCanExtract = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>)>>(
            'fv_content_indexer_can_extract')
        .asFunction();

    _fvContentIndexerSetMaxTextSizeKB = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>, Int32)>>(
            'fv_content_indexer_set_max_text_size_kb')
        .asFunction();

    _fvContentIndexerGetMaxTextSizeKB = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>(
            'fv_content_indexer_get_max_text_size_kb')
        .asFunction();

    // Secure Storage
    _fvSecureCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function()>>('fv_secure_create')
        .asFunction();

    _fvSecureDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_secure_destroy')
        .asFunction();

    _fvSecureStoreString = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>)>>(
            'fv_secure_store_string')
        .asFunction();

    _fvSecureRetrieveString = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>)>>(
            'fv_secure_retrieve_string')
        .asFunction();

    _fvSecureRemove = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>)>>(
            'fv_secure_remove')
        .asFunction();

    _fvSecureExists = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>)>>(
            'fv_secure_exists')
        .asFunction();

    // Family Pairing
    _fvPairingCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>)>>('fv_pairing_create')
        .asFunction();

    _fvPairingDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_pairing_destroy')
        .asFunction();

    _fvPairingIsConfigured = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('fv_pairing_is_configured')
        .asFunction();

    _fvPairingGetDeviceId = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_pairing_get_device_id')
        .asFunction();

    _fvPairingGetDeviceName = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_pairing_get_device_name')
        .asFunction();

    _fvPairingSetDeviceName = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>, Pointer<Utf8>)>>(
            'fv_pairing_set_device_name')
        .asFunction();

    _fvPairingGetDeviceType = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('fv_pairing_get_device_type')
        .asFunction();

    _fvPairingCreateFamily = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_pairing_create_family')
        .asFunction();

    _fvPairingRegeneratePin = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_pairing_regenerate_pin')
        .asFunction();

    _fvPairingCancel = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_pairing_cancel')
        .asFunction();

    _fvPairingHasPending = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('fv_pairing_has_pending')
        .asFunction();

    _fvPairingJoinPin = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, Uint16)>>(
            'fv_pairing_join_pin')
        .asFunction();

    _fvPairingJoinQr = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>)>>('fv_pairing_join_qr')
        .asFunction();

    _fvPairingReset = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_pairing_reset')
        .asFunction();
    _fvPairingStartServer = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Uint16)>>('fv_pairing_start_server')
        .asFunction();
    _fvPairingStopServer = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_pairing_stop_server')
        .asFunction();
    _fvPairingIsServerRunning = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('fv_pairing_is_server_running')
        .asFunction();
    _fvPairingGetServerPort = _lib
        .lookup<NativeFunction<Uint16 Function(Pointer<Void>)>>('fv_pairing_get_server_port')
        .asFunction();

    // Network Discovery
    _fvDiscoveryCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function()>>('fv_discovery_create')
        .asFunction();
    _fvDiscoveryDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_discovery_destroy')
        .asFunction();
    _fvDiscoveryStart = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<NativeFunction<Void Function(Int32, Pointer<Utf8>, Pointer<Void>)>>, Pointer<Void>)>>('fv_discovery_start')
        .asFunction();
    _fvDiscoveryStop = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_discovery_stop')
        .asFunction();
    _fvDiscoveryIsRunning = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('fv_discovery_is_running')
        .asFunction();
    _fvDiscoveryGetDevices = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_discovery_get_devices')
        .asFunction();
    _fvDiscoveryGetDeviceCount = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('fv_discovery_get_device_count')
        .asFunction();
    _fvDiscoveryGetDevice = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>)>>('fv_discovery_get_device')
        .asFunction();
    _fvDiscoveryGetLocalIps = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function()>>('fv_discovery_get_local_ips')
        .asFunction();

    // Network Manager
    _fvNetworkCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>)>>('fv_network_create')
        .asFunction();
    _fvNetworkDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_network_destroy')
        .asFunction();
    _fvNetworkStart = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Uint16, Pointer<NativeFunction<Void Function(Int32, Pointer<Utf8>, Pointer<Void>)>>, Pointer<Void>)>>('fv_network_start')
        .asFunction();
    _fvNetworkStop = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_network_stop')
        .asFunction();
    _fvNetworkGetState = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('fv_network_get_state')
        .asFunction();
    _fvNetworkIsRunning = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('fv_network_is_running')
        .asFunction();
    _fvNetworkGetPort = _lib
        .lookup<NativeFunction<Uint16 Function(Pointer<Void>)>>('fv_network_get_port')
        .asFunction();
    _fvNetworkGetDiscoveredDevices = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_network_get_discovered_devices')
        .asFunction();
    _fvNetworkGetConnectedDevices = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_network_get_connected_devices')
        .asFunction();
    _fvNetworkConnectToDevice = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>)>>('fv_network_connect_to_device')
        .asFunction();
    _fvNetworkConnectToAddress = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>, Uint16)>>('fv_network_connect_to_address')
        .asFunction();
    _fvNetworkDisconnectDevice = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>, Pointer<Utf8>)>>('fv_network_disconnect_device')
        .asFunction();
    _fvNetworkDisconnectAll = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_network_disconnect_all')
        .asFunction();
    _fvNetworkIsConnectedTo = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>)>>('fv_network_is_connected_to')
        .asFunction();
    _fvNetworkGetLastError = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_network_get_last_error')
        .asFunction();
    _fvNetworkSetDatabase = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<Utf8>)>>('fv_network_set_database')
        .asFunction();
    _fvNetworkRequestSync = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>, Int32)>>('fv_network_request_sync')
        .asFunction();
    _fvNetworkGetRemoteFiles = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_network_get_remote_files')
        .asFunction();
    _fvNetworkSearchRemoteFiles = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Int32)>>('fv_network_search_remote_files')
        .asFunction();
    _fvNetworkGetRemoteFileCount = _lib
        .lookup<NativeFunction<Int64 Function(Pointer<Void>)>>('fv_network_get_remote_file_count')
        .asFunction();
    
    // File Transfer
    _fvNetworkSetCacheDir = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>)>>('fv_network_set_cache_dir')
        .asFunction();
    _fvNetworkRequestFile = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Int64, Pointer<Utf8>, Int64, Pointer<Utf8>)>>('fv_network_request_file')
        .asFunction();
    _fvNetworkCancelFileRequest = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>, Pointer<Utf8>)>>('fv_network_cancel_file_request')
        .asFunction();
    _fvNetworkCancelAllFileRequests = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>, Pointer<Utf8>)>>('fv_network_cancel_all_file_requests')
        .asFunction();
    _fvNetworkGetActiveTransfers = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_network_get_active_transfers')
        .asFunction();
    _fvNetworkGetTransferProgress = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>)>>('fv_network_get_transfer_progress')
        .asFunction();
    _fvNetworkIsFileCached = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>, Int64, Pointer<Utf8>)>>('fv_network_is_file_cached')
        .asFunction();
    _fvNetworkGetCachedPath = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Int64)>>('fv_network_get_cached_path')
        .asFunction();
    _fvNetworkClearCache = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_network_clear_cache')
        .asFunction();
    _fvNetworkGetCacheSize = _lib
        .lookup<NativeFunction<Int64 Function(Pointer<Void>)>>('fv_network_get_cache_size')
        .asFunction();
    _fvNetworkHasActiveTransfers = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('fv_network_has_active_transfers')
        .asFunction();

    // Cloud Accounts
    _fvCloudAccountAdd = _lib
        .lookup<NativeFunction<Int64 Function(
            Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>)>>('fv_cloud_account_add')
        .asFunction();

    _fvCloudAccountList = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_cloud_account_list')
        .asFunction();

    _fvCloudAccountRemove = _lib
        .lookup<NativeFunction<Bool Function(Pointer<Void>, Int64)>>('fv_cloud_account_remove')
        .asFunction();

    _fvCloudAccountSetEnabled = _lib
        .lookup<NativeFunction<Bool Function(Pointer<Void>, Int64, Bool)>>('fv_cloud_account_set_enabled')
        .asFunction();

    _fvCloudAccountUpdateSync = _lib
        .lookup<NativeFunction<Bool Function(Pointer<Void>, Int64, Int64, Pointer<Utf8>)>>('fv_cloud_account_update_sync')
        .asFunction();

    _fvCloudFolderAdd = _lib
        .lookup<NativeFunction<Int64 Function(
            Pointer<Void>, Int64, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>)>>('fv_cloud_folder_add')
        .asFunction();

    _fvCloudFolderRemove = _lib
        .lookup<NativeFunction<Bool Function(Pointer<Void>, Int64)>>('fv_cloud_folder_remove')
        .asFunction();

    _fvCloudFolderList = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int64)>>('fv_cloud_folder_list')
        .asFunction();

    _fvCloudFolderSetEnabled = _lib
        .lookup<NativeFunction<Bool Function(Pointer<Void>, Int64, Bool)>>('fv_cloud_folder_set_enabled')
        .asFunction();

    _fvCloudFileUpsert = _lib
        .lookup<NativeFunction<Bool Function(Pointer<Void>, Int64, Pointer<Utf8>)>>('fv_cloud_file_upsert')
        .asFunction();

    _fvCloudFileRemove = _lib
        .lookup<NativeFunction<Bool Function(Pointer<Void>, Int64, Pointer<Utf8>)>>('fv_cloud_file_remove')
        .asFunction();

    _fvCloudFileRemoveAll = _lib
        .lookup<NativeFunction<Bool Function(Pointer<Void>, Int64)>>('fv_cloud_file_remove_all')
        .asFunction();

    // Index Sync Manager
    _fvSyncCreate = _lib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>, Pointer<Utf8>)>>('fv_sync_create')
        .asFunction();
    _fvSyncDestroy = _lib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('fv_sync_destroy')
        .asFunction();
    _fvSyncGetRemoteFiles = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('fv_sync_get_remote_files')
        .asFunction();
    _fvSyncGetRemoteFilesFrom = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>)>>('fv_sync_get_remote_files_from')
        .asFunction();
    _fvSyncSearchRemote = _lib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Int32)>>('fv_sync_search_remote')
        .asFunction();
    _fvSyncGetRemoteCount = _lib
        .lookup<NativeFunction<Int64 Function(Pointer<Void>)>>('fv_sync_get_remote_count')
        .asFunction();
    _fvSyncIsSyncing = _lib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('fv_sync_is_syncing')
        .asFunction();
  }

  // ═══════════════════════════════════════════════════════════
  // Helpers
  // ═══════════════════════════════════════════════════════════

  void _checkError(int errorCode, [String context = '']) {
    if (errorCode != 0) {
      final error = FVError.fromValue(errorCode);
      final message = getErrorMessage(errorCode);
      throw NativeException(error, context.isEmpty ? message : '$context: $message');
    }
  }

  /// Check for errors using the thread-local last error
  void _checkLastError([String context = '']) {
    final errorCode = _fvLastError();
    if (errorCode != 0) {
      final error = FVError.fromValue(errorCode);
      final messagePtr = _fvLastErrorMessage();
      final message = messagePtr.toDartString();
      _fvClearError();
      throw NativeException(error, context.isEmpty ? message : '$context: $message');
    }
  }

  String _readAndFreeString(Pointer<Utf8> ptr) {
    if (ptr == nullptr) return '';
    final str = ptr.toDartString();
    _fvFreeString(ptr);
    return str;
  }

  /// Read a string from native and check for null (indicates error)
  String _readAndFreeJsonStringOrThrow(Pointer<Utf8> ptr, [String context = '']) {
    if (ptr == nullptr) {
      _checkLastError(context);
      // If no error was set but ptr is null, throw generic error
      throw NativeException(FVError.internal, context.isEmpty ? 'Unknown error' : context);
    }
    return _readAndFreeString(ptr);
  }

  // ═══════════════════════════════════════════════════════════
  // General
  // ═══════════════════════════════════════════════════════════

  /// Получить версию библиотеки
  String getVersion() {
    final ptr = _fvVersion();
    return ptr.toDartString();
  }

  /// Получить текст ошибки
  String getErrorMessage(int errorCode) {
    final ptr = _fvErrorMessage(errorCode);
    return ptr.toDartString();
  }

  // ═══════════════════════════════════════════════════════════
  // Database
  // ═══════════════════════════════════════════════════════════

  /// Инициализировать базу данных
  Future<void> initDatabase([String? path]) async {
    if (_database != null) return;

    final dbPath = path ?? await _getDefaultDbPath();
    _databasePath = dbPath;
    
    final pathPtr = dbPath.toNativeUtf8();
    final errorPtr = calloc<Int32>();

    try {
      _database = _fvDatabaseOpen(pathPtr, errorPtr);
      _checkError(errorPtr.value, 'Failed to open database');

      final initError = _fvDatabaseInitialize(_database!);
      _checkError(initError, 'Failed to initialize database');

      // Создаём менеджеры
      _indexManager = _fvIndexCreate(_database!);
      if (_indexManager == nullptr) _checkLastError('Failed to create index manager');
      
      _searchEngine = _fvSearchCreate(_database!);
      if (_searchEngine == nullptr) _checkLastError('Failed to create search engine');
      
      _tagManager = _fvTagsCreate(_database!);
      if (_tagManager == nullptr) _checkLastError('Failed to create tag manager');
      
      // DuplicateFinder gets IndexManager reference for consistent deletion
      _duplicateFinder = _fvDuplicatesCreate(_database!, _indexManager!);
      if (_duplicateFinder == nullptr) _checkLastError('Failed to create duplicate finder');
      
      // ContentIndexer for text extraction
      _contentIndexer = _fvContentIndexerCreate(_database!);
      if (_contentIndexer == nullptr) _checkLastError('Failed to create content indexer');
    } finally {
      calloc.free(pathPtr);
      calloc.free(errorPtr);
    }
  }

  /// Initialize database for use in a background isolate (skip migrations)
  void _initDatabaseSyncForWorker(String dbPath) {
    if (_database != null) return;

    _databasePath = dbPath;
    final pathPtr = dbPath.toNativeUtf8();
    final errorPtr = calloc<Int32>();

    try {
      _database = _fvDatabaseOpen(pathPtr, errorPtr);
      if (errorPtr.value != 0) {
        throw NativeException(
          FVError.fromValue(errorPtr.value),
          'Failed to open database',
        );
      }

      // Check if already initialized - don't re-run migrations
      if (_fvDatabaseIsInitialized(_database!) == 0) {
        final initError = _fvDatabaseInitialize(_database!);
        if (initError != 0) {
          throw NativeException(FVError.fromValue(initError), 'Failed to initialize database');
        }
      }

      _indexManager = _fvIndexCreate(_database!);
      _searchEngine = _fvSearchCreate(_database!);
      _duplicateFinder = _fvDuplicatesCreate(_database!, _indexManager ?? nullptr);
    } finally {
      calloc.free(pathPtr);
      calloc.free(errorPtr);
    }
  }

  Future<String> _getDefaultDbPath() async {
    final dir = await getApplicationSupportDirectory();
    final dbDir = Directory('${dir.path}/FamilyVault');
    if (!await dbDir.exists()) {
      await dbDir.create(recursive: true);
    }
    return '${dbDir.path}/familyvault.db';
  }

  /// Закрыть базу данных
  void closeDatabase() {
    // Destroy managers first (in reverse order of creation)
    // Each manager automatically decrements database ref count
    if (_contentIndexer != null) {
      _fvContentIndexerDestroy(_contentIndexer!);
      _contentIndexer = null;
    }
    if (_duplicateFinder != null) {
      _fvDuplicatesDestroy(_duplicateFinder!);
      _duplicateFinder = null;
    }
    if (_tagManager != null) {
      _fvTagsDestroy(_tagManager!);
      _tagManager = null;
    }
    if (_searchEngine != null) {
      _fvSearchDestroy(_searchEngine!);
      _searchEngine = null;
    }
    if (_indexManager != null) {
      _fvIndexDestroy(_indexManager!);
      _indexManager = null;
    }
    if (_database != null) {
      final result = _fvDatabaseClose(_database!);
      if (result != 0) {
        // Log but don't throw - we're cleaning up
        final error = FVError.fromValue(result);
        if (error == FVError.busy) {
          // This shouldn't happen if we destroyed all managers
          throw NativeException(error, 'Database still has active references');
        }
      }
      _database = null;
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Index Manager
  // ═══════════════════════════════════════════════════════════

  /// Добавить папку для отслеживания
  int addFolder(String path, String name, int visibility) {
    _ensureIndexManager();
    final pathPtr = path.toNativeUtf8();
    final namePtr = name.toNativeUtf8();
    try {
      final id = _fvIndexAddFolder(_indexManager!, pathPtr, namePtr, visibility);
      if (id < 0) {
        _checkLastError('Failed to add folder');
      }
      return id;
    } finally {
      calloc.free(pathPtr);
      calloc.free(namePtr);
    }
  }

  /// Удалить папку из отслеживания
  void removeFolder(int folderId) {
    _ensureIndexManager();
    final error = _fvIndexRemoveFolder(_indexManager!, folderId);
    _checkError(error, 'Failed to remove folder');
  }

  /// Оптимизировать БД (rebuild FTS + VACUUM)
  void optimizeDatabase() {
    _ensureIndexManager();
    final error = _fvIndexOptimizeDatabase(_indexManager!);
    _checkError(error, 'Failed to optimize database');
  }

  /// Получить максимальный размер текста для индексации (KB)
  int getMaxTextSizeKB() {
    _ensureIndexManager();
    return _fvIndexGetMaxTextSizeKB(_indexManager!);
  }

  /// Установить максимальный размер текста для индексации (KB)
  void setMaxTextSizeKB(int sizeKB) {
    _ensureIndexManager();
    final error = _fvIndexSetMaxTextSizeKB(_indexManager!, sizeKB);
    _checkError(error, 'Failed to set max text size');
  }

  /// Получить список папок
  List<WatchedFolder> getFolders() {
    _ensureIndexManager();
    final ptr = _fvIndexGetFolders(_indexManager!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get folders');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => WatchedFolder.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Проверить, идёт ли сканирование
  bool isScanning() {
    _ensureIndexManager();
    return _fvIndexIsScanning(_indexManager!) != 0;
  }

  /// Сканировать папку (runs in background isolate with cooperative cancellation)
  Future<void> scanFolder(int folderId, {void Function(ScanProgress)? onProgress}) async {
    _ensureIndexManager();
    
    final dbPath = _databasePath;
    if (dbPath == null) {
      throw StateError('Database path not set');
    }

    final progressPort = ReceivePort();
    final completer = Completer<void>();

    progressPort.listen((message) {
      if (message is Map) {
        if (message['type'] == 'progress' && onProgress != null) {
          onProgress(ScanProgress(
            processed: message['processed'] as int,
            total: message['total'] as int,
            currentFile: message['currentFile'] as String?,
            isCountingPhase: message['isCountingPhase'] as bool? ?? false,
          ));
        } else if (message['type'] == 'done') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.complete();
          }
        } else if (message['type'] == 'error') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.completeError(
              NativeException(FVError.internal, message['message'] as String),
            );
          }
        }
      }
    });

    await Isolate.spawn(
      _scanFolderIsolate,
      _ScanFolderParams(
        dbPath: dbPath,
        folderId: folderId,
        progressPort: progressPort.sendPort,
      ),
    );

    await completer.future;
  }

  /// Сканировать все папки (runs in background isolate)
  Future<void> scanAll({void Function(ScanProgress)? onProgress}) async {
    _ensureIndexManager();
    
    final dbPath = _databasePath;
    if (dbPath == null) {
      throw StateError('Database path not set');
    }

    final progressPort = ReceivePort();
    final completer = Completer<void>();

    progressPort.listen((message) {
      if (message is Map) {
        if (message['type'] == 'progress' && onProgress != null) {
          onProgress(ScanProgress(
            processed: message['processed'] as int,
            total: message['total'] as int,
            currentFile: message['currentFile'] as String?,
            isCountingPhase: message['isCountingPhase'] as bool? ?? false,
          ));
        } else if (message['type'] == 'done') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.complete();
          }
        } else if (message['type'] == 'error') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.completeError(
              NativeException(FVError.internal, message['message'] as String),
            );
          }
        }
      }
    });

    await Isolate.spawn(
      _scanAllIsolate,
      _ScanAllParams(
        dbPath: dbPath,
        progressPort: progressPort.sendPort,
      ),
    );

    await completer.future;
  }

  /// Остановить сканирование (cooperative - scan checks for cancellation)
  void stopScan() {
    _ensureIndexManager();
    _fvIndexStopScan(_indexManager!);
  }

  /// Получить файл по ID
  FileRecord? getFile(int fileId) {
    _ensureIndexManager();
    final ptr = _fvIndexGetFile(_indexManager!, fileId);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get file');
    if (json.isEmpty || json == 'null') return null;
    return FileRecord.fromJson(jsonDecode(json) as Map<String, dynamic>);
  }

  /// Удалить файл из индекса (и опционально с диска)
  void deleteFile(int fileId, {bool deleteFromDisk = true}) {
    _ensureIndexManager();
    final error = _fvIndexDeleteFile(_indexManager!, fileId, deleteFromDisk ? 1 : 0);
    _checkError(error, 'Failed to delete file');
  }

  /// Получить недавние файлы (полная версия)
  List<FileRecord> getRecentFiles(int limit) {
    _ensureIndexManager();
    final ptr = _fvIndexGetRecent(_indexManager!, limit);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get recent files');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => FileRecord.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Получить недавние файлы (компактная версия)
  List<FileRecordCompact> getRecentFilesCompact(int limit) {
    _ensureIndexManager();
    final ptr = _fvIndexGetRecentCompact(_indexManager!, limit);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get recent files');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => FileRecordCompact.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Получить файлы папки (компактная версия)
  List<FileRecordCompact> getFilesByFolderCompact(int folderId, int limit, int offset) {
    _ensureIndexManager();
    final ptr = _fvIndexGetByFolderCompact(_indexManager!, folderId, limit, offset);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get folder files');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => FileRecordCompact.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Получить статистику индекса
  IndexStats getStats() {
    _ensureIndexManager();
    final ptr = _fvIndexGetStats(_indexManager!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get stats');
    return IndexStats.fromJson(jsonDecode(json) as Map<String, dynamic>);
  }

  /// Установить видимость папки
  void setFolderVisibility(int folderId, int visibility) {
    _ensureIndexManager();
    final error = _fvIndexSetFolderVisibility(_indexManager!, folderId, visibility);
    _checkError(error, 'Failed to set folder visibility');
  }

  /// Включить/отключить папку
  void setFolderEnabled(int folderId, bool enabled) {
    _ensureIndexManager();
    final error = _fvIndexSetFolderEnabled(_indexManager!, folderId, enabled ? 1 : 0);
    _checkError(error, 'Failed to set folder enabled');
  }

  /// Установить видимость файла
  void setFileVisibility(int fileId, int visibility) {
    _ensureIndexManager();
    final error = _fvIndexSetFileVisibility(_indexManager!, fileId, visibility);
    _checkError(error, 'Failed to set file visibility');
  }

  void _ensureDatabase() {
    if (_database == null) {
      throw StateError('Database not initialized. Call initDatabase() first.');
    }
  }

  void _ensureIndexManager() {
    if (_indexManager == null) {
      throw StateError('Database not initialized. Call initDatabase() first.');
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Search Engine
  // ═══════════════════════════════════════════════════════════

  /// Поиск (полная версия) - runs in background isolate
  Future<List<SearchResult>> search(SearchQuery query) async {
    _ensureSearchEngine();
    
    final dbPath = _databasePath;
    if (dbPath == null) {
      throw StateError('Database path not set');
    }
    
    final queryJson = jsonEncode(query.toJson());

    final json = await Isolate.run(() {
      final bridge = NativeBridge._forIsolate();
      bridge._initDatabaseSyncForWorker(dbPath);
      try {
        final queryPtr = queryJson.toNativeUtf8();
        try {
          final ptr = bridge._fvSearchQuery(bridge._searchEngine!, queryPtr);
          return bridge._readAndFreeJsonStringOrThrow(ptr, 'Search failed');
        } finally {
          calloc.free(queryPtr);
        }
      } finally {
        bridge.closeDatabase();
      }
    });
    
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => SearchResult.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Поиск (компактная версия) - runs in background isolate
  Future<List<SearchResultCompact>> searchCompact(SearchQuery query) async {
    _ensureSearchEngine();
    
    final dbPath = _databasePath;
    if (dbPath == null) {
      throw StateError('Database path not set');
    }
    
    final queryJson = jsonEncode(query.toJson());

    final json = await Isolate.run(() {
      final bridge = NativeBridge._forIsolate();
      bridge._initDatabaseSyncForWorker(dbPath);
      try {
        final queryPtr = queryJson.toNativeUtf8();
        try {
          final ptr = bridge._fvSearchQueryCompact(bridge._searchEngine!, queryPtr);
          return bridge._readAndFreeJsonStringOrThrow(ptr, 'Compact search failed');
        } finally {
          calloc.free(queryPtr);
        }
      } finally {
        bridge.closeDatabase();
      }
    });
    
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => SearchResultCompact.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Подсчёт результатов
  int searchCount(SearchQuery query) {
    _ensureSearchEngine();
    final queryPtr = jsonEncode(query.toJson()).toNativeUtf8();
    try {
      final result = _fvSearchCount(_searchEngine!, queryPtr);
      if (result < 0) {
        _checkLastError('Search count failed');
      }
      return result;
    } finally {
      calloc.free(queryPtr);
    }
  }

  /// Автодополнение
  List<String> searchSuggest(String prefix, int limit) {
    _ensureSearchEngine();
    final prefixPtr = prefix.toNativeUtf8();
    try {
      final ptr = _fvSearchSuggest(_searchEngine!, prefixPtr, limit);
      final json = _readAndFreeJsonStringOrThrow(ptr, 'Suggest failed');
      if (json.isEmpty) return [];
      final list = jsonDecode(json) as List<dynamic>;
      return list.cast<String>();
    } finally {
      calloc.free(prefixPtr);
    }
  }

  void _ensureSearchEngine() {
    if (_searchEngine == null) {
      throw StateError('Database not initialized. Call initDatabase() first.');
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Tag Manager
  // ═══════════════════════════════════════════════════════════

  /// Добавить тег к файлу
  void addTag(int fileId, String tag) {
    _ensureTagManager();
    final tagPtr = tag.toNativeUtf8();
    try {
      final error = _fvTagsAdd(_tagManager!, fileId, tagPtr);
      _checkError(error, 'Failed to add tag');
    } finally {
      calloc.free(tagPtr);
    }
  }

  /// Удалить тег с файла
  void removeTag(int fileId, String tag) {
    _ensureTagManager();
    final tagPtr = tag.toNativeUtf8();
    try {
      final error = _fvTagsRemove(_tagManager!, fileId, tagPtr);
      _checkError(error, 'Failed to remove tag');
    } finally {
      calloc.free(tagPtr);
    }
  }

  /// Получить теги файла
  List<String> getTagsForFile(int fileId) {
    _ensureTagManager();
    final ptr = _fvTagsGetForFile(_tagManager!, fileId);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get file tags');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.cast<String>();
  }

  /// Получить все теги
  List<Tag> getAllTags() {
    _ensureTagManager();
    final ptr = _fvTagsGetAll(_tagManager!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get all tags');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => Tag.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Получить популярные теги
  List<Tag> getPopularTags(int limit) {
    _ensureTagManager();
    final ptr = _fvTagsGetPopular(_tagManager!, limit);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get popular tags');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => Tag.fromJson(e as Map<String, dynamic>)).toList();
  }

  void _ensureTagManager() {
    if (_tagManager == null) {
      throw StateError('Database not initialized. Call initDatabase() first.');
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Duplicate Finder
  // ═══════════════════════════════════════════════════════════

  /// Найти дубликаты
  List<DuplicateGroup> findDuplicates() {
    _ensureDuplicateFinder();
    final ptr = _fvDuplicatesFind(_duplicateFinder!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to find duplicates');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => DuplicateGroup.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Статистика дубликатов
  DuplicateStats getDuplicatesStats() {
    _ensureDuplicateFinder();
    final ptr = _fvDuplicatesStats(_duplicateFinder!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get duplicate stats');
    return DuplicateStats.fromJson(jsonDecode(json) as Map<String, dynamic>);
  }

  /// Файлы без бэкапа
  List<FileRecord> getFilesWithoutBackup() {
    _ensureDuplicateFinder();
    final ptr = _fvDuplicatesWithoutBackup(_duplicateFinder!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to find files without backup');
    if (json.isEmpty) return [];
    final list = jsonDecode(json) as List<dynamic>;
    return list.map((e) => FileRecord.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Удалить файл через DuplicateFinder (uses IndexManager internally)
  void deleteDuplicateFile(int fileId) {
    _ensureDuplicateFinder();
    final error = _fvDuplicatesDeleteFile(_duplicateFinder!, fileId);
    _checkError(error, 'Failed to delete file');
  }

  /// Вычислить недостающие checksums (runs in background isolate)
  Future<void> computeChecksums({void Function(int processed, int total)? onProgress}) async {
    _ensureDuplicateFinder();
    
    final dbPath = _databasePath;
    if (dbPath == null) {
      throw StateError('Database path not set');
    }

    final progressPort = ReceivePort();
    final completer = Completer<void>();

    progressPort.listen((message) {
      if (message is Map) {
        if (message['type'] == 'progress' && onProgress != null) {
          onProgress(
            message['processed'] as int,
            message['total'] as int,
          );
        } else if (message['type'] == 'done') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.complete();
          }
        } else if (message['type'] == 'error') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.completeError(
              NativeException(FVError.internal, message['message'] as String),
            );
          }
        }
      }
    });

    await Isolate.spawn(
      _computeChecksumsIsolate,
      _ComputeChecksumsParams(
        dbPath: dbPath,
        progressPort: progressPort.sendPort,
      ),
    );

    await completer.future;
  }

  void _ensureDuplicateFinder() {
    if (_duplicateFinder == null) {
      throw StateError('Database not initialized. Call initDatabase() first.');
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Content Indexer (Text Extraction)
  // ═══════════════════════════════════════════════════════════

  /// Запустить фоновое извлечение текста
  void startContentIndexer() {
    _ensureContentIndexer();
    final error = _fvContentIndexerStart(_contentIndexer!);
    _checkError(error, 'Failed to start content indexer');
  }

  /// Остановить извлечение текста
  void stopContentIndexer({bool wait = true}) {
    _ensureContentIndexer();
    final error = _fvContentIndexerStop(_contentIndexer!, wait ? 1 : 0);
    _checkError(error, 'Failed to stop content indexer');
  }

  /// Проверить, запущено ли извлечение
  bool isContentIndexerRunning() {
    _ensureContentIndexer();
    return _fvContentIndexerIsRunning(_contentIndexer!) != 0;
  }

  /// Извлечь текст из конкретного файла (синхронно)
  bool processFileContent(int fileId) {
    _ensureContentIndexer();
    final error = _fvContentIndexerProcessFile(_contentIndexer!, fileId);
    return error == 0; // FV_OK
  }

  /// Добавить все необработанные файлы в очередь
  int enqueueUnprocessedContent() {
    _ensureContentIndexer();
    final count = _fvContentIndexerEnqueueUnprocessed(_contentIndexer!);
    if (count < 0) {
      _checkLastError('Failed to enqueue unprocessed files');
    }
    return count;
  }

  /// Получить статус индексации контента
  ContentIndexerStatus getContentIndexerStatus() {
    _ensureContentIndexer();
    final ptr = _fvContentIndexerGetStatus(_contentIndexer!);
    final json = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get content indexer status');
    return ContentIndexerStatus.fromJson(jsonDecode(json) as Map<String, dynamic>);
  }

  /// Получить количество файлов без извлечённого текста
  int getPendingContentCount() {
    _ensureContentIndexer();
    final count = _fvContentIndexerGetPendingCount(_contentIndexer!);
    if (count < 0) {
      _checkLastError('Failed to get pending content count');
    }
    return count;
  }

  /// Проверить, поддерживается ли MIME тип для извлечения
  bool canExtractContent(String mimeType) {
    _ensureContentIndexer();
    final mimePtr = mimeType.toNativeUtf8();
    try {
      return _fvContentIndexerCanExtract(_contentIndexer!, mimePtr) != 0;
    } finally {
      calloc.free(mimePtr);
    }
  }

  /// Получить максимальный размер текста для индексации (KB)
  int getContentIndexerMaxTextSizeKB() {
    _ensureContentIndexer();
    return _fvContentIndexerGetMaxTextSizeKB(_contentIndexer!);
  }

  /// Установить максимальный размер текста для индексации (KB)
  void setContentIndexerMaxTextSizeKB(int sizeKB) {
    _ensureContentIndexer();
    _fvContentIndexerSetMaxTextSizeKB(_contentIndexer!, sizeKB);
  }

  /// Переиндексировать весь контент (runs in background isolate)
  Future<void> reindexAllContent({void Function(int processed, int total)? onProgress}) async {
    _ensureContentIndexer();
    
    final dbPath = _databasePath;
    if (dbPath == null) {
      throw StateError('Database path not set');
    }

    final progressPort = ReceivePort();
    final completer = Completer<void>();

    progressPort.listen((message) {
      if (message is Map) {
        if (message['type'] == 'progress' && onProgress != null) {
          onProgress(
            message['processed'] as int,
            message['total'] as int,
          );
        } else if (message['type'] == 'done') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.complete();
          }
        } else if (message['type'] == 'error') {
          progressPort.close();
          if (!completer.isCompleted) {
            completer.completeError(
              NativeException(FVError.internal, message['message'] as String),
            );
          }
        }
      }
    });

    await Isolate.spawn(
      _reindexContentIsolate,
      _ReindexContentParams(
        dbPath: dbPath,
        progressPort: progressPort.sendPort,
      ),
    );

    await completer.future;
  }

  void _ensureContentIndexer() {
    if (_contentIndexer == null) {
      throw StateError('Database not initialized. Call initDatabase() first.');
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Family Pairing
  // ═══════════════════════════════════════════════════════════

  /// Инициализировать SecureStorage и FamilyPairing
  void initPairing() {
    if (_familyPairing != null) return;

    _secureStorage = _fvSecureCreate();
    if (_secureStorage == null || _secureStorage == nullptr) {
      _checkLastError('Failed to create secure storage');
      _secureStorage = null;
      throw StateError('Failed to create secure storage');
    }

    _familyPairing = _fvPairingCreate(_secureStorage!);
    if (_familyPairing == null || _familyPairing == nullptr) {
      _checkLastError('Failed to create family pairing');
      _fvSecureDestroy(_secureStorage!);
      _secureStorage = null;
      throw StateError('Failed to create family pairing');
    }
  }

  /// Закрыть FamilyPairing (вызывается при закрытии приложения)
  void closePairing() {
    if (_familyPairing != null) {
      _fvPairingDestroy(_familyPairing!);
      _familyPairing = null;
    }
    if (_secureStorage != null) {
      _fvSecureDestroy(_secureStorage!);
      _secureStorage = null;
    }
  }

  /// Проверить, настроена ли семья
  bool isFamilyConfigured() {
    _ensurePairing();
    return _fvPairingIsConfigured(_familyPairing!) != 0;
  }

  /// Получить ID устройства
  String getDeviceId() {
    _ensurePairing();
    final ptr = _fvPairingGetDeviceId(_familyPairing!);
    return _readAndFreeJsonStringOrThrow(ptr, 'Failed to get device ID');
  }

  /// Получить имя устройства
  String getDeviceName() {
    _ensurePairing();
    final ptr = _fvPairingGetDeviceName(_familyPairing!);
    return _readAndFreeJsonStringOrThrow(ptr, 'Failed to get device name');
  }

  /// Установить имя устройства
  void setDeviceName(String name) {
    _ensurePairing();
    final namePtr = name.toNativeUtf8();
    try {
      _fvPairingSetDeviceName(_familyPairing!, namePtr);
    } finally {
      calloc.free(namePtr);
    }
  }

  /// Получить тип устройства
  DeviceType getDeviceType() {
    _ensurePairing();
    final typeInt = _fvPairingGetDeviceType(_familyPairing!);
    return DeviceType.fromValue(typeInt);
  }

  /// Создать новую семью
  /// Возвращает PairingInfo с PIN и QR данными
  PairingInfo? createFamily() {
    _ensurePairing();
    final ptr = _fvPairingCreateFamily(_familyPairing!);
    final json = _readAndFreeString(ptr);
    if (json.isEmpty) {
      _checkLastError('Failed to create family');
      return null;
    }
    return PairingInfo.fromJson(jsonDecode(json) as Map<String, dynamic>);
  }

  /// Сгенерировать новый PIN
  PairingInfo? regeneratePin() {
    _ensurePairing();
    final ptr = _fvPairingRegeneratePin(_familyPairing!);
    final json = _readAndFreeString(ptr);
    if (json.isEmpty) {
      _checkLastError('Failed to regenerate PIN');
      return null;
    }
    return PairingInfo.fromJson(jsonDecode(json) as Map<String, dynamic>);
  }

  /// Отменить активную pairing сессию
  void cancelPairing() {
    _ensurePairing();
    _fvPairingCancel(_familyPairing!);
  }

  /// Есть ли активная pairing сессия
  bool hasPendingPairing() {
    _ensurePairing();
    return _fvPairingHasPending(_familyPairing!) != 0;
  }

  /// Присоединиться к семье по PIN
  JoinResult joinFamilyByPin(String pin, {String? host, int port = 0}) {
    _ensurePairing();
    final pinPtr = pin.toNativeUtf8();
    final hostPtr = (host ?? '').toNativeUtf8();
    try {
      final result = _fvPairingJoinPin(_familyPairing!, pinPtr, hostPtr, port);
      return JoinResult.fromValue(result);
    } finally {
      calloc.free(pinPtr);
      calloc.free(hostPtr);
    }
  }

  /// Присоединиться к семье по QR
  JoinResult joinFamilyByQR(String qrData) {
    _ensurePairing();
    final qrPtr = qrData.toNativeUtf8();
    try {
      final result = _fvPairingJoinQr(_familyPairing!, qrPtr);
      return JoinResult.fromValue(result);
    } finally {
      calloc.free(qrPtr);
    }
  }

  /// Сбросить семейную конфигурацию
  void resetFamily() {
    _ensurePairing();
    _fvPairingReset(_familyPairing!);
  }

  /// Запустить pairing сервер (для приёма входящих подключений)
  /// @param port Порт (0 = использовать порт по умолчанию 45680)
  void startPairingServer({int port = 0}) {
    _ensurePairing();
    final result = _fvPairingStartServer(_familyPairing!, port);
    if (result != 0) {
      throw NativeException(FVError.fromValue(result), 'Failed to start pairing server');
    }
  }

  /// Остановить pairing сервер
  void stopPairingServer() {
    _ensurePairing();
    _fvPairingStopServer(_familyPairing!);
  }

  /// Проверить, запущен ли pairing сервер
  bool isPairingServerRunning() {
    _ensurePairing();
    return _fvPairingIsServerRunning(_familyPairing!) != 0;
  }

  /// Получить порт pairing сервера
  int getPairingServerPort() {
    _ensurePairing();
    return _fvPairingGetServerPort(_familyPairing!);
  }

  /// Получить информацию о текущем устройстве
  DeviceInfo getThisDeviceInfo() {
    _ensurePairing();
    return DeviceInfo(
      deviceId: getDeviceId(),
      deviceName: getDeviceName(),
      deviceType: getDeviceType(),
      fileCount: isInitialized ? getStats().totalFiles : 0,
    );
  }

  void _ensurePairing() {
    if (_familyPairing == null) {
      throw StateError('Pairing not initialized. Call initPairing() first.');
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Secure Storage
  // ═══════════════════════════════════════════════════════════

  bool get isSecureStorageReady => _secureStorage != null;

  void _ensureSecureStorage() {
    if (_secureStorage == null) {
      throw StateError('SecureStorage not initialized. Call initPairing() first.');
    }
  }

  /// Сохранить строку в защищённое хранилище
  void secureStore(String key, String value) {
    _ensureSecureStorage();
    final keyPtr = key.toNativeUtf8();
    final valuePtr = value.toNativeUtf8();
    try {
      final error = _fvSecureStoreString(_secureStorage!, keyPtr, valuePtr);
      _checkError(error, 'Failed to store secure value');
    } finally {
      calloc.free(keyPtr);
      calloc.free(valuePtr);
    }
  }

  /// Получить строку из защищённого хранилища
  String? secureRetrieve(String key) {
    _ensureSecureStorage();
    final keyPtr = key.toNativeUtf8();
    try {
      final ptr = _fvSecureRetrieveString(_secureStorage!, keyPtr);
      if (ptr == nullptr) return null;
      final value = ptr.toDartString();
      _fvFreeString(ptr);
      return value.isEmpty ? null : value;
    } finally {
      calloc.free(keyPtr);
    }
  }

  /// Удалить значение из защищённого хранилища
  void secureRemove(String key) {
    _ensureSecureStorage();
    final keyPtr = key.toNativeUtf8();
    try {
      _fvSecureRemove(_secureStorage!, keyPtr);
    } finally {
      calloc.free(keyPtr);
    }
  }

  /// Проверить наличие ключа в защищённом хранилище
  bool secureExists(String key) {
    _ensureSecureStorage();
    final keyPtr = key.toNativeUtf8();
    try {
      return _fvSecureExists(_secureStorage!, keyPtr) != 0;
    } finally {
      calloc.free(keyPtr);
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Network Discovery
  // ═══════════════════════════════════════════════════════════

  /// Запустить обнаружение устройств в сети
  void startDiscovery() {
    _ensurePairing();
    
    // Создаём discovery если не существует
    _networkDiscovery ??= _fvDiscoveryCreate();
    if (_networkDiscovery == null || _networkDiscovery == nullptr) {
      _checkLastError('Failed to create network discovery');
      return;
    }

    final error = _fvDiscoveryStart(
      _networkDiscovery!,
      _familyPairing!,
      nullptr, // No callback - use polling instead
      nullptr,
    );
    if (error != 0) {
      _checkLastError('Failed to start discovery');
    }
  }

  /// Остановить обнаружение устройств
  void stopDiscovery() {
    if (_networkDiscovery != null && _networkDiscovery != nullptr) {
      _fvDiscoveryStop(_networkDiscovery!);
    }
  }

  /// Проверить, запущен ли discovery
  bool isDiscoveryRunning() {
    if (_networkDiscovery == null || _networkDiscovery == nullptr) {
      return false;
    }
    return _fvDiscoveryIsRunning(_networkDiscovery!) != 0;
  }

  /// Получить список обнаруженных устройств
  List<DeviceInfo> getDiscoveredDevices() {
    if (_networkDiscovery == null || _networkDiscovery == nullptr) {
      return [];
    }
    
    final ptr = _fvDiscoveryGetDevices(_networkDiscovery!);
    if (ptr == nullptr) {
      return [];
    }
    
    final jsonStr = ptr.toDartString();
    _fvFreeString(ptr);
    
    if (jsonStr.isEmpty || jsonStr == '[]') {
      return [];
    }
    
    final List<dynamic> jsonList = jsonDecode(jsonStr);
    return jsonList.map((j) => DeviceInfo.fromJson(j)).toList();
  }

  /// Получить количество обнаруженных устройств
  int getDiscoveredDeviceCount() {
    if (_networkDiscovery == null || _networkDiscovery == nullptr) {
      return 0;
    }
    return _fvDiscoveryGetDeviceCount(_networkDiscovery!);
  }

  /// Получить устройство по ID
  DeviceInfo? getDiscoveredDevice(String deviceId) {
    if (_networkDiscovery == null || _networkDiscovery == nullptr) {
      return null;
    }
    
    final deviceIdPtr = deviceId.toNativeUtf8();
    try {
      final ptr = _fvDiscoveryGetDevice(_networkDiscovery!, deviceIdPtr);
      if (ptr == nullptr) {
        return null;
      }
      
      final jsonStr = ptr.toDartString();
      _fvFreeString(ptr);
      
      if (jsonStr.isEmpty) {
        return null;
      }
      
      return DeviceInfo.fromJson(jsonDecode(jsonStr));
    } finally {
      calloc.free(deviceIdPtr);
    }
  }

  /// Получить локальные IP адреса этого устройства
  List<String> getLocalIpAddresses() {
    final ptr = _fvDiscoveryGetLocalIps();
    if (ptr == nullptr) {
      return [];
    }
    
    final jsonStr = ptr.toDartString();
    _fvFreeString(ptr);
    
    if (jsonStr.isEmpty || jsonStr == '[]') {
      return [];
    }
    
    final List<dynamic> jsonList = jsonDecode(jsonStr);
    return jsonList.cast<String>();
  }

  /// Уничтожить discovery (вызывается при закрытии приложения)
  void disposeDiscovery() {
    if (_networkDiscovery != null && _networkDiscovery != nullptr) {
      _fvDiscoveryStop(_networkDiscovery!);
      _fvDiscoveryDestroy(_networkDiscovery!);
      _networkDiscovery = null;
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Network Manager
  // ═══════════════════════════════════════════════════════════

  /// Инициализировать NetworkManager
  void initNetworkManager() {
    if (_networkManager != null) return;
    _ensurePairing();

    _networkManager = _fvNetworkCreate(_familyPairing!);
    if (_networkManager == nullptr) {
      _checkLastError('Failed to create network manager');
      _networkManager = null;
      throw StateError('Failed to create network manager');
    }
  }

  void _ensureNetworkManager() {
    if (_networkManager == null) {
      initNetworkManager();
    }
  }

  /// Запустить P2P сеть
  /// @param port TCP порт (0 = авто)
  /// @param onEvent Callback для событий сети (event, jsonData)
  void startNetwork({
    int port = 0,
    void Function(int event, String jsonData)? onEvent,
  }) {
    _ensureNetworkManager();

    // Keep previous callback until new start succeeds
    final previousCallback = _networkCallback;
    _networkCallback = null;

    Pointer<NativeFunction<Void Function(Int32, Pointer<Utf8>, Pointer<Void>)>> callbackPtr = nullptr;
    
    if (onEvent != null) {
      _networkCallback = NativeCallable<Void Function(Int32, Pointer<Utf8>, Pointer<Void>)>.listener(
        (int event, Pointer<Utf8> dataPtr, Pointer<Void> userData) {
          // Read string first, then free it (native allocates with strdup)
          final jsonData = dataPtr == nullptr ? '{}' : dataPtr.toDartString();
          if (dataPtr != nullptr) {
            _fvFreeString(dataPtr);  // CRITICAL: free heap-allocated event data
          }
          onEvent(event, jsonData);
        },
      );
      callbackPtr = _networkCallback!.nativeFunction;
    }
    
    try {
      final error = _fvNetworkStart(_networkManager!, port, callbackPtr, nullptr);
      _checkError(error, 'Failed to start network');
      // New start succeeded — safe to close previous callback
      previousCallback?.close();
    } catch (e) {
      // Ensure native state is cleaned before touching the callback
      if (_networkManager != null && _networkManager != nullptr) {
        _fvNetworkStop(_networkManager!);
      }
      // Close new callback allocated for this start attempt
      _networkCallback?.close();
      // Restore previous callback so native side (if already registered) still has a valid target
      _networkCallback = previousCallback;
      rethrow;
    }
  }

  /// Остановить P2P сеть
  void stopNetwork() {
    if (_networkManager != null && _networkManager != nullptr) {
      _fvNetworkStop(_networkManager!);
    }
    // Cleanup callback
    _networkCallback?.close();
    _networkCallback = null;
  }

  /// Проверить, запущена ли сеть
  bool isNetworkRunning() {
    if (_networkManager == null || _networkManager == nullptr) {
      return false;
    }
    return _fvNetworkIsRunning(_networkManager!) != 0;
  }

  /// Получить состояние сети
  /// 0=stopped, 1=starting, 2=running, 3=stopping, 4=error
  int getNetworkState() {
    if (_networkManager == null || _networkManager == nullptr) {
      return 0;
    }
    return _fvNetworkGetState(_networkManager!);
  }

  /// Получить порт сервера
  int getNetworkPort() {
    if (_networkManager == null || _networkManager == nullptr) {
      return 0;
    }
    return _fvNetworkGetPort(_networkManager!);
  }

  /// Получить обнаруженные устройства (через NetworkManager)
  List<DeviceInfo> getNetworkDiscoveredDevices() {
    _ensureNetworkManager();
    final ptr = _fvNetworkGetDiscoveredDevices(_networkManager!);
    if (ptr == nullptr) return [];

    final jsonStr = ptr.toDartString();
    _fvFreeString(ptr);
    
    if (jsonStr.isEmpty || jsonStr == '[]') return [];
    
    final List<dynamic> jsonList = jsonDecode(jsonStr);
    return jsonList.map((j) => DeviceInfo.fromJson(j)).toList();
  }

  /// Получить подключённые устройства
  List<DeviceInfo> getConnectedDevices() {
    _ensureNetworkManager();
    final ptr = _fvNetworkGetConnectedDevices(_networkManager!);
    if (ptr == nullptr) return [];

    final jsonStr = ptr.toDartString();
    _fvFreeString(ptr);
    
    if (jsonStr.isEmpty || jsonStr == '[]') return [];
    
    final List<dynamic> jsonList = jsonDecode(jsonStr);
    return jsonList.map((j) => DeviceInfo.fromJson(j)).toList();
  }

  /// Подключиться к устройству по ID
  void connectToDevice(String deviceId) {
    _ensureNetworkManager();
    final deviceIdPtr = deviceId.toNativeUtf8();
    try {
      final error = _fvNetworkConnectToDevice(_networkManager!, deviceIdPtr);
      _checkError(error, 'Failed to connect to device');
    } finally {
      calloc.free(deviceIdPtr);
    }
  }

  /// Подключиться к устройству по адресу
  void connectToAddress(String host, int port) {
    _ensureNetworkManager();
    final hostPtr = host.toNativeUtf8();
    try {
      final error = _fvNetworkConnectToAddress(_networkManager!, hostPtr, port);
      _checkError(error, 'Failed to connect to address');
    } finally {
      calloc.free(hostPtr);
    }
  }

  /// Отключиться от устройства
  void disconnectFromDevice(String deviceId) {
    if (_networkManager == null || _networkManager == nullptr) return;
    final deviceIdPtr = deviceId.toNativeUtf8();
    try {
      _fvNetworkDisconnectDevice(_networkManager!, deviceIdPtr);
    } finally {
      calloc.free(deviceIdPtr);
    }
  }

  /// Отключиться от всех устройств
  void disconnectAllDevices() {
    if (_networkManager == null || _networkManager == nullptr) return;
    _fvNetworkDisconnectAll(_networkManager!);
  }

  /// Проверить подключение к устройству
  bool isConnectedTo(String deviceId) {
    if (_networkManager == null || _networkManager == nullptr) return false;
    final deviceIdPtr = deviceId.toNativeUtf8();
    try {
      return _fvNetworkIsConnectedTo(_networkManager!, deviceIdPtr) != 0;
    } finally {
      calloc.free(deviceIdPtr);
    }
  }

  /// Получить последнюю ошибку сети
  String? getNetworkLastError() {
    if (_networkManager == null || _networkManager == nullptr) return null;
    final ptr = _fvNetworkGetLastError(_networkManager!);
    if (ptr == nullptr) return null;
    final str = ptr.toDartString();
    _fvFreeString(ptr);
    return str.isEmpty ? null : str;
  }

  /// Установить базу данных для синхронизации индекса
  /// Должен вызываться после initDatabase и startNetwork
  void setNetworkDatabase() {
    _ensureDatabase();
    _ensurePairing();
    if (_networkManager == null || _networkManager == nullptr) {
      throw NativeException(FVError.invalidArgument, 'NetworkManager not initialized');
    }
    final deviceIdPtr = getDeviceId().toNativeUtf8();
    try {
      final result = _fvNetworkSetDatabase(_networkManager!, _database!, deviceIdPtr);
      if (result != 0) {
        throw NativeException(FVError.fromValue(result), 'Failed to set network database');
      }
    } finally {
      calloc.free(deviceIdPtr);
    }
  }

  /// Запросить синхронизацию индекса с устройством
  /// @param deviceId ID устройства (должно быть подключено)
  /// @param fullSync true для полной синхронизации (все файлы)
  void requestIndexSync(String deviceId, {bool fullSync = false}) {
    if (_networkManager == null || _networkManager == nullptr) {
      throw NativeException(FVError.invalidArgument, 'NetworkManager not initialized');
    }
    final deviceIdPtr = deviceId.toNativeUtf8();
    try {
      final result = _fvNetworkRequestSync(_networkManager!, deviceIdPtr, fullSync ? 1 : 0);
      if (result != 0) {
        throw NativeException(FVError.fromValue(result), 'Failed to request sync');
      }
    } finally {
      calloc.free(deviceIdPtr);
    }
  }

  List<RemoteFileRecord> _parseRemoteFilePointer(Pointer<Utf8> ptr) {
    if (ptr == nullptr) return [];
    String jsonStr;
    try {
      jsonStr = ptr.toDartString();
    } finally {
      _fvFreeString(ptr); // free exactly once
    }

    try {
      return _decodeRemoteFiles(jsonStr);
    } catch (_) {
      return [];
    }
  }

  List<RemoteFileRecord> _decodeRemoteFiles(String jsonStr) {
    if (jsonStr.isEmpty || jsonStr == '[]') return [];
    try {
      final List<dynamic> jsonList = jsonDecode(jsonStr);
      return jsonList
          .map((item) => RemoteFileRecord.fromJson(
              Map<String, dynamic>.from(item as Map<dynamic, dynamic>)))
          .toList();
    } catch (_) {
      return [];
    }
  }

  /// Получить список удалённых файлов (с других устройств семьи)
  List<RemoteFileRecord> getNetworkRemoteFiles() {
    if (_networkManager == null || _networkManager == nullptr) return [];
    final ptr = _fvNetworkGetRemoteFiles(_networkManager!);
    return _parseRemoteFilePointer(ptr);
  }

  /// Поиск по удалённым файлам
  List<RemoteFileRecord> searchNetworkRemoteFiles(String query, {int limit = 50}) {
    if (_networkManager == null || _networkManager == nullptr) return [];
    final queryPtr = query.toNativeUtf8();
    try {
      final ptr = _fvNetworkSearchRemoteFiles(_networkManager!, queryPtr, limit);
      return _parseRemoteFilePointer(ptr);
    } finally {
      calloc.free(queryPtr);
    }
  }

  /// Получить количество удалённых файлов
  int getNetworkRemoteFileCount() {
    if (_networkManager == null || _networkManager == nullptr) return 0;
    return _fvNetworkGetRemoteFileCount(_networkManager!);
  }

  // ═══════════════════════════════════════════════════════════
  // File Transfer
  // ═══════════════════════════════════════════════════════════

  /// Установить директорию кэша для файлов
  void setFileCacheDir(String cacheDir) {
    if (_networkManager == null || _networkManager == nullptr) {
      throw NativeException(FVError.invalidArgument, 'NetworkManager not initialized');
    }
    final cacheDirPtr = cacheDir.toNativeUtf8();
    try {
      final result = _fvNetworkSetCacheDir(_networkManager!, cacheDirPtr);
      if (result != 0) {
        throw NativeException(FVError.fromValue(result), 'Failed to set cache directory');
      }
    } finally {
      calloc.free(cacheDirPtr);
    }
  }

  /// Результат запроса файла
  /// Возвращает либо request ID (начинается НЕ с "CACHED:"), либо путь к кэшу (начинается с "CACHED:")
  static const String _cacheHitPrefix = 'CACHED:';

  /// Запросить файл с удалённого устройства
  /// @return FileRequestResult с requestId или cachedPath
  FileRequestResult? requestRemoteFile({
    required String deviceId,
    required int fileId,
    required String fileName,
    required int expectedSize,
    String? checksum,
  }) {
    if (_networkManager == null || _networkManager == nullptr) return null;
    
    final deviceIdPtr = deviceId.toNativeUtf8();
    final fileNamePtr = fileName.toNativeUtf8();
    final checksumPtr = checksum?.toNativeUtf8() ?? nullptr;
    
    try {
      final resultPtr = _fvNetworkRequestFile(
        _networkManager!,
        deviceIdPtr,
        fileId,
        fileNamePtr,
        expectedSize,
        checksumPtr,
      );
      
      if (resultPtr == nullptr) return null;
      final result = resultPtr.toDartString();
      _fvFreeString(resultPtr);
      if (result.isEmpty) return null;
      
      // Check for cache hit
      if (result.startsWith(_cacheHitPrefix)) {
        return FileRequestResult.cached(result.substring(_cacheHitPrefix.length));
      }
      return FileRequestResult.pending(result);
    } finally {
      calloc.free(deviceIdPtr);
      calloc.free(fileNamePtr);
      if (checksumPtr != nullptr) calloc.free(checksumPtr);
    }
  }

  /// Отменить запрос файла
  void cancelFileRequest(String requestId) {
    if (_networkManager == null || _networkManager == nullptr) return;
    final requestIdPtr = requestId.toNativeUtf8();
    try {
      _fvNetworkCancelFileRequest(_networkManager!, requestIdPtr);
    } finally {
      calloc.free(requestIdPtr);
    }
  }

  /// Отменить все запросы файлов к устройству
  void cancelAllFileRequests(String deviceId) {
    if (_networkManager == null || _networkManager == nullptr) return;
    final deviceIdPtr = deviceId.toNativeUtf8();
    try {
      _fvNetworkCancelAllFileRequests(_networkManager!, deviceIdPtr);
    } finally {
      calloc.free(deviceIdPtr);
    }
  }

  /// Получить активные передачи
  List<Map<String, dynamic>> getActiveTransfers() {
    if (_networkManager == null || _networkManager == nullptr) return [];
    final ptr = _fvNetworkGetActiveTransfers(_networkManager!);
    if (ptr == nullptr) return [];
    try {
      final jsonStr = ptr.toDartString();
      _fvFreeString(ptr);
      if (jsonStr.isEmpty) return [];
      final list = jsonDecode(jsonStr) as List;
      return list.cast<Map<String, dynamic>>();
    } catch (e) {
      return [];
    }
  }

  /// Получить прогресс передачи
  Map<String, dynamic>? getTransferProgress(String requestId) {
    if (_networkManager == null || _networkManager == nullptr) return null;
    final requestIdPtr = requestId.toNativeUtf8();
    try {
      final ptr = _fvNetworkGetTransferProgress(_networkManager!, requestIdPtr);
      if (ptr == nullptr) return null;
      final jsonStr = ptr.toDartString();
      _fvFreeString(ptr);
      if (jsonStr.isEmpty) return null;
      return jsonDecode(jsonStr) as Map<String, dynamic>;
    } catch (e) {
      return null;
    } finally {
      calloc.free(requestIdPtr);
    }
  }

  /// Проверить, есть ли файл в кэше
  /// @param deviceId ID устройства-источника
  /// @param fileId ID файла
  /// @param checksum Контрольная сумма (опционально)
  bool isFileCached(String deviceId, int fileId, {String? checksum}) {
    if (_networkManager == null || _networkManager == nullptr) return false;
    final deviceIdPtr = deviceId.toNativeUtf8();
    final checksumPtr = checksum?.toNativeUtf8() ?? nullptr;
    try {
      return _fvNetworkIsFileCached(_networkManager!, deviceIdPtr, fileId, checksumPtr) != 0;
    } finally {
      calloc.free(deviceIdPtr);
      if (checksumPtr != nullptr) calloc.free(checksumPtr);
    }
  }

  /// Получить путь к кэшированному файлу
  /// @param deviceId ID устройства-источника
  /// @param fileId ID файла
  String? getCachedFilePath(String deviceId, int fileId) {
    if (_networkManager == null || _networkManager == nullptr) return null;
    final deviceIdPtr = deviceId.toNativeUtf8();
    try {
      final ptr = _fvNetworkGetCachedPath(_networkManager!, deviceIdPtr, fileId);
      if (ptr == nullptr) return null;
      final path = ptr.toDartString();
      _fvFreeString(ptr);
      return path.isEmpty ? null : path;
    } finally {
      calloc.free(deviceIdPtr);
    }
  }

  /// Очистить кэш файлов
  void clearFileCache() {
    if (_networkManager == null || _networkManager == nullptr) return;
    _fvNetworkClearCache(_networkManager!);
  }

  /// Получить размер кэша (в байтах)
  int getFileCacheSize() {
    if (_networkManager == null || _networkManager == nullptr) return 0;
    return _fvNetworkGetCacheSize(_networkManager!);
  }

  /// Проверить, есть ли активные передачи
  bool hasActiveTransfers() {
    if (_networkManager == null || _networkManager == nullptr) return false;
    return _fvNetworkHasActiveTransfers(_networkManager!) != 0;
  }

  /// Уничтожить NetworkManager
  void disposeNetworkManager() {
    if (_networkManager != null && _networkManager != nullptr) {
      _fvNetworkStop(_networkManager!);
      _fvNetworkDestroy(_networkManager!);
      _networkManager = null;
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Cloud Accounts
  // ═══════════════════════════════════════════════════════════

  int addCloudAccount(String type, String email, String displayName, String avatarUrl) {
    _ensureDatabase();
    final typePtr = type.toNativeUtf8();
    final emailPtr = email.toNativeUtf8();
    final namePtr = displayName.toNativeUtf8();
    final avatarPtr = avatarUrl.toNativeUtf8();
    try {
      final id = _fvCloudAccountAdd(_database!, typePtr, emailPtr, namePtr, avatarPtr);
      if (id < 0) {
        _checkLastError('Failed to add cloud account');
      }
      return id;
    } finally {
      calloc.free(typePtr);
      calloc.free(emailPtr);
      calloc.free(namePtr);
      calloc.free(avatarPtr);
    }
  }

  List<CloudAccount> getCloudAccounts() {
    _ensureDatabase();
    final ptr = _fvCloudAccountList(_database!);
    final jsonStr = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get cloud accounts');
    if (jsonStr.isEmpty) return [];

    final List<dynamic> json = jsonDecode(jsonStr);
    return json.map((e) => CloudAccount.fromJson(e)).toList();
  }

  void removeCloudAccount(int accountId) {
    _ensureDatabase();
    if (!_fvCloudAccountRemove(_database!, accountId)) {
      _checkLastError('Failed to remove cloud account');
    }
  }

  void setCloudAccountEnabled(int accountId, bool enabled) {
    _ensureDatabase();
    if (!_fvCloudAccountSetEnabled(_database!, accountId, enabled)) {
      _checkLastError('Failed to set cloud account enabled');
    }
  }

  void updateCloudAccountSync(int accountId, int fileCount, String changeToken) {
    _ensureDatabase();
    final tokenPtr = changeToken.toNativeUtf8();
    try {
      if (!_fvCloudAccountUpdateSync(_database!, accountId, fileCount, tokenPtr)) {
        _checkLastError('Failed to update cloud account sync');
      }
    } finally {
      calloc.free(tokenPtr);
    }
  }

  int addCloudFolder(int accountId, String cloudId, String name, String path) {
    _ensureDatabase();
    final cloudIdPtr = cloudId.toNativeUtf8();
    final namePtr = name.toNativeUtf8();
    final pathPtr = path.toNativeUtf8();
    try {
      final id = _fvCloudFolderAdd(_database!, accountId, cloudIdPtr, namePtr, pathPtr);
      if (id < 0) {
        _checkLastError('Failed to add cloud folder');
      }
      return id;
    } finally {
      calloc.free(cloudIdPtr);
      calloc.free(namePtr);
      calloc.free(pathPtr);
    }
  }

  void removeCloudFolder(int folderId) {
    _ensureDatabase();
    if (!_fvCloudFolderRemove(_database!, folderId)) {
      _checkLastError('Failed to remove cloud folder');
    }
  }

  List<CloudWatchedFolder> getCloudFolders(int accountId) {
    _ensureDatabase();
    final ptr = _fvCloudFolderList(_database!, accountId);
    final jsonStr = _readAndFreeJsonStringOrThrow(ptr, 'Failed to get cloud folders');
    if (jsonStr.isEmpty) return [];

    final List<dynamic> json = jsonDecode(jsonStr);
    return json.map((e) => CloudWatchedFolder.fromJson(e)).toList();
  }

  void setCloudFolderEnabled(int folderId, bool enabled) {
    _ensureDatabase();
    if (!_fvCloudFolderSetEnabled(_database!, folderId, enabled)) {
      _checkLastError('Failed to set cloud folder enabled');
    }
  }

  void upsertCloudFile(int accountId, String fileJson) {
    _ensureDatabase();
    final jsonPtr = fileJson.toNativeUtf8();
    try {
      if (!_fvCloudFileUpsert(_database!, accountId, jsonPtr)) {
        _checkLastError('Failed to upsert cloud file');
      }
    } finally {
      calloc.free(jsonPtr);
    }
  }

  void removeCloudFile(int accountId, String cloudId) {
    _ensureDatabase();
    final idPtr = cloudId.toNativeUtf8();
    try {
      if (!_fvCloudFileRemove(_database!, accountId, idPtr)) {
        _checkLastError('Failed to remove cloud file');
      }
    } finally {
      calloc.free(idPtr);
    }
  }

  void removeAllCloudFiles(int accountId) {
    _ensureDatabase();
    if (!_fvCloudFileRemoveAll(_database!, accountId)) {
        _checkLastError('Failed to remove all cloud files');
    }
  }

  // ═══════════════════════════════════════════════════════════
  // Index Sync Manager
  // ═══════════════════════════════════════════════════════════

  /// Инициализировать IndexSyncManager
  void initSyncManager() {
    if (_indexSyncManager != null) return;
    if (_database == null) throw StateError('Database not initialized');
    _ensurePairing();

    final deviceIdPtr = getDeviceId().toNativeUtf8();
    try {
      _indexSyncManager = _fvSyncCreate(_database!, deviceIdPtr);
      if (_indexSyncManager == nullptr) {
        _checkLastError('Failed to create sync manager');
        _indexSyncManager = null;
        throw StateError('Failed to create sync manager');
      }
    } finally {
      calloc.free(deviceIdPtr);
    }
  }

  void _ensureSyncManager() {
    if (_indexSyncManager == null) {
      initSyncManager();
    }
  }

  /// Получить все удалённые файлы
  List<RemoteFileRecord> getRemoteFiles() {
    _ensureSyncManager();
    final ptr = _fvSyncGetRemoteFiles(_indexSyncManager!);
    return _parseRemoteFilePointer(ptr);
  }

  /// Получить удалённые файлы с устройства
  List<RemoteFileRecord> getRemoteFilesFrom(String deviceId) {
    _ensureSyncManager();
    final deviceIdPtr = deviceId.toNativeUtf8();
    try {
      final ptr = _fvSyncGetRemoteFilesFrom(_indexSyncManager!, deviceIdPtr);
      return _parseRemoteFilePointer(ptr);
    } finally {
      calloc.free(deviceIdPtr);
    }
  }

  /// Поиск по удалённым файлам
  List<RemoteFileRecord> searchRemoteFiles(String query, {int limit = 50}) {
    _ensureSyncManager();
    final queryPtr = query.toNativeUtf8();
    try {
      final ptr = _fvSyncSearchRemote(_indexSyncManager!, queryPtr, limit);
      return _parseRemoteFilePointer(ptr);
    } finally {
      calloc.free(queryPtr);
    }
  }

  /// Количество удалённых файлов
  int getRemoteFileCount() {
    _ensureSyncManager();
    return _fvSyncGetRemoteCount(_indexSyncManager!);
  }

  /// Проверить, идёт ли синхронизация
  bool isSyncing() {
    if (_indexSyncManager == null || _indexSyncManager == nullptr) return false;
    return _fvSyncIsSyncing(_indexSyncManager!) != 0;
  }

  /// Уничтожить IndexSyncManager
  void disposeSyncManager() {
    if (_indexSyncManager != null && _indexSyncManager != nullptr) {
      _fvSyncDestroy(_indexSyncManager!);
      _indexSyncManager = null;
    }
  }
}

// ═══════════════════════════════════════════════════════════
// Background Isolate Functions
// ═══════════════════════════════════════════════════════════

/// Parameters for scanFolder isolate
class _ScanFolderParams {
  final String dbPath;
  final int folderId;
  final SendPort progressPort;

  _ScanFolderParams({
    required this.dbPath,
    required this.folderId,
    required this.progressPort,
  });
}

/// Parameters for scanAll isolate
class _ScanAllParams {
  final String dbPath;
  final SendPort progressPort;

  _ScanAllParams({
    required this.dbPath,
    required this.progressPort,
  });
}

/// Parameters for computeChecksums isolate
class _ComputeChecksumsParams {
  final String dbPath;
  final SendPort progressPort;

  _ComputeChecksumsParams({
    required this.dbPath,
    required this.progressPort,
  });
}

/// Parameters for reindexContent isolate
class _ReindexContentParams {
  final String dbPath;
  final SendPort progressPort;

  _ReindexContentParams({
    required this.dbPath,
    required this.progressPort,
  });
}

// Store send ports for callbacks keyed by isolate
final Map<int, SendPort> _isolateSendPorts = {};
int _nextIsolateId = 1;

int _registerSendPort(SendPort port) {
  final id = _nextIsolateId++;
  _isolateSendPorts[id] = port;
  return id;
}

void _unregisterSendPort(int id) {
  _isolateSendPorts.remove(id);
}

SendPort? _getSendPort(int id) {
  return _isolateSendPorts[id];
}

/// Static callback function for scan progress
void _scanProgressCallback(
  int processed,
  int total,
  Pointer<Utf8> currentFile,
  Pointer<Void> userData,
) {
  final portId = userData.address;
  final port = _getSendPort(portId);
  if (port != null) {
    port.send({
      'type': 'progress',
      'processed': processed,
      'total': total,
      'currentFile': currentFile != nullptr ? currentFile.toDartString() : null,
    });
  }
}

/// Static callback function for checksum progress
void _checksumProgressCallback(int processed, int total, Pointer<Void> userData) {
  final portId = userData.address;
  final port = _getSendPort(portId);
  if (port != null) {
    port.send({
      'type': 'progress',
      'processed': processed,
      'total': total,
    });
  }
}

/// Static callback function for content indexing progress
void _contentProgressCallback(int processed, int total, Pointer<Void> userData) {
  final portId = userData.address;
  final port = _getSendPort(portId);
  if (port != null) {
    port.send({
      'type': 'progress',
      'processed': processed,
      'total': total,
    });
  }
}

/// Isolate entry point for scanning a folder
void _scanFolderIsolate(_ScanFolderParams params) {
  final portId = _registerSendPort(params.progressPort);
  
  try {
    final bridge = NativeBridge._forIsolate();
    bridge._initDatabaseSyncForWorker(params.dbPath);
    
    final callback = NativeCallable<ScanCallbackNative>.isolateLocal(
      _scanProgressCallback,
    );
    
    try {
      final userDataPtr = Pointer<Void>.fromAddress(portId);
      
      final error = bridge._fvIndexScanFolder(
        bridge._indexManager!,
        params.folderId,
        callback.nativeFunction,
        userDataPtr,
      );
      
      if (error != 0) {
        params.progressPort.send({
          'type': 'error',
          'message': bridge.getErrorMessage(error),
        });
      } else {
        params.progressPort.send({'type': 'done'});
      }
    } finally {
      callback.close();
      bridge.closeDatabase();
    }
  } catch (e) {
    params.progressPort.send({
      'type': 'error',
      'message': e.toString(),
    });
  } finally {
    _unregisterSendPort(portId);
  }
}

/// Isolate entry point for scanning all folders
void _scanAllIsolate(_ScanAllParams params) {
  final portId = _registerSendPort(params.progressPort);
  
  try {
    final bridge = NativeBridge._forIsolate();
    bridge._initDatabaseSyncForWorker(params.dbPath);
    
    final callback = NativeCallable<ScanCallbackNative>.isolateLocal(
      _scanProgressCallback,
    );
    
    try {
      final userDataPtr = Pointer<Void>.fromAddress(portId);
      
      final error = bridge._fvIndexScanAll(
        bridge._indexManager!,
        callback.nativeFunction,
        userDataPtr,
      );
      
      if (error != 0) {
        params.progressPort.send({
          'type': 'error',
          'message': bridge.getErrorMessage(error),
        });
      } else {
        params.progressPort.send({'type': 'done'});
      }
    } finally {
      callback.close();
      bridge.closeDatabase();
    }
  } catch (e) {
    params.progressPort.send({
      'type': 'error',
      'message': e.toString(),
    });
  } finally {
    _unregisterSendPort(portId);
  }
}

/// Isolate entry point for computing checksums
void _computeChecksumsIsolate(_ComputeChecksumsParams params) {
  final portId = _registerSendPort(params.progressPort);
  
  try {
    final bridge = NativeBridge._forIsolate();
    bridge._initDatabaseSyncForWorker(params.dbPath);
    
    final callback = NativeCallable<ChecksumCallbackNative>.isolateLocal(
      _checksumProgressCallback,
    );
    
    try {
      final userDataPtr = Pointer<Void>.fromAddress(portId);
      
      final error = bridge._fvDuplicatesComputeChecksums(
        bridge._duplicateFinder!,
        callback.nativeFunction,
        userDataPtr,
      );
      
      if (error != 0) {
        params.progressPort.send({
          'type': 'error',
          'message': bridge.getErrorMessage(error),
        });
      } else {
        params.progressPort.send({'type': 'done'});
      }
    } finally {
      callback.close();
      bridge.closeDatabase();
    }
  } catch (e) {
    params.progressPort.send({
      'type': 'error',
      'message': e.toString(),
    });
  } finally {
    _unregisterSendPort(portId);
  }
}

/// Isolate entry point for reindexing content
void _reindexContentIsolate(_ReindexContentParams params) {
  final portId = _registerSendPort(params.progressPort);
  
  try {
    final bridge = NativeBridge._forIsolate();
    bridge._initDatabaseSyncForWorker(params.dbPath);
    
    // Create content indexer in worker
    bridge._contentIndexer = bridge._fvContentIndexerCreate(bridge._database!);
    
    final callback = NativeCallable<ContentProgressCallbackNative>.isolateLocal(
      _contentProgressCallback,
    );
    
    try {
      final userDataPtr = Pointer<Void>.fromAddress(portId);
      
      final error = bridge._fvContentIndexerReindexAll(
        bridge._contentIndexer!,
        callback.nativeFunction,
        userDataPtr,
      );
      
      if (error != 0) {
        params.progressPort.send({
          'type': 'error',
          'message': bridge.getErrorMessage(error),
        });
      } else {
        params.progressPort.send({'type': 'done'});
      }
    } finally {
      callback.close();
      if (bridge._contentIndexer != null) {
        bridge._fvContentIndexerDestroy(bridge._contentIndexer!);
        bridge._contentIndexer = null;
      }
      bridge.closeDatabase();
    }
  } catch (e) {
    params.progressPort.send({
      'type': 'error',
      'message': e.toString(),
    });
  } finally {
    _unregisterSendPort(portId);
  }
}

// ═══════════════════════════════════════════════════════════
// Native function typedefs
// ═══════════════════════════════════════════════════════════

// Callback typedefs
typedef ScanCallbackNative = Void Function(
    Int64 processed, Int64 total, Pointer<Utf8> currentFile, Pointer<Void> userData);
typedef ChecksumCallbackNative = Void Function(
    Int32 processed, Int32 total, Pointer<Void> userData);
typedef ContentProgressCallbackNative = Void Function(
    Int32 processed, Int32 total, Pointer<Void> userData);

// General
typedef _FvVersion = Pointer<Utf8> Function();
typedef _FvErrorMessage = Pointer<Utf8> Function(int error);
typedef _FvLastError = int Function();
typedef _FvLastErrorMessage = Pointer<Utf8> Function();
typedef _FvClearError = void Function();
typedef _FvFreeString = void Function(Pointer<Utf8> str);

// Database
typedef _FvDatabaseOpen = Pointer<Void> Function(Pointer<Utf8> path, Pointer<Int32> outError);
typedef _FvDatabaseClose = int Function(Pointer<Void> db);
typedef _FvDatabaseInitialize = int Function(Pointer<Void> db);
typedef _FvDatabaseIsInitialized = int Function(Pointer<Void> db);

// Index Manager
typedef _FvIndexCreate = Pointer<Void> Function(Pointer<Void> db);
typedef _FvIndexDestroy = void Function(Pointer<Void> mgr);
typedef _FvIndexAddFolder = int Function(
    Pointer<Void> mgr, Pointer<Utf8> path, Pointer<Utf8> name, int visibility);
typedef _FvIndexRemoveFolder = int Function(Pointer<Void> mgr, int folderId);
typedef _FvIndexGetFolders = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvIndexScanFolder = int Function(Pointer<Void> mgr, int folderId,
    Pointer<NativeFunction<ScanCallbackNative>> cb, Pointer<Void> userData);
typedef _FvIndexScanAll = int Function(Pointer<Void> mgr,
    Pointer<NativeFunction<ScanCallbackNative>> cb, Pointer<Void> userData);
typedef _FvIndexStopScan = void Function(Pointer<Void> mgr);
typedef _FvIndexIsScanning = int Function(Pointer<Void> mgr);
typedef _FvIndexGetFile = Pointer<Utf8> Function(Pointer<Void> mgr, int fileId);
typedef _FvIndexDeleteFile = int Function(Pointer<Void> mgr, int fileId, int deleteFromDisk);
typedef _FvIndexGetRecent = Pointer<Utf8> Function(Pointer<Void> mgr, int limit);
typedef _FvIndexGetRecentCompact = Pointer<Utf8> Function(Pointer<Void> mgr, int limit);
typedef _FvIndexGetByFolderCompact = Pointer<Utf8> Function(
    Pointer<Void> mgr, int folderId, int limit, int offset);
typedef _FvIndexGetStats = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvIndexSetFolderVisibility = int Function(
    Pointer<Void> mgr, int folderId, int visibility);
typedef _FvIndexSetFolderEnabled = int Function(
    Pointer<Void> mgr, int folderId, int enabled);
typedef _FvIndexSetFileVisibility = int Function(
    Pointer<Void> mgr, int fileId, int visibility);
typedef _FvIndexOptimizeDatabase = int Function(Pointer<Void> mgr);
typedef _FvIndexGetMaxTextSizeKB = int Function(Pointer<Void> mgr);
typedef _FvIndexSetMaxTextSizeKB = int Function(Pointer<Void> mgr, int sizeKB);

// Search Engine
typedef _FvSearchCreate = Pointer<Void> Function(Pointer<Void> db);
typedef _FvSearchDestroy = void Function(Pointer<Void> engine);
typedef _FvSearchQuery = Pointer<Utf8> Function(Pointer<Void> engine, Pointer<Utf8> queryJson);
typedef _FvSearchQueryCompact = Pointer<Utf8> Function(
    Pointer<Void> engine, Pointer<Utf8> queryJson);
typedef _FvSearchCount = int Function(Pointer<Void> engine, Pointer<Utf8> queryJson);
typedef _FvSearchSuggest = Pointer<Utf8> Function(
    Pointer<Void> engine, Pointer<Utf8> prefix, int limit);

// Tag Manager
typedef _FvTagsCreate = Pointer<Void> Function(Pointer<Void> db);
typedef _FvTagsDestroy = void Function(Pointer<Void> mgr);
typedef _FvTagsAdd = int Function(Pointer<Void> mgr, int fileId, Pointer<Utf8> tag);
typedef _FvTagsRemove = int Function(Pointer<Void> mgr, int fileId, Pointer<Utf8> tag);
typedef _FvTagsGetForFile = Pointer<Utf8> Function(Pointer<Void> mgr, int fileId);
typedef _FvTagsGetAll = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvTagsGetPopular = Pointer<Utf8> Function(Pointer<Void> mgr, int limit);

// Duplicate Finder
typedef _FvDuplicatesCreate = Pointer<Void> Function(Pointer<Void> db, Pointer<Void> indexMgr);
typedef _FvDuplicatesDestroy = void Function(Pointer<Void> finder);
typedef _FvDuplicatesFind = Pointer<Utf8> Function(Pointer<Void> finder);
typedef _FvDuplicatesStats = Pointer<Utf8> Function(Pointer<Void> finder);
typedef _FvDuplicatesWithoutBackup = Pointer<Utf8> Function(Pointer<Void> finder);
typedef _FvDuplicatesDeleteFile = int Function(Pointer<Void> finder, int fileId);
typedef _FvDuplicatesComputeChecksums = int Function(Pointer<Void> finder,
    Pointer<NativeFunction<ChecksumCallbackNative>> cb, Pointer<Void> userData);

// Content Indexer
typedef _FvContentIndexerCreate = Pointer<Void> Function(Pointer<Void> db);
typedef _FvContentIndexerDestroy = void Function(Pointer<Void> indexer);
typedef _FvContentIndexerStart = int Function(Pointer<Void> indexer);
typedef _FvContentIndexerStop = int Function(Pointer<Void> indexer, int wait);
typedef _FvContentIndexerIsRunning = int Function(Pointer<Void> indexer);
typedef _FvContentIndexerProcessFile = int Function(Pointer<Void> indexer, int fileId);
typedef _FvContentIndexerEnqueueUnprocessed = int Function(Pointer<Void> indexer);
typedef _FvContentIndexerGetStatus = Pointer<Utf8> Function(Pointer<Void> indexer);
typedef _FvContentIndexerGetPendingCount = int Function(Pointer<Void> indexer);
typedef _FvContentIndexerReindexAll = int Function(Pointer<Void> indexer,
    Pointer<NativeFunction<ContentProgressCallbackNative>> cb, Pointer<Void> userData);
typedef _FvContentIndexerCanExtract = int Function(Pointer<Void> indexer, Pointer<Utf8> mimeType);
typedef _FvContentIndexerSetMaxTextSizeKB = void Function(Pointer<Void> indexer, int sizeKB);
typedef _FvContentIndexerGetMaxTextSizeKB = int Function(Pointer<Void> indexer);

// Secure Storage
typedef _FvSecureCreate = Pointer<Void> Function();
typedef _FvSecureDestroy = void Function(Pointer<Void> storage);
typedef _FvSecureStoreString = int Function(Pointer<Void> storage, Pointer<Utf8> key, Pointer<Utf8> value);
typedef _FvSecureRetrieveString = Pointer<Utf8> Function(Pointer<Void> storage, Pointer<Utf8> key);
typedef _FvSecureRemove = int Function(Pointer<Void> storage, Pointer<Utf8> key);
typedef _FvSecureExists = int Function(Pointer<Void> storage, Pointer<Utf8> key);

// Family Pairing
typedef _FvPairingCreate = Pointer<Void> Function(Pointer<Void> storage);
typedef _FvPairingDestroy = void Function(Pointer<Void> pairing);
typedef _FvPairingIsConfigured = int Function(Pointer<Void> pairing);
typedef _FvPairingGetDeviceId = Pointer<Utf8> Function(Pointer<Void> pairing);
typedef _FvPairingGetDeviceName = Pointer<Utf8> Function(Pointer<Void> pairing);
typedef _FvPairingSetDeviceName = void Function(Pointer<Void> pairing, Pointer<Utf8> name);
typedef _FvPairingGetDeviceType = int Function(Pointer<Void> pairing);
typedef _FvPairingCreateFamily = Pointer<Utf8> Function(Pointer<Void> pairing);
typedef _FvPairingRegeneratePin = Pointer<Utf8> Function(Pointer<Void> pairing);
typedef _FvPairingCancel = void Function(Pointer<Void> pairing);
typedef _FvPairingHasPending = int Function(Pointer<Void> pairing);
typedef _FvPairingJoinPin = int Function(Pointer<Void> pairing, Pointer<Utf8> pin, Pointer<Utf8> host, int port);
typedef _FvPairingJoinQr = int Function(Pointer<Void> pairing, Pointer<Utf8> qrData);
typedef _FvPairingReset = void Function(Pointer<Void> pairing);
typedef _FvPairingStartServer = int Function(Pointer<Void> pairing, int port);
typedef _FvPairingStopServer = void Function(Pointer<Void> pairing);
typedef _FvPairingIsServerRunning = int Function(Pointer<Void> pairing);
typedef _FvPairingGetServerPort = int Function(Pointer<Void> pairing);

// Network Discovery
typedef _FvDiscoveryCreate = Pointer<Void> Function();
typedef _FvDiscoveryDestroy = void Function(Pointer<Void> discovery);
typedef _FvDiscoveryStart = int Function(Pointer<Void> discovery, Pointer<Void> pairing, Pointer<NativeFunction<Void Function(Int32, Pointer<Utf8>, Pointer<Void>)>> callback, Pointer<Void> userData);
typedef _FvDiscoveryStop = void Function(Pointer<Void> discovery);
typedef _FvDiscoveryIsRunning = int Function(Pointer<Void> discovery);
typedef _FvDiscoveryGetDevices = Pointer<Utf8> Function(Pointer<Void> discovery);
typedef _FvDiscoveryGetDeviceCount = int Function(Pointer<Void> discovery);
typedef _FvDiscoveryGetDevice = Pointer<Utf8> Function(Pointer<Void> discovery, Pointer<Utf8> deviceId);
typedef _FvDiscoveryGetLocalIps = Pointer<Utf8> Function();

// Network Manager
typedef _FvNetworkCreate = Pointer<Void> Function(Pointer<Void> pairing);
typedef _FvNetworkDestroy = void Function(Pointer<Void> mgr);
typedef _FvNetworkStart = int Function(Pointer<Void> mgr, int port, Pointer<NativeFunction<Void Function(Int32, Pointer<Utf8>, Pointer<Void>)>> callback, Pointer<Void> userData);
typedef _FvNetworkStop = void Function(Pointer<Void> mgr);
typedef _FvNetworkGetState = int Function(Pointer<Void> mgr);
typedef _FvNetworkIsRunning = int Function(Pointer<Void> mgr);
typedef _FvNetworkGetPort = int Function(Pointer<Void> mgr);
typedef _FvNetworkGetDiscoveredDevices = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvNetworkGetConnectedDevices = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvNetworkConnectToDevice = int Function(Pointer<Void> mgr, Pointer<Utf8> deviceId);
typedef _FvNetworkConnectToAddress = int Function(Pointer<Void> mgr, Pointer<Utf8> host, int port);
typedef _FvNetworkDisconnectDevice = void Function(Pointer<Void> mgr, Pointer<Utf8> deviceId);
typedef _FvNetworkDisconnectAll = void Function(Pointer<Void> mgr);
typedef _FvNetworkIsConnectedTo = int Function(Pointer<Void> mgr, Pointer<Utf8> deviceId);
typedef _FvNetworkGetLastError = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvNetworkSetDatabase = int Function(Pointer<Void> mgr, Pointer<Void> db, Pointer<Utf8> deviceId);
typedef _FvNetworkRequestSync = int Function(Pointer<Void> mgr, Pointer<Utf8> deviceId, int fullSync);
typedef _FvNetworkGetRemoteFiles = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvNetworkSearchRemoteFiles = Pointer<Utf8> Function(Pointer<Void> mgr, Pointer<Utf8> query, int limit);
typedef _FvNetworkGetRemoteFileCount = int Function(Pointer<Void> mgr);

// File Transfer
typedef _FvNetworkSetCacheDir = int Function(Pointer<Void> mgr, Pointer<Utf8> cacheDir);
typedef _FvNetworkRequestFile = Pointer<Utf8> Function(
    Pointer<Void> mgr, Pointer<Utf8> deviceId, int fileId, 
    Pointer<Utf8> fileName, int expectedSize, Pointer<Utf8> checksum);
typedef _FvNetworkCancelFileRequest = void Function(Pointer<Void> mgr, Pointer<Utf8> requestId);
typedef _FvNetworkCancelAllFileRequests = void Function(Pointer<Void> mgr, Pointer<Utf8> deviceId);
typedef _FvNetworkGetActiveTransfers = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvNetworkGetTransferProgress = Pointer<Utf8> Function(Pointer<Void> mgr, Pointer<Utf8> requestId);
typedef _FvNetworkIsFileCached = int Function(Pointer<Void> mgr, Pointer<Utf8> deviceId, int fileId, Pointer<Utf8> checksum);
typedef _FvNetworkGetCachedPath = Pointer<Utf8> Function(Pointer<Void> mgr, Pointer<Utf8> deviceId, int fileId);
typedef _FvNetworkClearCache = void Function(Pointer<Void> mgr);
typedef _FvNetworkGetCacheSize = int Function(Pointer<Void> mgr);
typedef _FvNetworkHasActiveTransfers = int Function(Pointer<Void> mgr);

// Cloud Accounts
typedef _FvCloudAccountAdd = int Function(
    Pointer<Void> db,
    Pointer<Utf8> type,
    Pointer<Utf8> email,
    Pointer<Utf8> displayName,
    Pointer<Utf8> avatarUrl);
typedef _FvCloudAccountList = Pointer<Utf8> Function(Pointer<Void> db);
typedef _FvCloudAccountRemove = bool Function(Pointer<Void> db, int accountId);
typedef _FvCloudAccountSetEnabled = bool Function(Pointer<Void> db, int accountId, bool enabled);
typedef _FvCloudAccountUpdateSync = bool Function(
    Pointer<Void> db,
    int accountId,
    int fileCount,
    Pointer<Utf8> changeToken);

typedef _FvCloudFolderAdd = int Function(
    Pointer<Void> db,
    int accountId,
    Pointer<Utf8> cloudId,
    Pointer<Utf8> name,
    Pointer<Utf8> path);
typedef _FvCloudFolderRemove = bool Function(Pointer<Void> db, int folderId);
typedef _FvCloudFolderList = Pointer<Utf8> Function(Pointer<Void> db, int accountId);
typedef _FvCloudFolderSetEnabled = bool Function(Pointer<Void> db, int folderId, bool enabled);

typedef _FvCloudFileUpsert = bool Function(
    Pointer<Void> db,
    int accountId,
    Pointer<Utf8> fileJson);
typedef _FvCloudFileRemove = bool Function(
    Pointer<Void> db,
    int accountId,
    Pointer<Utf8> cloudId);
typedef _FvCloudFileRemoveAll = bool Function(Pointer<Void> db, int accountId);

// Index Sync Manager
typedef _FvSyncCreate = Pointer<Void> Function(Pointer<Void> db, Pointer<Utf8> deviceId);
typedef _FvSyncDestroy = void Function(Pointer<Void> mgr);
typedef _FvSyncGetRemoteFiles = Pointer<Utf8> Function(Pointer<Void> mgr);
typedef _FvSyncGetRemoteFilesFrom = Pointer<Utf8> Function(Pointer<Void> mgr, Pointer<Utf8> deviceId);
typedef _FvSyncSearchRemote = Pointer<Utf8> Function(Pointer<Void> mgr, Pointer<Utf8> query, int limit);
typedef _FvSyncGetRemoteCount = int Function(Pointer<Void> mgr);
typedef _FvSyncIsSyncing = int Function(Pointer<Void> mgr);
