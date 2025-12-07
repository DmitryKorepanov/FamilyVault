import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/models/models.dart';
import '../../core/providers/providers.dart';
import '../../shared/utils/formatters.dart';
import '../../shared/utils/file_icons.dart';

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

    return Scaffold(
      drawer: const _AppDrawer(),
      body: SafeArea(
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
                ref.read(searchQueryProvider.notifier).state = const SearchQuery();
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

            // Stats bar
            stats.when(
              data: (s) => _StatsBar(stats: s),
              loading: () => const SizedBox(height: 32),
              error: (_, __) => const SizedBox(height: 32),
            ),

            // Results
            Expanded(
              child: results.when(
                data: (files) {
                  if (files.isEmpty) {
                    return _EmptyResults(hasQuery: !query.isEmpty);
                  }
                  return _ResultsGrid(files: files);
                },
                loading: () => const Center(
                  child: CircularProgressIndicator(),
                ),
                error: (e, _) => Center(
                  child: Text('Error: $e'),
                ),
              ),
            ),
          ],
        ),
      ),
    );
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
// Stats Bar
// ═══════════════════════════════════════════════════════════

class _StatsBar extends StatelessWidget {
  final IndexStats stats;

  const _StatsBar({required this.stats});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Row(
        children: [
          Icon(
            Icons.folder,
            size: 16,
            color: colorScheme.outline,
          ),
          const SizedBox(width: 4),
          Text(
            '${stats.totalFolders} folders',
            style: theme.textTheme.bodySmall?.copyWith(
              color: colorScheme.outline,
            ),
          ),
          const SizedBox(width: 16),
          Icon(
            Icons.insert_drive_file,
            size: 16,
            color: colorScheme.outline,
          ),
          const SizedBox(width: 4),
          Text(
            '${stats.totalFiles} files',
            style: theme.textTheme.bodySmall?.copyWith(
              color: colorScheme.outline,
            ),
          ),
          const SizedBox(width: 16),
          Icon(
            Icons.storage,
            size: 16,
            color: colorScheme.outline,
          ),
          const SizedBox(width: 4),
          Text(
            formatFileSize(stats.totalSize),
            style: theme.textTheme.bodySmall?.copyWith(
              color: colorScheme.outline,
            ),
          ),
        ],
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
                : 'Add folders from the menu to start',
            style: theme.textTheme.bodyMedium?.copyWith(
              color: colorScheme.outline,
            ),
          ),
          if (!hasQuery) ...[
            const SizedBox(height: 24),
            Builder(
              builder: (context) => FilledButton.icon(
                onPressed: () => Scaffold.of(context).openDrawer(),
                icon: const Icon(Icons.add),
                label: const Text('Add Folders'),
              ),
            ),
          ],
        ],
      ),
    );
  }
}

// ═══════════════════════════════════════════════════════════
// Results Grid
// ═══════════════════════════════════════════════════════════

class _ResultsGrid extends StatelessWidget {
  final List<FileRecordCompact> files;

  const _ResultsGrid({required this.files});

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.of(context).size.width;
    final crossAxisCount = width > 1200 ? 6 : width > 800 ? 4 : width > 600 ? 3 : 2;

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
        return _FileCard(file: file);
      },
    );
  }
}

class _FileCard extends StatelessWidget {
  final FileRecordCompact file;

  const _FileCard({required this.file});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colorScheme = theme.colorScheme;

    return Card(
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: () => context.push('/file/${file.id}'),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // Thumbnail area
            Expanded(
              child: Container(
                color: colorScheme.surfaceContainerHighest,
                child: Center(
                  child: Icon(
                    getContentTypeIcon(file.contentType),
                    size: 48,
                    color: getContentTypeColor(file.contentType),
                  ),
                ),
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

