import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:familyvault/core/ffi/native_bridge.dart' show ScanProgress;
import 'package:familyvault/core/models/models.dart';
import 'package:familyvault/core/providers/providers.dart';

void main() {
  group('State Providers', () {
    group('indexVersionProvider', () {
      test('initial value is 0', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        expect(container.read(indexVersionProvider), 0);
      });

      test('can be incremented', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        expect(container.read(indexVersionProvider), 0);
        container.read(indexVersionProvider.notifier).state++;
        expect(container.read(indexVersionProvider), 1);
        container.read(indexVersionProvider.notifier).state++;
        expect(container.read(indexVersionProvider), 2);
      });
    });

    group('viewModeProvider', () {
      test('initial value is grid', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        expect(container.read(viewModeProvider), ViewMode.grid);
      });

      test('can change to list', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        container.read(viewModeProvider.notifier).state = ViewMode.list;
        expect(container.read(viewModeProvider), ViewMode.list);
      });
    });

    group('sortByProvider', () {
      test('initial value is date', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        expect(container.read(sortByProvider), SortBy.date);
      });

      test('can change sort type', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        container.read(sortByProvider.notifier).state = SortBy.name;
        expect(container.read(sortByProvider), SortBy.name);

        container.read(sortByProvider.notifier).state = SortBy.size;
        expect(container.read(sortByProvider), SortBy.size);
      });
    });

    group('sortAscProvider', () {
      test('initial value is false (descending)', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        expect(container.read(sortAscProvider), false);
      });

      test('can toggle sort direction', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        container.read(sortAscProvider.notifier).state = true;
        expect(container.read(sortAscProvider), true);

        container.read(sortAscProvider.notifier).state = false;
        expect(container.read(sortAscProvider), false);
      });
    });

    group('searchQueryProvider', () {
      test('initial value is empty query', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        final query = container.read(searchQueryProvider);
        expect(query.text, '');
        expect(query.isEmpty, true);
      });

      test('can set search text', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        container.read(searchQueryProvider.notifier).state =
            const SearchQuery(text: 'vacation photos');

        final query = container.read(searchQueryProvider);
        expect(query.text, 'vacation photos');
        expect(query.isEmpty, false);
      });

      test('can set filters', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        container.read(searchQueryProvider.notifier).state = const SearchQuery(
          contentType: ContentType.image,
          tags: ['summer', 'beach'],
        );

        final query = container.read(searchQueryProvider);
        expect(query.contentType, ContentType.image);
        expect(query.tags, ['summer', 'beach']);
        expect(query.hasFilters, true);
      });

      test('can update using copyWith', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        final initial = container.read(searchQueryProvider);
        container.read(searchQueryProvider.notifier).state =
            initial.copyWith(text: 'test', limit: 25);

        final updated = container.read(searchQueryProvider);
        expect(updated.text, 'test');
        expect(updated.limit, 25);
      });
    });

    group('scanningFolderIdProvider', () {
      test('initial value is null', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        expect(container.read(scanningFolderIdProvider), null);
      });

      test('can set folder id', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        container.read(scanningFolderIdProvider.notifier).state = 5;
        expect(container.read(scanningFolderIdProvider), 5);
      });

      test('can reset to null', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        container.read(scanningFolderIdProvider.notifier).state = 5;
        container.read(scanningFolderIdProvider.notifier).state = null;
        expect(container.read(scanningFolderIdProvider), null);
      });
    });

    group('scanProgressProvider', () {
      test('initial value is null', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        expect(container.read(scanProgressProvider), null);
      });

      test('can set progress', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        final progress = ScanProgress(
          total: 100,
          processed: 50,
          currentFile: 'test.txt',
        );
        container.read(scanProgressProvider.notifier).state = progress;

        final result = container.read(scanProgressProvider);
        expect(result?.total, 100);
        expect(result?.processed, 50);
        expect(result?.currentFile, 'test.txt');
        expect(result?.progress, 0.5);
      });
    });
  });

  group('Derived Providers', () {
    group('isScanningProvider', () {
      test('returns false when scanningFolderId is null', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        expect(container.read(isScanningProvider), false);
      });

      test('returns true when scanningFolderId is set', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        container.read(scanningFolderIdProvider.notifier).state = 1;
        expect(container.read(isScanningProvider), true);
      });

      test('returns true when scanningFolderId is -1 (scanAll)', () {
        final container = ProviderContainer();
        addTearDown(container.dispose);

        container.read(scanningFolderIdProvider.notifier).state = -1;
        expect(container.read(isScanningProvider), true);
      });
    });
  });

  group('Provider Reactivity', () {
    test('searchQueryProvider changes trigger listeners', () {
      final container = ProviderContainer();
      addTearDown(container.dispose);

      var callCount = 0;
      container.listen(searchQueryProvider, (_, __) => callCount++);

      // Initial listen call
      expect(callCount, 0);

      // First change
      container.read(searchQueryProvider.notifier).state =
          const SearchQuery(text: 'test');
      expect(callCount, 1);

      // Second change
      container.read(searchQueryProvider.notifier).state =
          const SearchQuery(text: 'another');
      expect(callCount, 2);
    });

    test('indexVersionProvider changes trigger listeners', () {
      final container = ProviderContainer();
      addTearDown(container.dispose);

      var callCount = 0;
      container.listen(indexVersionProvider, (_, __) => callCount++);

      expect(callCount, 0);

      container.read(indexVersionProvider.notifier).state++;
      expect(callCount, 1);

      container.read(indexVersionProvider.notifier).state++;
      expect(callCount, 2);
    });

    test('viewModeProvider changes trigger listeners', () {
      final container = ProviderContainer();
      addTearDown(container.dispose);

      var callCount = 0;
      container.listen(viewModeProvider, (_, __) => callCount++);

      expect(callCount, 0);

      container.read(viewModeProvider.notifier).state = ViewMode.list;
      expect(callCount, 1);

      container.read(viewModeProvider.notifier).state = ViewMode.grid;
      expect(callCount, 2);
    });
  });

  group('Search Query Integration', () {
    test('search query pagination works', () {
      final container = ProviderContainer();
      addTearDown(container.dispose);

      // Set initial query
      container.read(searchQueryProvider.notifier).state = const SearchQuery(
        text: 'photos',
        limit: 20,
        offset: 0,
      );

      // Navigate to next page
      final currentQuery = container.read(searchQueryProvider);
      container.read(searchQueryProvider.notifier).state =
          currentQuery.nextPage();

      final updatedQuery = container.read(searchQueryProvider);
      expect(updatedQuery.offset, 20);
      expect(updatedQuery.limit, 20);
      expect(updatedQuery.text, 'photos');
    });

    test('search query tag management works', () {
      final container = ProviderContainer();
      addTearDown(container.dispose);

      // Add tags
      container.read(searchQueryProvider.notifier).state =
          const SearchQuery().addTag('vacation');

      var query = container.read(searchQueryProvider);
      expect(query.tags, ['vacation']);

      container.read(searchQueryProvider.notifier).state =
          query.addTag('summer');

      query = container.read(searchQueryProvider);
      expect(query.tags, ['vacation', 'summer']);

      // Remove tag
      container.read(searchQueryProvider.notifier).state =
          query.removeTag('vacation');

      query = container.read(searchQueryProvider);
      expect(query.tags, ['summer']);
    });

    test('search query filter clearing works', () {
      final container = ProviderContainer();
      addTearDown(container.dispose);

      // Set query with filters
      container.read(searchQueryProvider.notifier).state = const SearchQuery(
        text: 'test',
        contentType: ContentType.image,
        tags: ['tag1', 'tag2'],
      );

      var query = container.read(searchQueryProvider);
      expect(query.hasFilters, true);

      // Clear content type
      container.read(searchQueryProvider.notifier).state =
          query.copyWith(clearContentType: true);

      query = container.read(searchQueryProvider);
      expect(query.contentType, null);
      expect(query.tags, ['tag1', 'tag2']); // Tags preserved

      // Clear all by resetting
      container.read(searchQueryProvider.notifier).state = const SearchQuery();

      query = container.read(searchQueryProvider);
      expect(query.isEmpty, true);
    });
  });
}

