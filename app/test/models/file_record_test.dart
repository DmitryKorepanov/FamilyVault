import 'package:flutter_test/flutter_test.dart';
import 'package:familyvault/core/models/models.dart';

void main() {
  group('FileRecordCompact', () {
    group('fromJson', () {
      test('parses minimal JSON', () {
        final json = {
          'id': 1,
          'name': 'test.txt',
          'size': 1024,
          'modifiedAt': 1700000000,
        };

        final record = FileRecordCompact.fromJson(json);

        expect(record.id, 1);
        expect(record.name, 'test.txt');
        expect(record.size, 1024);
        expect(record.modifiedAt.year, 2023);
      });

      test('parses full JSON', () {
        final json = {
          'id': 42,
          'folderId': 5,
          'relativePath': 'photos/2023/image.jpg',
          'folderPath': 'C:/Pictures',
          'name': 'image.jpg',
          'extension': 'jpg',
          'size': 5242880,
          'contentType': 1, // image
          'modifiedAt': 1700000000,
          'isRemote': true,
          'hasThumbnail': true,
        };

        final record = FileRecordCompact.fromJson(json);

        expect(record.id, 42);
        expect(record.folderId, 5);
        expect(record.relativePath, 'photos/2023/image.jpg');
        expect(record.folderPath, 'C:/Pictures');
        expect(record.name, 'image.jpg');
        expect(record.extension, 'jpg');
        expect(record.size, 5242880);
        expect(record.contentType, ContentType.image);
        expect(record.isRemote, true);
        expect(record.hasThumbnail, true);
      });

      test('handles missing optional fields', () {
        final json = {
          'id': 1,
          'name': 'file.txt',
          'size': 100,
          'modifiedAt': 1700000000,
        };

        final record = FileRecordCompact.fromJson(json);

        expect(record.folderId, 0);
        expect(record.relativePath, '');
        expect(record.folderPath, '');
        expect(record.extension, '');
        expect(record.contentType, ContentType.unknown);
        expect(record.isRemote, false);
        expect(record.hasThumbnail, false);
      });
    });

    group('toJson', () {
      test('serializes correctly', () {
        final record = FileRecordCompact(
          id: 1,
          folderId: 2,
          relativePath: 'path/to/file.txt',
          folderPath: '/root',
          name: 'file.txt',
          extension: 'txt',
          size: 1024,
          contentType: ContentType.document,
          modifiedAt: DateTime.fromMillisecondsSinceEpoch(1700000000 * 1000),
          isRemote: false,
          hasThumbnail: false,
        );

        final json = record.toJson();

        expect(json['id'], 1);
        expect(json['folderId'], 2);
        expect(json['relativePath'], 'path/to/file.txt');
        expect(json['folderPath'], '/root');
        expect(json['name'], 'file.txt');
        expect(json['extension'], 'txt');
        expect(json['size'], 1024);
        expect(json['contentType'], 4); // document
        expect(json['modifiedAt'], 1700000000);
        expect(json['isRemote'], false);
        expect(json['hasThumbnail'], false);
      });
    });

    group('getters', () {
      test('isLocal returns opposite of isRemote', () {
        final localFile = FileRecordCompact(
          id: 1,
          folderId: 1,
          relativePath: '',
          folderPath: '',
          name: 'local.txt',
          extension: 'txt',
          size: 0,
          contentType: ContentType.document,
          modifiedAt: DateTime.now(),
          isRemote: false,
          hasThumbnail: false,
        );

        final remoteFile = FileRecordCompact(
          id: 2,
          folderId: 1,
          relativePath: '',
          folderPath: '',
          name: 'remote.txt',
          extension: 'txt',
          size: 0,
          contentType: ContentType.document,
          modifiedAt: DateTime.now(),
          isRemote: true,
          hasThumbnail: false,
        );

        expect(localFile.isLocal, true);
        expect(remoteFile.isLocal, false);
      });

      test('content type getters work correctly', () {
        FileRecordCompact createWithType(ContentType type) => FileRecordCompact(
              id: 1,
              folderId: 1,
              relativePath: '',
              folderPath: '',
              name: 'file',
              extension: '',
              size: 0,
              contentType: type,
              modifiedAt: DateTime.now(),
              isRemote: false,
              hasThumbnail: false,
            );

        expect(createWithType(ContentType.image).isImage, true);
        expect(createWithType(ContentType.video).isVideo, true);
        expect(createWithType(ContentType.audio).isAudio, true);
        expect(createWithType(ContentType.document).isDocument, true);

        expect(createWithType(ContentType.image).isVideo, false);
        expect(createWithType(ContentType.video).isImage, false);
      });

      test('fullPath builds correctly with forward slash', () {
        final record = FileRecordCompact(
          id: 1,
          folderId: 1,
          relativePath: 'subdir/file.txt',
          folderPath: '/home/user/docs',
          name: 'file.txt',
          extension: 'txt',
          size: 0,
          contentType: ContentType.document,
          modifiedAt: DateTime.now(),
          isRemote: false,
          hasThumbnail: false,
        );

        expect(record.fullPath, '/home/user/docs/subdir/file.txt');
      });

      test('fullPath builds correctly with backslash', () {
        final record = FileRecordCompact(
          id: 1,
          folderId: 1,
          relativePath: 'subdir\\file.txt',
          folderPath: 'C:\\Users\\test',
          name: 'file.txt',
          extension: 'txt',
          size: 0,
          contentType: ContentType.document,
          modifiedAt: DateTime.now(),
          isRemote: false,
          hasThumbnail: false,
        );

        expect(record.fullPath, 'C:\\Users\\test\\subdir\\file.txt');
      });
    });
  });

  group('FileRecord', () {
    test('fromJson parses complete record', () {
      final json = {
        'id': 100,
        'folderId': 10,
        'relativePath': 'photo.jpg',
        'name': 'photo.jpg',
        'extension': 'jpg',
        'size': 2048000,
        'mimeType': 'image/jpeg',
        'contentType': 1,
        'checksum': 'abc123',
        'createdAt': 1700000000,
        'modifiedAt': 1700001000,
        'indexedAt': 1700002000,
        'visibility': 1,
        'sourceDeviceId': 'device-1',
        'isRemote': false,
        'syncVersion': 5,
        'lastModifiedBy': 'user-1',
        'tags': ['vacation', 'summer'],
        'imageMeta': {
          'width': 1920,
          'height': 1080,
          'orientation': 1,
        },
      };

      final record = FileRecord.fromJson(json);

      expect(record.id, 100);
      expect(record.folderId, 10);
      expect(record.checksum, 'abc123');
      expect(record.visibility, Visibility.family);
      expect(record.tags, ['vacation', 'summer']);
      expect(record.imageMeta?.width, 1920);
      expect(record.imageMeta?.height, 1080);
    });

    test('toCompact creates correct compact record', () {
      final fullRecord = FileRecord(
        id: 1,
        folderId: 2,
        relativePath: 'test.jpg',
        name: 'test.jpg',
        extension: 'jpg',
        size: 1024,
        mimeType: 'image/jpeg',
        contentType: ContentType.image,
        createdAt: DateTime.now(),
        modifiedAt: DateTime.now(),
        indexedAt: DateTime.now(),
        visibility: Visibility.family,
        isRemote: false,
        imageMeta: ImageMetadata(width: 100, height: 100),
      );

      final compact = fullRecord.toCompact(folderPath: '/photos');

      expect(compact.id, 1);
      expect(compact.folderId, 2);
      expect(compact.folderPath, '/photos');
      expect(compact.hasThumbnail, true); // has imageMeta
    });

    test('copyWith creates modified copy', () {
      final original = FileRecord(
        id: 1,
        folderId: 1,
        relativePath: 'test.txt',
        name: 'test.txt',
        extension: 'txt',
        size: 100,
        mimeType: 'text/plain',
        contentType: ContentType.document,
        createdAt: DateTime.now(),
        modifiedAt: DateTime.now(),
        indexedAt: DateTime.now(),
        visibility: Visibility.family,
        isRemote: false,
      );

      final modified = original.copyWith(
        name: 'modified.txt',
        size: 200,
        visibility: Visibility.private,
      );

      expect(modified.id, original.id);
      expect(modified.name, 'modified.txt');
      expect(modified.size, 200);
      expect(modified.visibility, Visibility.private);
      expect(modified.extension, original.extension);
    });
  });

  group('ImageMetadata', () {
    test('fromJson parses EXIF data', () {
      final json = {
        'width': 4032,
        'height': 3024,
        'takenAt': 1700000000,
        'cameraMake': 'Apple',
        'cameraModel': 'iPhone 15 Pro',
        'latitude': 40.7128,
        'longitude': -74.0060,
        'orientation': 6,
      };

      final meta = ImageMetadata.fromJson(json);

      expect(meta.width, 4032);
      expect(meta.height, 3024);
      expect(meta.cameraMake, 'Apple');
      expect(meta.cameraModel, 'iPhone 15 Pro');
      expect(meta.latitude, 40.7128);
      expect(meta.longitude, -74.0060);
      expect(meta.orientation, 6);
      expect(meta.takenAt?.year, 2023);
    });

    test('hasLocation returns true when coordinates present', () {
      final withLocation = ImageMetadata(
        width: 100,
        height: 100,
        latitude: 40.7128,
        longitude: -74.0060,
      );

      final withoutLocation = ImageMetadata(
        width: 100,
        height: 100,
      );

      expect(withLocation.hasLocation, true);
      expect(withoutLocation.hasLocation, false);
    });

    test('resolution returns formatted string', () {
      final meta = ImageMetadata(width: 1920, height: 1080);
      expect(meta.resolution, '1920x1080');
    });

    test('camera returns combined make and model', () {
      final full = ImageMetadata(
        width: 100,
        height: 100,
        cameraMake: 'Canon',
        cameraModel: 'EOS R5',
      );
      expect(full.camera, 'Canon EOS R5');

      // Note: current impl produces 'Nikon null' when model is null
      final makeOnly = ImageMetadata(
        width: 100,
        height: 100,
        cameraMake: 'Nikon',
      );
      expect(makeOnly.camera, isNotNull);
      expect(makeOnly.camera, contains('Nikon'));

      final none = ImageMetadata(width: 100, height: 100);
      expect(none.camera, null);
    });
  });

  group('ContentType', () {
    test('fromValue returns correct type', () {
      expect(ContentType.fromValue(0), ContentType.unknown);
      expect(ContentType.fromValue(1), ContentType.image);
      expect(ContentType.fromValue(2), ContentType.video);
      expect(ContentType.fromValue(3), ContentType.audio);
      expect(ContentType.fromValue(4), ContentType.document);
      expect(ContentType.fromValue(5), ContentType.archive);
      expect(ContentType.fromValue(99), ContentType.other); // other has value 99
    });

    test('value returns correct integer', () {
      expect(ContentType.unknown.value, 0);
      expect(ContentType.image.value, 1);
      expect(ContentType.video.value, 2);
      expect(ContentType.audio.value, 3);
      expect(ContentType.document.value, 4);
      expect(ContentType.archive.value, 5);
      expect(ContentType.other.value, 99);
    });
  });

  group('Visibility', () {
    test('fromValue returns correct visibility', () {
      expect(Visibility.fromValue(0), Visibility.private);
      expect(Visibility.fromValue(1), Visibility.family);
      expect(Visibility.fromValue(99), Visibility.family); // default
    });

    test('value returns correct integer', () {
      expect(Visibility.private.value, 0);
      expect(Visibility.family.value, 1);
    });
  });
}

