import 'package:flutter_test/flutter_test.dart';
import 'package:familyvault/core/models/models.dart';

void main() {
  group('SearchQuery', () {
    group('constructors', () {
      test('default constructor has sensible defaults', () {
        const query = SearchQuery();

        expect(query.text, '');
        expect(query.contentType, null);
        expect(query.extension, null);
        expect(query.folderId, null);
        expect(query.tags, isEmpty);
        expect(query.excludeTags, isEmpty);
        expect(query.limit, 100);
        expect(query.offset, 0);
        expect(query.sortBy, SortBy.relevance);
        expect(query.sortAsc, false);
        expect(query.includeRemote, true);
      });

      test('empty factory creates default query', () {
        final query = SearchQuery.empty();
        expect(query.isEmpty, true);
      });
    });

    group('fromJson', () {
      test('parses minimal JSON', () {
        final json = <String, dynamic>{};
        final query = SearchQuery.fromJson(json);

        expect(query.text, '');
        expect(query.limit, 100);
        expect(query.includeRemote, true);
      });

      test('parses full JSON', () {
        final json = {
          'text': 'vacation photos',
          'contentType': 1, // image
          'extension': 'jpg',
          'folderId': 5,
          'dateFrom': 1700000000,
          'dateTo': 1700100000,
          'minSize': 1024,
          'maxSize': 10485760,
          'tags': ['summer', 'beach'],
          'excludeTags': ['work'],
          'visibility': 1,
          'includeRemote': true,
          'limit': 50,
          'offset': 100,
          'sortBy': 2, // date (0=relevance, 1=name, 2=date, 3=size)
          'sortAsc': true,
        };

        final query = SearchQuery.fromJson(json);

        expect(query.text, 'vacation photos');
        expect(query.contentType, ContentType.image);
        expect(query.extension, 'jpg');
        expect(query.folderId, 5);
        expect(query.dateFrom?.year, 2023);
        expect(query.dateTo?.year, 2023);
        expect(query.minSize, 1024);
        expect(query.maxSize, 10485760);
        expect(query.tags, ['summer', 'beach']);
        expect(query.excludeTags, ['work']);
        expect(query.visibility, Visibility.family);
        expect(query.includeRemote, true);
        expect(query.limit, 50);
        expect(query.offset, 100);
        expect(query.sortBy, SortBy.date);
        expect(query.sortAsc, true);
      });
    });

    group('toJson', () {
      test('serializes correctly', () {
        final query = SearchQuery(
          text: 'test',
          contentType: ContentType.video,
          limit: 25,
          offset: 50,
          sortBy: SortBy.size,
        );

        final json = query.toJson();

        expect(json['text'], 'test');
        expect(json['contentType'], 2); // video
        expect(json['limit'], 25);
        expect(json['offset'], 50);
        expect(json['sortBy'], 3); // size (0=relevance, 1=name, 2=date, 3=size)
      });

      test('omits null optional fields', () {
        final query = SearchQuery(text: 'test');
        final json = query.toJson();

        expect(json.containsKey('contentType'), false);
        expect(json.containsKey('extension'), false);
        expect(json.containsKey('folderId'), false);
        expect(json.containsKey('dateFrom'), false);
        expect(json.containsKey('visibility'), false);
      });
    });

    group('isEmpty', () {
      test('returns true for empty query', () {
        expect(const SearchQuery().isEmpty, true);
        expect(SearchQuery.empty().isEmpty, true);
      });

      test('returns false when has text', () {
        expect(const SearchQuery(text: 'search').isEmpty, false);
      });

      test('returns false when has filters', () {
        expect(
          const SearchQuery(contentType: ContentType.image).isEmpty,
          false,
        );
        expect(
          const SearchQuery(tags: ['tag']).isEmpty,
          false,
        );
      });
    });

    group('hasFilters', () {
      test('returns false for default query', () {
        expect(const SearchQuery().hasFilters, false);
      });

      test('returns true for each filter type', () {
        expect(
          const SearchQuery(contentType: ContentType.image).hasFilters,
          true,
        );
        expect(const SearchQuery(extension: 'pdf').hasFilters, true);
        expect(const SearchQuery(folderId: 1).hasFilters, true);
        expect(SearchQuery(dateFrom: DateTime.now()).hasFilters, true);
        expect(SearchQuery(dateTo: DateTime.now()).hasFilters, true);
        expect(const SearchQuery(minSize: 1024).hasFilters, true);
        expect(const SearchQuery(maxSize: 1024).hasFilters, true);
        expect(const SearchQuery(tags: ['tag']).hasFilters, true);
        expect(const SearchQuery(excludeTags: ['tag']).hasFilters, true);
        expect(
          const SearchQuery(visibility: Visibility.private).hasFilters,
          true,
        );
      });
    });

    group('activeFilterCount', () {
      test('returns 0 for no filters', () {
        expect(const SearchQuery().activeFilterCount, 0);
      });

      test('counts individual filters', () {
        expect(
          const SearchQuery(contentType: ContentType.image).activeFilterCount,
          1,
        );
        expect(
          const SearchQuery(
            contentType: ContentType.image,
            extension: 'jpg',
          ).activeFilterCount,
          2,
        );
      });

      test('counts date range as one filter', () {
        final withBothDates = SearchQuery(
          dateFrom: DateTime(2023),
          dateTo: DateTime(2024),
        );
        expect(withBothDates.activeFilterCount, 1);
      });

      test('counts size range as one filter', () {
        const withBothSizes = SearchQuery(minSize: 1024, maxSize: 10240);
        expect(withBothSizes.activeFilterCount, 1);
      });

      test('counts each tag', () {
        const withTags = SearchQuery(tags: ['a', 'b', 'c']);
        expect(withTags.activeFilterCount, 3);
      });
    });

    group('copyWith', () {
      test('creates modified copy', () {
        const original = SearchQuery(text: 'original');
        final modified = original.copyWith(text: 'modified');

        expect(original.text, 'original');
        expect(modified.text, 'modified');
      });

      test('preserves unchanged fields', () {
        const original = SearchQuery(
          text: 'search',
          contentType: ContentType.image,
          limit: 50,
        );
        final modified = original.copyWith(text: 'new search');

        expect(modified.contentType, ContentType.image);
        expect(modified.limit, 50);
      });

      test('clears fields with clear flags', () {
        const original = SearchQuery(
          contentType: ContentType.image,
          extension: 'jpg',
        );
        final cleared = original.copyWith(
          clearContentType: true,
          clearExtension: true,
        );

        expect(cleared.contentType, null);
        expect(cleared.extension, null);
      });
    });

    group('tag operations', () {
      test('addTag adds new tag', () {
        const query = SearchQuery(tags: ['a']);
        final withNewTag = query.addTag('b');

        expect(withNewTag.tags, ['a', 'b']);
      });

      test('addTag does not duplicate', () {
        const query = SearchQuery(tags: ['a', 'b']);
        final withExisting = query.addTag('a');

        expect(withExisting.tags, ['a', 'b']);
        expect(identical(withExisting, query), true);
      });

      test('removeTag removes tag', () {
        const query = SearchQuery(tags: ['a', 'b', 'c']);
        final withoutB = query.removeTag('b');

        expect(withoutB.tags, ['a', 'c']);
      });

      test('removeTag handles non-existent tag', () {
        const query = SearchQuery(tags: ['a', 'b']);
        final result = query.removeTag('z');

        expect(result.tags, ['a', 'b']);
      });
    });

    group('pagination', () {
      test('nextPage advances offset', () {
        const query = SearchQuery(limit: 20, offset: 0);
        final page2 = query.nextPage();
        final page3 = page2.nextPage();

        expect(page2.offset, 20);
        expect(page3.offset, 40);
        expect(page3.limit, 20);
      });

      test('resetPagination sets offset to 0', () {
        const query = SearchQuery(offset: 100);
        final reset = query.resetPagination();

        expect(reset.offset, 0);
      });
    });
  });

  group('SortBy', () {
    test('fromValue returns correct sort type', () {
      expect(SortBy.fromValue(0), SortBy.relevance);
      expect(SortBy.fromValue(1), SortBy.name);
      expect(SortBy.fromValue(2), SortBy.date);
      expect(SortBy.fromValue(3), SortBy.size);
      expect(SortBy.fromValue(99), SortBy.relevance); // default
    });

    test('value returns correct integer', () {
      expect(SortBy.relevance.value, 0);
      expect(SortBy.name.value, 1);
      expect(SortBy.date.value, 2);
      expect(SortBy.size.value, 3);
    });
  });
}

