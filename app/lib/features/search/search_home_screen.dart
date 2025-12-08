import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import 'package:file_picker/file_picker.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:open_filex/open_filex.dart';

import '../../core/models/models.dart';
import '../../core/providers/providers.dart';
import '../../shared/utils/formatters.dart';
import '../../shared/utils/file_icons.dart';
import '../../shared/widgets/index_status_bar.dart';
import '../../shared/widgets/file_details_panel.dart';
import '../../shared/widgets/file_thumbnail.dart';
import '../../theme/app_theme.dart';

/// Провайдер для выбранного файла (для панели деталей)
final selectedFileIdProvider = StateProvider<int?>((ref) => null);

/// Главный экран приложения — поиск с фильтрами
class SearchHomeScreen extends ConsumerStatefulWidget {
  const SearchHomeScreen({super.key});

  @override
  ConsumerState<SearchHomeScreen> createState() => _SearchHomeScreenState();
}

class _SearchHomeScreenState extends ConsumerState<SearchHomeScreen> {
  final _searchController = TextEditingController();
  final _focusNode = FocusNode();

  @override
  void dispose() {
    _searchController.dispose();
    _focusNode.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final query = ref.watch(searchQueryProvider);
    final results = ref.watch(searchResultsProvider);
    final stats = ref.watch(indexStatsProvider);
    final selectedFileId = ref.watch(selectedFileIdProvider);
    final isDesktop = AppSizes.isDesktop(context);

    return Scaffold(
      drawer: const _AppDrawer(),
      body: SafeArea(
        bottom: false,
        child: Column(
          children: [
            // Header with search
            _SearchHeader(
              controller: _searchController,
              focusNode: _focusNode,
              onChanged: (text) {
                ref.read(searchQueryProvider.notifier).state =
                    query.copyWith(text: text);
              },
              onClear: () {
                _searchController.clear();
                ref.read(searchQueryProvider.notifier).state =
                    const SearchQuery();
              },
            ),

            // Filter chips
            _FilterChips(
              selectedType: query.contentType,
              onTypeSelected: (type) {
                if (query.contentType == type) {
                  // Deselect
                  ref.read(searchQueryProvider.notifier).state =
                      query.copyWith(clearContentType: true);
                } else {
                  ref.read(searchQueryProvider.notifier).state =
                      query.copyWith(contentType: type);
                }
              },
            ),

            // Main content area with optional side panel
            Expanded(
              child: Row(
                children: [
                  // Results area
                  Expanded(
                    child: results.when(
                      data: (files) {
                        final hasFolders = stats.valueOrNull?.totalFolders ?? 0;
                        if (files.isEmpty && hasFolders == 0) {
                          return _EmptyWelcome(
                            onAddFolder: () => _addFolder(context, ref),
                          );
                        }
                        if (files.isEmpty) {
                          return _EmptyResults(hasQuery: !query.isEmpty);
                        }
                        return _ResultsGrid(
                          files: files,
                          selectedFileId: selectedFileId,
                          onFileSelected: (file) {
                            if (isDesktop) {
                              // На desktop показываем панель сбоку
                              ref.read(selectedFileIdProvider.notifier).state =
                                  file.id;
                            } else {
                              // На mobile показываем bottom sheet
                              showFileDetailsSheet(context, file.id);
                            }
                          },
                        );
                      },
                      loading: () => const Center(
                        child: CircularProgressIndicator(),
                      ),
                      error: (e, _) => Center(
                        child: Text('Error: $e'),
                      ),
                    ),
                  ),

                  // Details panel (desktop only)
                  if (isDesktop && selectedFileId != null)
                    SizedBox(
                      width: 340,
                      child: FileDetailsPanel(
                        fileId: selectedFileId,
                        onClose: () {
                          ref.read(selectedFileIdProvider.notifier).state =
                              null;
                        },
                      ),
                    ),
                ],
              ),
            ),

            // Status bar
            const IndexStatusBar(),
          ],
        ),
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

    final result = await FilePicker.platform.getDirectoryPath();
    if (result != null) {
      final folderName = result.split(RegExp(r'[/\\]')).last;

      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Row(
              children: [
                const SizedBox(
                  width: 16,
                  height: 16,
                  child: CircularProgressIndicator(
                    strokeWidth: 2,
                    color: Colors.white,
                  ),
                ),
                const SizedBox(width: 12),
                Text('Adding $folderName...'),
              ],
            ),
            duration: const Duration(seconds: 10),
          ),
        );
      }

      await ref.read(foldersNotifierProvider.notifier).addFolder(result);

      if (context.mounted) {
        ScaffoldMessenger.of(context).hideCurrentSnackBar();
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Row(
              children: [
                const Icon(Icons.check_circle, color: Colors.white),
                const SizedBox(width: 12),
                Text('$folderName added successfully!'),
              ],
            ),
            backgroundColor: Colors.green.shade600,
            duration: const Duration(seconds: 2),
          ),
        );
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════
// Search Header
// ═══════════════════════════════════════════════════════════

class _SearchHeader extends StatelessWidget {
  final TextEditingController controller;
  final FocusNode focusNode;
  final ValueChanged<String> onChanged;
  final VoidCallback onClear;

  const _SearchHeader({
    required this.controller,
    required this.focusNode,
    required this.onChanged,
    required this.onClear,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Padding(
      padding: const EdgeInsets.fromLTRB(8, 8, 16, 8),
      child: Row(
        children: [
          // Menu button
          Builder(
            builder: (context) => IconButton(
              icon: const Icon(Icons.menu),
              onPressed: () => Scaffold.of(context).openDrawer(),
              tooltip: 'Menu',
            ),
          ),

          // Search field
          Expanded(
            child: Container(
              height: 48,
              decoration: BoxDecoration(
                color: colorScheme.surfaceContainerHighest,
                borderRadius: BorderRadius.circular(24),
              ),
              child: TextField(
                controller: controller,
                focusNode: focusNode,
                onChanged: onChanged,
                decoration: InputDecoration(
                  hintText: 'Search files...',
                  prefixIcon: const Icon(Icons.search),
                  suffixIcon: controller.text.isNotEmpty
                      ? IconButton(
                          icon: const Icon(Icons.clear),
                          onPressed: onClear,
                        )
                      : null,
                  border: InputBorder.none,
                  contentPadding: const EdgeInsets.symmetric(
                    horizontal: 16,
                    vertical: 12,
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

// ═══════════════════════════════════════════════════════════
// Filter Chips
// ═══════════════════════════════════════════════════════════

class _FilterChips extends StatelessWidget {
  final ContentType? selectedType;
  final ValueChanged<ContentType> onTypeSelected;

  const _FilterChips({
    required this.selectedType,
    required this.onTypeSelected,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 48,
      child: ListView(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: 16),
        children: [
          _FilterChip(
            icon: Icons.image,
            label: 'Images',
            selected: selectedType == ContentType.image,
            onTap: () => onTypeSelected(ContentType.image),
            color: Colors.pink,
          ),
          const SizedBox(width: 8),
          _FilterChip(
            icon: Icons.videocam,
            label: 'Videos',
            selected: selectedType == ContentType.video,
            onTap: () => onTypeSelected(ContentType.video),
            color: Colors.red,
          ),
          const SizedBox(width: 8),
          _FilterChip(
            icon: Icons.audio_file,
            label: 'Audio',
            selected: selectedType == ContentType.audio,
            onTap: () => onTypeSelected(ContentType.audio),
            color: Colors.orange,
          ),
          const SizedBox(width: 8),
          _FilterChip(
            icon: Icons.description,
            label: 'Documents',
            selected: selectedType == ContentType.document,
            onTap: () => onTypeSelected(ContentType.document),
            color: Colors.blue,
          ),
          const SizedBox(width: 8),
          _FilterChip(
            icon: Icons.archive,
            label: 'Archives',
            selected: selectedType == ContentType.archive,
            onTap: () => onTypeSelected(ContentType.archive),
            color: Colors.brown,
          ),
        ],
      ),
    );
  }
}

class _FilterChip extends StatelessWidget {
  final IconData icon;
  final String label;
  final bool selected;
  final VoidCallback onTap;
  final Color color;

  const _FilterChip({
    required this.icon,
    required this.label,
    required this.selected,
    required this.onTap,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return FilterChip(
      avatar: Icon(
        icon,
        size: 18,
        color: selected ? Colors.white : color,
      ),
      label: Text(label),
      selected: selected,
      onSelected: (_) => onTap(),
      selectedColor: color,
      checkmarkColor: Colors.white,
      labelStyle: TextStyle(
        color: selected ? Colors.white : null,
        fontWeight: selected ? FontWeight.bold : null,
      ),
    );
  }
}

// ═══════════════════════════════════════════════════════════
// Empty Welcome (no folders yet)
// ═══════════════════════════════════════════════════════════

class _EmptyWelcome extends StatelessWidget {
  final VoidCallback onAddFolder;

  const _EmptyWelcome({required this.onAddFolder});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            // Animated icon
            TweenAnimationBuilder<double>(
              tween: Tween(begin: 0.8, end: 1.0),
              duration: const Duration(milliseconds: 800),
              curve: Curves.easeOutBack,
              builder: (context, value, child) {
                return Transform.scale(
                  scale: value,
                  child: child,
                );
              },
              child: Container(
                padding: const EdgeInsets.all(24),
                decoration: BoxDecoration(
                  color: colorScheme.primaryContainer.withValues(alpha: 0.5),
                  shape: BoxShape.circle,
                ),
                child: Icon(
                  Icons.folder_special,
                  size: 72,
                  color: colorScheme.primary,
                ),
              ),
            ),
            const SizedBox(height: 32),
            Text(
              'Welcome to FamilyVault',
              style: theme.textTheme.headlineSmall?.copyWith(
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 12),
            Text(
              'Add your first folder to start organizing\nand searching your files',
              style: theme.textTheme.bodyLarge?.copyWith(
                color: colorScheme.outline,
              ),
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 32),
            FilledButton.icon(
              onPressed: onAddFolder,
              icon: const Icon(Icons.create_new_folder),
              label: const Text('Add Your First Folder'),
              style: FilledButton.styleFrom(
                padding: const EdgeInsets.symmetric(
                  horizontal: 24,
                  vertical: 16,
                ),
              ),
            ),
            const SizedBox(height: 16),
            TextButton(
              onPressed: () {
                Scaffold.of(context).openDrawer();
              },
              child: Text(
                'Or explore the menu',
                style: TextStyle(color: colorScheme.outline),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ═══════════════════════════════════════════════════════════
// Empty Results
// ═══════════════════════════════════════════════════════════

class _EmptyResults extends StatelessWidget {
  final bool hasQuery;

  const _EmptyResults({required this.hasQuery});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            hasQuery ? Icons.search_off : Icons.folder_open,
            size: 64,
            color: colorScheme.outline,
          ),
          const SizedBox(height: 16),
          Text(
            hasQuery ? 'No results found' : 'No files yet',
            style: theme.textTheme.titleMedium?.copyWith(
              color: colorScheme.outline,
            ),
          ),
          const SizedBox(height: 8),
          Text(
            hasQuery
                ? 'Try different search terms or filters'
                : 'Files will appear here after scanning',
            style: theme.textTheme.bodyMedium?.copyWith(
              color: colorScheme.outline,
            ),
          ),
        ],
      ),
    );
  }
}

// ═══════════════════════════════════════════════════════════
// Results Grid
// ═══════════════════════════════════════════════════════════

class _ResultsGrid extends ConsumerWidget {
  final List<FileRecordCompact> files;
  final int? selectedFileId;
  final ValueChanged<FileRecordCompact> onFileSelected;

  const _ResultsGrid({
    required this.files,
    required this.selectedFileId,
    required this.onFileSelected,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final width = MediaQuery.of(context).size.width;
    final crossAxisCount =
        width > 1200 ? 6 : width > 800 ? 4 : width > 600 ? 3 : 2;

    return GridView.builder(
      padding: const EdgeInsets.all(16),
      gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: crossAxisCount,
        mainAxisSpacing: 12,
        crossAxisSpacing: 12,
        childAspectRatio: 0.85,
      ),
      itemCount: files.length,
      itemBuilder: (context, index) {
        final file = files[index];
        return _FileCard(
          file: file,
          isSelected: file.id == selectedFileId,
          onTap: () => onFileSelected(file),
          onDoubleTap: () => _openFile(context, ref, file.id),
        );
      },
    );
  }

  Future<void> _openFile(BuildContext context, WidgetRef ref, int fileId) async {
    final fileDetails = await ref.read(fileDetailsProvider(fileId).future);
    if (fileDetails == null) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('File not found')),
        );
      }
      return;
    }

    final folders = await ref.read(foldersNotifierProvider.future);
    final folder = folders.where((f) => f.id == fileDetails.folderId).firstOrNull;

    if (folder == null) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Folder not found')),
        );
      }
      return;
    }

    final path = '${folder.path}/${fileDetails.relativePath}';
    final fullPath = Platform.isWindows ? path.replaceAll('/', '\\') : path;

    try {
      if (Platform.isAndroid || Platform.isIOS) {
        final result = await OpenFilex.open(fullPath);
        if (result.type != ResultType.done && context.mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('Cannot open: ${result.message}')),
          );
        }
      } else if (Platform.isWindows) {
        await Process.run('cmd', ['/c', 'start', '', fullPath]);
      } else if (Platform.isMacOS) {
        await Process.run('open', [fullPath]);
      } else if (Platform.isLinux) {
        await Process.run('xdg-open', [fullPath]);
      }
    } catch (e) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed to open: $e')),
        );
      }
    }
  }
}

class _FileCard extends StatelessWidget {
  final FileRecordCompact file;
  final bool isSelected;
  final VoidCallback onTap;
  final VoidCallback onDoubleTap;

  const _FileCard({
    required this.file,
    this.isSelected = false,
    required this.onTap,
    required this.onDoubleTap,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Card(
      clipBehavior: Clip.antiAlias,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: isSelected
            ? BorderSide(color: colorScheme.primary, width: 2)
            : BorderSide.none,
      ),
      elevation: isSelected ? 4 : 0,
      child: InkWell(
        onTap: onTap,
        onDoubleTap: onDoubleTap,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // Thumbnail area with image preview
            Expanded(
              child: Stack(
                fit: StackFit.expand,
                children: [
                  FileThumbnail(file: file),
                  // Selection overlay
                  if (isSelected)
                    Container(
                      color: colorScheme.primary.withValues(alpha: 0.1),
                    ),
                ],
              ),
            ),
            // Info
            Padding(
              padding: const EdgeInsets.all(8),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    file.name,
                    style: theme.textTheme.bodySmall?.copyWith(
                      fontWeight: FontWeight.w500,
                      color: isSelected ? colorScheme.primary : null,
                    ),
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis,
                  ),
                  const SizedBox(height: 4),
                  Text(
                    formatFileSize(file.size),
                    style: theme.textTheme.labelSmall?.copyWith(
                      color: colorScheme.outline,
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ═══════════════════════════════════════════════════════════
// App Drawer
// ═══════════════════════════════════════════════════════════

class _AppDrawer extends ConsumerWidget {
  const _AppDrawer();

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;
    final stats = ref.watch(indexStatsProvider);

    return Drawer(
      child: SafeArea(
        child: Column(
          children: [
            // Header
            Container(
              padding: const EdgeInsets.all(16),
              child: Row(
                children: [
                  Icon(
                    Icons.folder_special,
                    size: 32,
                    color: colorScheme.primary,
                  ),
                  const SizedBox(width: 12),
                  Text(
                    'FamilyVault',
                    style: theme.textTheme.titleLarge?.copyWith(
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                ],
              ),
            ),
            const Divider(),

            // Menu items
            ListTile(
              leading: const Icon(Icons.folder),
              title: const Text('Manage Folders'),
              subtitle: stats.whenOrNull(
                data: (s) => Text('${s.totalFolders} folders'),
              ),
              onTap: () {
                Navigator.pop(context);
                context.push('/folders');
              },
            ),
            ListTile(
              leading: const Icon(Icons.content_copy),
              title: const Text('Find Duplicates'),
              onTap: () {
                Navigator.pop(context);
                context.push('/duplicates');
              },
            ),
            ListTile(
              leading: const Icon(Icons.label),
              title: const Text('Manage Tags'),
              onTap: () {
                Navigator.pop(context);
                context.push('/tags');
              },
            ),

            const Spacer(),
            const Divider(),

            ListTile(
              leading: const Icon(Icons.settings),
              title: const Text('Settings'),
              onTap: () {
                Navigator.pop(context);
                context.push('/settings');
              },
            ),
            const SizedBox(height: 8),
          ],
        ),
      ),
    );
  }
}
