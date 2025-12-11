import 'dart:convert';

import 'package:familyvault/core/services/google_auth_service.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';

class MockAuthStorage implements AuthStorage {
  final Map<String, String> secureData = {};
  final List<int> removedAccounts = [];
  int nextAccountId = 1;

  @override
  bool get isReady => true;

  @override
  int addCloudAccount(String type, String email, String name, String avatarUrl) {
    return nextAccountId++;
  }

  @override
  void removeCloudAccount(int accountId) {
    removedAccounts.add(accountId);
  }

  @override
  void secureRemove(String key) {
    secureData.remove(key);
  }

  @override
  String? secureRetrieve(String key) {
    return secureData[key];
  }

  @override
  void secureStore(String key, String value) {
    secureData[key] = value;
  }
}

class UnreadyAuthStorage implements AuthStorage {
  @override
  bool get isReady => false;

  @override
  int addCloudAccount(String type, String email, String name, String avatarUrl) {
    throw UnimplementedError();
  }

  @override
  void removeCloudAccount(int accountId) {
    throw UnimplementedError();
  }

  @override
  void secureRemove(String key) {
    throw UnimplementedError();
  }

  @override
  String? secureRetrieve(String key) {
    throw UnimplementedError();
  }

  @override
  void secureStore(String key, String value) {
    throw UnimplementedError();
  }
}

class FailingAddAccountStorage extends MockAuthStorage {
  @override
  int addCloudAccount(String type, String email, String name, String avatarUrl) {
    throw Exception('db failed');
  }
}

void main() {
  group('GoogleAuthService', () {
    late MockAuthStorage mockStorage;
    late GoogleAuthService service;

    setUp(() {
      mockStorage = MockAuthStorage();
    });

    test('getAccessToken returns cached token if valid', () async {
      // Seed storage with refresh token
      mockStorage.secureStore('gdrive_refresh_token_1', 'refresh_token_123');
      
      int networkCalls = 0;
      final client = MockClient((request) async {
        networkCalls++;
        if (request.url.path.contains('token')) {
             return http.Response(jsonEncode({
            'access_token': 'new_access_token',
            'expires_in': 3600,
          }), 200);
        }
        return http.Response('Not Found', 404);
      });
      
      service = GoogleAuthService(storage: mockStorage, client: client);
      
      // First call - should hit network (refresh)
      final token1 = await service.getAccessToken(1);
      expect(token1, 'new_access_token');
      expect(networkCalls, 1);
      
      // Second call - should hit cache (no network)
      final token2 = await service.getAccessToken(1);
      expect(token2, 'new_access_token');
      expect(networkCalls, 1); // Still 1
    });

    test('getAccessToken updates refresh token when rotated', () async {
      mockStorage.secureStore('gdrive_refresh_token_1', 'old_refresh');

      final client = MockClient((request) async {
        return http.Response(
          jsonEncode({
            'access_token': 'rotated_access',
            'refresh_token': 'new_refresh',
            'expires_in': 1800,
          }),
          200,
        );
      });

      service = GoogleAuthService(storage: mockStorage, client: client);

      final token = await service.getAccessToken(1);

      expect(token, 'rotated_access');
      expect(mockStorage.secureRetrieve('gdrive_refresh_token_1'), 'new_refresh');
    });

    test('getAccessToken clears stored token on unauthorized refresh', () async {
      mockStorage.secureStore('gdrive_refresh_token_1', 'stale_refresh');

      final client = MockClient((request) async {
        return http.Response('invalid_grant', 401);
      });

      service = GoogleAuthService(storage: mockStorage, client: client);

      final token = await service.getAccessToken(1);

      expect(token, isNull);
      expect(mockStorage.secureRetrieve('gdrive_refresh_token_1'), isNull);
    });

    test('getAccessToken throws GoogleAuthException on invalid JSON', () async {
      mockStorage.secureStore('gdrive_refresh_token_1', 'refresh_token');

      final client = MockClient((request) async {
        return http.Response('not-json', 200);
      });

      service = GoogleAuthService(storage: mockStorage, client: client);

      await expectLater(
        service.getAccessToken(1),
        throwsA(isA<GoogleAuthException>()),
      );
    });

    test('getAccessToken throws when token response is invalid', () async {
      mockStorage.secureStore('gdrive_refresh_token_1', 'refresh_token');

      final client = MockClient((request) async {
        return http.Response(
          jsonEncode({
            // missing access_token
            'expires_in': 3600,
          }),
          200,
        );
      });

      service = GoogleAuthService(storage: mockStorage, client: client);

      await expectLater(
        service.getAccessToken(1),
        throwsA(isA<GoogleAuthException>()),
      );
    });

    test('signOut clears data', () async {
      mockStorage.secureStore('gdrive_refresh_token_1', 'refresh_token');
      final client = MockClient((request) async {
        return http.Response('OK', 200);
      });
      service = GoogleAuthService(storage: mockStorage, client: client);

      await service.signOut(1);

      expect(mockStorage.secureRetrieve('gdrive_refresh_token_1'), isNull);
      expect(mockStorage.removedAccounts, contains(1));
    });

    test('signOut revokes refresh token', () async {
      mockStorage.secureStore('gdrive_refresh_token_1', 'refresh_token_value');
      int revokeCalls = 0;
      final client = MockClient((request) async {
        if (request.url.path.contains('revoke')) {
          revokeCalls++;
          expect(request.bodyFields['token'], 'refresh_token_value');
          return http.Response('OK', 200);
        }
        return http.Response('OK', 200);
      });

      service = GoogleAuthService(storage: mockStorage, client: client);
      await service.signOut(1);

      expect(revokeCalls, 1);
      expect(mockStorage.secureRetrieve('gdrive_refresh_token_1'), isNull);
      expect(mockStorage.removedAccounts, contains(1));
    });

    test('getAccessToken throws if secure storage is not ready', () async {
      final client = MockClient((request) async {
        return http.Response('{}', 200);
      });
      final unready = UnreadyAuthStorage();
      final svc = GoogleAuthService(storage: unready, client: client);

      await expectLater(
        svc.getAccessToken(1),
        throwsA(isA<GoogleAuthException>()),
      );
    });

    test('signInWithAuthCode revokes refresh token when storing fails', () async {
      final failingStorage = FailingAddAccountStorage();
      int revokeCalls = 0;

      final client = MockClient((request) async {
        if (request.url.path.contains('token')) {
          return http.Response(
            jsonEncode({
              'access_token': 'access_token_value',
              'refresh_token': 'refresh_token_value',
              'expires_in': 3600,
            }),
            200,
          );
        }

        if (request.url.path.contains('userinfo')) {
          return http.Response(
            jsonEncode({
              'email': 'user@example.com',
              'name': 'User Name',
              'picture': 'http://avatar',
            }),
            200,
          );
        }

        if (request.url.path.contains('revoke')) {
          revokeCalls++;
          expect(request.bodyFields['token'], 'refresh_token_value');
          return http.Response('OK', 200);
        }

        return http.Response('Not Found', 404);
      });

      service = GoogleAuthService(storage: failingStorage, client: client);

      await expectLater(
        service.signInWithAuthCode(
          'auth_code',
          redirectUri: Uri.parse('http://localhost:8765/oauth2redirect'),
          codeVerifier: 'verifier',
        ),
        throwsA(isA<GoogleAuthException>()),
      );

      expect(revokeCalls, 1);
      expect(failingStorage.secureData, isEmpty);
    });
  });
}
