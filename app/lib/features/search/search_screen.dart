import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../core/models/models.dart';
import '../../core/providers/providers.dart';
import '../../shared/widgets/widgets.dart';
import '../../shared/utils/file_icons.dart';

/// Экран поиска с фильтрами
class SearchScreen extends ConsumerStatefulWidget {
  final String? initialQuery;
  final String? initialType;

  const SearchScreen({
    super.key,
    this.initialQuery,
    this.initialType,
  });

  @override
  ConsumerState<SearchScreen> createState() => _SearchScreenState();
}

class _SearchScreenState extends ConsumerState<SearchScreen> {
  final _searchController = TextEditingController();
  final _focusNode = FocusNode();

  @override
  void initState() {
    super.initState();
    
    // Применяем начальные параметры
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (widget.initialQuery != null) {
        _searchController.text = widget.initialQuery!;
        ref.read(searchQueryProvider.notifier).state = SearchQuery(text: widget.initialQuery!);
      }
      
      if (widget.initialType != null) {
        final type = _parseContentType(widget.initialType!);
        if (type != null) {
          final current = ref.read(searchQueryProvider);
          ref.read(searchQueryProvider.notifier).state = current.copyWith(contentType: type);
        }
      }
    });
  }

  ContentType? _parseContentType(String type) {
    switch (type.toLowerCase()) {
      case 'image':
        return ContentType.image;
      case 'video':
        return ContentType.video;
      case 'audio':
        return ContentType.audio;
      case 'document':
        return ContentType.document;
      case 'archive':
        return ContentType.archive;
      default:
        return null;
    }
  }

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
    final viewMode = ref.watch(viewModeProvider);

    return Scaffold(
      appBar: AppBar(
        title: TextField(
          controller: _searchController,
          focusNode: _focusNode,
          autofocus: widget.initialQuery == null,
          decoration: InputDecoration(
            hintText: 'Search files...',
            border: InputBorder.none,
            filled: false,
            suffixIcon: _searchController.text.isNotEmpty
                ? IconButton(
                    icon: const Icon(Icons.clear),
                    onPressed: () {
                      _searchController.clear();
                      ref.read(searchQueryProvider.notifier).state = const SearchQuery();
                    },
                  )
                : null,
          ),
          onChanged: (value) {
            ref.read(searchQueryProvider.notifier).state = query.copyWith(text: value);
          },
        ),
        actions: [
          IconButton(
            icon: Icon(viewMode == ViewMode.grid ? Icons.list : Icons.grid_view),
            tooltip: viewMode == ViewMode.grid ? 'List view' : 'Grid view',
            onPressed: () {
              ref.read(viewModeProvider.notifier).state =
                  viewMode == ViewMode.grid ? ViewMode.list : ViewMode.grid;
            },
          ),
          IconButton(
            icon: Badge(
              isLabelVisible: query.hasFilters,
              label: Text('${query.activeFilterCount}'),
              child: const Icon(Icons.filter_list),
            ),
            tooltip: 'Filters',
            onPressed: () => _showFiltersSheet(context, ref),
          ),
        ],
      ),
      body: Column(
        children: [
          // Active filters
          if (query.hasFilters) _ActiveFiltersBar(query: query),

          // Results
          Expanded(
            child: results.when(
              data: (files) {
                if (files.isEmpty && !query.isEmpty) {
                  return EmptyState.noResults(
                    onClearFilters: () {
                      _searchController.clear();
                      ref.read(searchQueryProvider.notifier).state = const SearchQuery();
                    },
                  );
                }

                if (files.isEmpty) {
                  return const EmptyState(
                    icon: Icons.search,
                    title: 'Search your files',
                    subtitle: 'Enter a search term or use filters',
                  );
                }

                if (viewMode == ViewMode.grid) {
                  return FileGrid(
                    files: files,
                    onFileTap: (file) => context.push('/file/${file.id}'),
                  );
                }

                return FileList(
                  files: files,
                  onFileTap: (file) => context.push('/file/${file.id}'),
                );
              },
              loading: () => viewMode == ViewMode.grid
                  ? const FileGridSkeleton()
                  : const FileListSkeleton(),
              error: (e, _) => AppErrorWidget(
                message: 'Search failed',
                details: e.toString(),
                onRetry: () => ref.invalidate(searchResultsProvider),
              ),
            ),
          ),
        ],
      ),
    );
  }

  void _showFiltersSheet(BuildContext context, WidgetRef ref) {
    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      builder: (context) => const _FiltersSheet(),
    );
  }
}

class _ActiveFiltersBar extends ConsumerWidget {
  final SearchQuery query;

  const _ActiveFiltersBar({required this.query});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final colorScheme = Theme.of(context).colorScheme;

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      decoration: BoxDecoration(
        color: colorScheme.surfaceContainerHighest,
        border: Border(
          bottom: BorderSide(color: colorScheme.outlineVariant),
        ),
      ),
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        child: Row(
          children: [
            if (query.contentType != null)
              _FilterChip(
                label: query.contentType!.displayName,
                icon: getContentTypeIcon(query.contentType!),
                onRemove: () {
                  ref.read(searchQueryProvider.notifier).state =
                      query.copyWith(clearContentType: true);
                },
              ),
            ...query.tags.map((tag) => _FilterChip(
                  label: tag,
                  icon: Icons.label,
                  onRemove: () {
                    ref.read(searchQueryProvider.notifier).state = query.removeTag(tag);
                  },
                )),
            if (query.dateFrom != null || query.dateTo != null)
              _FilterChip(
                label: 'Date range',
                icon: Icons.date_range,
                onRemove: () {
                  ref.read(searchQueryProvider.notifier).state =
                      query.copyWith(clearDateFrom: true, clearDateTo: true);
                },
              ),
            if (query.minSize != null || query.maxSize != null)
              _FilterChip(
                label: 'Size',
                icon: Icons.storage,
                onRemove: () {
                  ref.read(searchQueryProvider.notifier).state =
                      query.copyWith(clearMinSize: true, clearMaxSize: true);
                },
              ),
            // Clear all button
            TextButton(
              onPressed: () {
                ref.read(searchQueryProvider.notifier).state =
                    SearchQuery(text: query.text);
              },
              child: const Text('Clear all'),
            ),
          ],
        ),
      ),
    );
  }
}

class _FilterChip extends StatelessWidget {
  final String label;
  final IconData icon;
  final VoidCallback onRemove;

  const _FilterChip({
    required this.label,
    required this.icon,
    required this.onRemove,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(right: 8),
      child: InputChip(
        avatar: Icon(icon, size: 18),
        label: Text(label),
        onDeleted: onRemove,
        deleteIconColor: Theme.of(context).colorScheme.onSurfaceVariant,
      ),
    );
  }
}

class _FiltersSheet extends ConsumerWidget {
  const _FiltersSheet();

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final query = ref.watch(searchQueryProvider);
    final theme = Theme.of(context);

    return DraggableScrollableSheet(
      initialChildSize: 0.6,
      minChildSize: 0.4,
      maxChildSize: 0.9,
      expand: false,
      builder: (context, scrollController) {
        return Container(
          decoration: BoxDecoration(
            color: theme.colorScheme.surface,
            borderRadius: const BorderRadius.vertical(top: Radius.circular(20)),
          ),
          child: ListView(
            controller: scrollController,
            padding: const EdgeInsets.all(16),
            children: [
              // Handle
              Center(
                child: Container(
                  width: 40,
                  height: 4,
                  decoration: BoxDecoration(
                    color: theme.colorScheme.outlineVariant,
                    borderRadius: BorderRadius.circular(2),
                  ),
                ),
              ),
              const SizedBox(height: 16),

              // Title
              Text(
                'Filters',
                style: theme.textTheme.titleLarge?.copyWith(
                  fontWeight: FontWeight.bold,
                ),
              ),
              const SizedBox(height: 24),

              // Content Type
              Text('File Type', style: theme.textTheme.titleSmall),
              const SizedBox(height: 8),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: ContentType.values
                    .where((t) => t != ContentType.unknown)
                    .map((type) => FilterChip(
                          avatar: Icon(getContentTypeIcon(type), size: 18),
                          label: Text(type.displayName),
                          selected: query.contentType == type,
                          onSelected: (selected) {
                            ref.read(searchQueryProvider.notifier).state = selected
                                ? query.copyWith(contentType: type)
                                : query.copyWith(clearContentType: true);
                          },
                        ))
                    .toList(),
              ),
              const SizedBox(height: 24),

              // Sort
              Text('Sort By', style: theme.textTheme.titleSmall),
              const SizedBox(height: 8),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: SortBy.values
                    .map((sort) => ChoiceChip(
                          label: Text(sort.displayName),
                          selected: query.sortBy == sort,
                          onSelected: (selected) {
                            if (selected) {
                              ref.read(searchQueryProvider.notifier).state =
                                  query.copyWith(sortBy: sort);
                            }
                          },
                        ))
                    .toList(),
              ),
              const SizedBox(height: 24),

              // Apply button
              FilledButton(
                onPressed: () => Navigator.pop(context),
                child: const Text('Apply Filters'),
              ),
            ],
          ),
        );
      },
    );
  }
}
