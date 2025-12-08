import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:file_picker/file_picker.dart';
import 'package:permission_handler/permission_handler.dart';

import '../../core/models/models.dart';
import '../../core/providers/providers.dart';
import '../../shared/widgets/widgets.dart';
import '../../shared/utils/formatters.dart';
import '../../shared/utils/file_icons.dart';

// Alias to avoid conflict with Flutter's Visibility widget
typedef FVVisibility = FileVisibility;

/// Экран управления папками
class FoldersScreen extends ConsumerWidget {
  const FoldersScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final folders = ref.watch(foldersNotifierProvider);
    final isScanning = ref.watch(isScanningProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Folders'),
        actions: [
          if (isScanning)
            const Padding(
              padding: EdgeInsets.only(right: 16),
              child: SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              ),
            ),
          IconButton(
            icon: const Icon(Icons.refresh),
            tooltip: 'Scan All',
            onPressed: isScanning
                ? null
                : () => ref.read(foldersNotifierProvider.notifier).scanAll(),
          ),
        ],
      ),
      body: folders.when(
        data: (list) {
          if (list.isEmpty) {
            return EmptyState.noFolders(
              onAddFolder: () => _addFolder(context, ref),
            );
          }

          return ListView.builder(
            padding: const EdgeInsets.only(bottom: 80),
            itemCount: list.length,
            itemBuilder: (context, index) {
              final folder = list[index];
              final scanningId = ref.watch(scanningFolderIdProvider);
              final isThisScanning = scanningId == folder.id;
              
              return _FolderTile(
                folder: folder,
                isScanning: isThisScanning,
                onScan: () async {
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text('Scanning ${folder.name}...'),
                      duration: const Duration(seconds: 1),
                    ),
                  );
                  await ref.read(foldersNotifierProvider.notifier).scanFolder(folder.id);
                  if (context.mounted) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      SnackBar(
                        content: Text('${folder.name} scan complete'),
                        backgroundColor: Colors.green,
                      ),
                    );
                  }
                },
                onRemove: () => _confirmRemove(context, ref, folder),
                onVisibilityChanged: (v) =>
                    ref.read(foldersNotifierProvider.notifier).setVisibility(folder.id, v),
              );
            },
          );
        },
        loading: () => const LoadingIndicator(message: 'Loading folders...'),
        error: (e, _) => AppErrorWidget(
          message: 'Failed to load folders',
          details: e.toString(),
          onRetry: () => ref.invalidate(foldersNotifierProvider),
        ),
      ),
      floatingActionButton: FloatingActionButton.extended(
        onPressed: () => _addFolder(context, ref),
        icon: const Icon(Icons.add),
        label: const Text('Add Folder'),
      ),
    );
  }

  Future<void> _addFolder(BuildContext context, WidgetRef ref) async {
    // Android: запросить права доступа
    if (Platform.isAndroid) {
      final status = await Permission.manageExternalStorage.request();
      if (!status.isGranted) {
        if (context.mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: const Text('Storage permission required'),
              action: SnackBarAction(
                label: 'Settings',
                onPressed: () => openAppSettings(),
              ),
            ),
          );
        }
        return;
      }
    }
    
    // Выбор папки через file picker
    final result = await FilePicker.platform.getDirectoryPath();
    if (result != null) {
      final folderName = result.split(RegExp(r'[/\\]')).last;
      
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Adding $folderName...')),
        );
      }
      
      await ref.read(foldersNotifierProvider.notifier).addFolder(result);
      
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('$folderName added and scanned!'),
            backgroundColor: Colors.green,
          ),
        );
      }
    }
  }

  Future<void> _confirmRemove(BuildContext context, WidgetRef ref, WatchedFolder folder) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Remove Folder'),
        content: Text(
          'Remove "${folder.name}" from FamilyVault?\n\n'
          'This will remove the folder from indexing. Files will not be deleted.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            style: FilledButton.styleFrom(
              backgroundColor: Theme.of(context).colorScheme.error,
            ),
            child: const Text('Remove'),
          ),
        ],
      ),
    );

    if (confirmed == true) {
      await ref.read(foldersNotifierProvider.notifier).removeFolder(folder.id);
    }
  }
}

class _FolderTile extends StatelessWidget {
  final WatchedFolder folder;
  final bool isScanning;
  final VoidCallback onScan;
  final VoidCallback onRemove;
  final ValueChanged<FVVisibility> onVisibilityChanged;

  const _FolderTile({
    required this.folder,
    this.isScanning = false,
    required this.onScan,
    required this.onRemove,
    required this.onVisibilityChanged,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: InkWell(
        onTap: () {}, // TODO: Show folder files
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  // Folder icon with visibility badge or scanning indicator
                  Stack(
                    children: [
                      if (isScanning)
                        const SizedBox(
                          width: 40,
                          height: 40,
                          child: CircularProgressIndicator(strokeWidth: 3),
                        )
                      else
                        Icon(
                          Icons.folder,
                          size: 40,
                          color: colorScheme.primary,
                        ),
                      if (!isScanning)
                        Positioned(
                          right: -2,
                          bottom: -2,
                          child: Container(
                            padding: const EdgeInsets.all(2),
                            decoration: BoxDecoration(
                              color: colorScheme.surface,
                              shape: BoxShape.circle,
                            ),
                            child: Icon(
                              getVisibilityIcon(folder.defaultVisibility),
                              size: 14,
                              color: getVisibilityColor(folder.defaultVisibility),
                            ),
                          ),
                        ),
                    ],
                  ),
                  const SizedBox(width: 16),
                  
                  // Info
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          folder.name,
                          style: theme.textTheme.titleMedium?.copyWith(
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        const SizedBox(height: 4),
                        Text(
                          folder.path,
                          style: theme.textTheme.bodySmall?.copyWith(
                            color: colorScheme.outline,
                          ),
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                        ),
                      ],
                    ),
                  ),
                  
                  // Menu
                  PopupMenuButton<String>(
                    enabled: !isScanning,
                    itemBuilder: (context) => [
                      PopupMenuItem(
                        value: 'scan',
                        enabled: !isScanning,
                        child: ListTile(
                          leading: Icon(isScanning ? Icons.hourglass_empty : Icons.refresh),
                          title: Text(isScanning ? 'Scanning...' : 'Rescan'),
                          contentPadding: EdgeInsets.zero,
                        ),
                      ),
                      PopupMenuItem(
                        value: 'visibility',
                        child: ListTile(
                          leading: Icon(
                            folder.defaultVisibility == FVVisibility.private
                                ? Icons.family_restroom
                                : Icons.lock,
                          ),
                          title: Text(
                            folder.defaultVisibility == FVVisibility.private
                                ? 'Make Family'
                                : 'Make Private',
                          ),
                          contentPadding: EdgeInsets.zero,
                        ),
                      ),
                      const PopupMenuItem(
                        value: 'remove',
                        child: ListTile(
                          leading: Icon(Icons.delete, color: Colors.red),
                          title: Text('Remove', style: TextStyle(color: Colors.red)),
                          contentPadding: EdgeInsets.zero,
                        ),
                      ),
                    ],
                    onSelected: (value) {
                      switch (value) {
                        case 'scan':
                          onScan();
                          break;
                        case 'visibility':
                          onVisibilityChanged(
                            folder.defaultVisibility == FVVisibility.private
                                ? FVVisibility.family
                                : FVVisibility.private,
                          );
                          break;
                        case 'remove':
                          onRemove();
                          break;
                      }
                    },
                  ),
                ],
              ),
              
              const Divider(height: 24),
              
              // Stats
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceAround,
                children: [
                  _StatChip(
                    icon: Icons.insert_drive_file,
                    label: formatFileCount(folder.fileCount),
                  ),
                  _StatChip(
                    icon: Icons.storage,
                    label: formatFileSize(folder.totalSize),
                  ),
                  _StatChip(
                    icon: Icons.schedule,
                    label: folder.lastScanAt != null
                        ? formatDate(folder.lastScanAt!)
                        : 'Never scanned',
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _StatChip extends StatelessWidget {
  final IconData icon;
  final String label;

  const _StatChip({
    required this.icon,
    required this.label,
  });

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;

    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: 16, color: colorScheme.outline),
        const SizedBox(width: 4),
        Text(
          label,
          style: Theme.of(context).textTheme.bodySmall?.copyWith(
                color: colorScheme.outline,
              ),
        ),
      ],
    );
  }
}

