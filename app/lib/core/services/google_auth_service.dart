import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math';

import 'package:crypto/crypto.dart';
import 'package:http/http.dart' as http;
import 'package:shelf/shelf.dart';
import 'package:shelf/shelf_io.dart' as shelf_io;
import 'package:url_launcher/url_launcher.dart';

import '../../config/google_credentials.dart';
import '../ffi/native_bridge.dart';
import '../models/cloud_account.dart';

class GoogleAuthException implements Exception {
  final String message;
  GoogleAuthException(this.message);
  @override
  String toString() => 'GoogleAuthException: $message';
}

/// Abstract interface for storage operations
abstract class AuthStorage {
  /// Whether secure storage is ready for use (e.g. after pairing init)
  bool get isReady;

  int addCloudAccount(String type, String email, String name, String avatarUrl);
  void removeCloudAccount(int accountId);
  void secureStore(String key, String value);
  String? secureRetrieve(String key);
  void secureRemove(String key);
}

/// Implementation using NativeBridge
class NativeAuthStorage implements AuthStorage {
  final NativeBridge _bridge;
  NativeAuthStorage([NativeBridge? bridge]) : _bridge = bridge ?? NativeBridge.instance;

  @override
  bool get isReady => _bridge.isSecureStorageReady && _bridge.isInitialized;

  @override
  int addCloudAccount(String type, String email, String name, String avatarUrl) {
    return _bridge.addCloudAccount(type, email, name, avatarUrl);
  }

  @override
  void removeCloudAccount(int accountId) {
    _bridge.removeCloudAccount(accountId);
  }

  @override
  void secureStore(String key, String value) {
    _bridge.secureStore(key, value);
  }

  @override
  String? secureRetrieve(String key) {
    return _bridge.secureRetrieve(key);
  }

  @override
  void secureRemove(String key) {
    _bridge.secureRemove(key);
  }
}

class GoogleAuthService {
  final AuthStorage _storage;
  final http.Client _client;
  final bool _ownsClient;

  // Cache access tokens in memory
  final Map<int, String> _accessTokenCache = {};
  final Map<int, DateTime> _accessTokenExpiry = {};

  GoogleAuthService({AuthStorage? storage, http.Client? client})
      : _storage = storage ?? NativeAuthStorage(),
        _client = client ?? http.Client(),
        _ownsClient = client == null;

  static const _scopes = [
    'https://www.googleapis.com/auth/drive.readonly',
    'https://www.googleapis.com/auth/userinfo.email',
    'https://www.googleapis.com/auth/userinfo.profile',
  ];

  static const _authUrl = 'https://accounts.google.com/o/oauth2/v2/auth';
  static const _tokenUrl = 'https://oauth2.googleapis.com/token';
  static const _userInfoUrl = 'https://www.googleapis.com/oauth2/v2/userinfo';
  static const _revokeUrl = 'https://oauth2.googleapis.com/revoke';

  static final Random _rng = Random.secure();

  Future<CloudAccount> signIn() async {
    _ensureStorageReady();

    if (kGoogleClientId.isEmpty) {
      throw GoogleAuthException('Google Credentials not configured');
    }

    if (kGoogleRedirectUri.isEmpty) {
      throw GoogleAuthException('Google redirect URI not configured');
    }

    final configuredRedirect = Uri.parse(kGoogleRedirectUri);
    if (!_isSupportedRedirect(configuredRedirect)) {
      throw GoogleAuthException(
        'Redirect URI must be loopback http with host localhost/127.0.0.1 (e.g. http://localhost:8765).',
      );
    }

    final expectedPath = _normalizedPath(configuredRedirect);

    // 1. Start local server to listen for callback on configured redirect
    final bindPort = configuredRedirect.hasPort ? configuredRedirect.port : 0;
    final server = await _bindWithFallback(bindPort);
    final redirectUri = configuredRedirect.replace(port: server.port);

    final state = _generateState();
    final codeVerifier = _generateCodeVerifier();
    final codeChallenge = _codeChallengeFor(codeVerifier);

    String? authCode;
    final completer = Completer<void>();

    final handler = (Request request) {
      if (request.url.path != expectedPath) {
        return Response.notFound('Not Found');
      }
      
      final code = request.url.queryParameters['code'];
      final error = request.url.queryParameters['error'];
      final receivedState = request.url.queryParameters['state'];

      if (receivedState != state) {
        if (!completer.isCompleted) {
          completer.completeError(GoogleAuthException('Invalid OAuth state'));
        }
        return Response.forbidden('Invalid state');
      }

      if (code != null) {
        authCode = code;
        if (!completer.isCompleted) {
          completer.complete();
        }
        return Response.ok('Authentication successful! You can close this window.');
      } else if (error != null) {
        if (!completer.isCompleted) {
          completer.completeError(GoogleAuthException('Auth error: $error'));
        }
        return Response.internalServerError(body: 'Authentication failed');
      } else {
        return Response.ok('Waiting for authentication...');
      }
    };

    shelf_io.serveRequests(server, handler);

    try {
      // 2. Open browser
      final url = Uri.parse(_authUrl).replace(queryParameters: {
        'client_id': kGoogleClientId,
        'redirect_uri': redirectUri.toString(),
        'response_type': 'code',
        'scope': _scopes.join(' '),
        'access_type': 'offline',
        'prompt': 'consent',
        'state': state,
        'code_challenge': codeChallenge,
        'code_challenge_method': 'S256',
      });

      if (!await launchUrl(url, mode: LaunchMode.externalApplication)) {
        throw GoogleAuthException('Could not launch browser');
      }

      // 3. Wait for callback
      await completer.future.timeout(const Duration(minutes: 5));
    } on TimeoutException {
      throw GoogleAuthException('Authentication timed out');
    } finally {
      await server.close();
    }

    if (authCode == null) {
      throw GoogleAuthException('No authorization code received');
    }

    return signInWithAuthCode(
      authCode,
      redirectUri: redirectUri,
      codeVerifier: codeVerifier,
    );
  }

  Future<void> signOut(int accountId) async {
    _ensureStorageReady();
    final tokenKey = 'gdrive_refresh_token_$accountId';
    try {
      // Prefer revoking refresh token (long-lived). Fallback to cached access token.
      final refreshToken = _storage.secureRetrieve(tokenKey);
      final tokenToRevoke = refreshToken ?? _accessTokenCache[accountId];
      if (tokenToRevoke != null) {
        await _revokeToken(tokenToRevoke);
      }
    } catch (e) {
      // Ignore revocation errors
    }

    // Always clear secrets/cache even if account removal fails
    try {
      _storage.secureRemove(tokenKey);
    } catch (_) {}
    _clearCachedToken(accountId);

    try {
      _storage.removeCloudAccount(accountId);
    } catch (e) {
      throw GoogleAuthException('Failed to remove cloud account: $e');
    }
  }

  Future<String?> getAccessToken(int accountId) async {
    _ensureStorageReady();
    if (kGoogleClientId.isEmpty) {
      throw GoogleAuthException('Google Credentials not configured');
    }
    // Check cache
    if (_accessTokenCache.containsKey(accountId) && 
        _accessTokenExpiry[accountId] != null &&
        _accessTokenExpiry[accountId]!.isAfter(DateTime.now())) {
      return _accessTokenCache[accountId];
    }

    // Get refresh token
    final refreshToken = _storage.secureRetrieve('gdrive_refresh_token_$accountId');
    if (refreshToken == null) return null;

    // Refresh
    final response = await _client.post(
      Uri.parse(_tokenUrl),
      body: {
        'client_id': kGoogleClientId,
        'refresh_token': refreshToken,
        'grant_type': 'refresh_token',
      },
    );

    if (response.statusCode != 200) {
      if (response.statusCode == 400 || response.statusCode == 401) {
        // Token expired or revoked; clear stored token to avoid repeated failures
        _storage.secureRemove('gdrive_refresh_token_$accountId');
        _clearCachedToken(accountId);
        return null;
      }
      throw GoogleAuthException('Failed to refresh token: ${response.body}');
    }

    final dynamic data;
    try {
      data = jsonDecode(response.body);
    } on FormatException {
      throw GoogleAuthException('Invalid token response format');
    }
    final parsed = _parseTokenResponse(data, refreshTokenRequired: false);
    final newAccessToken = parsed.accessToken;
    final expiresIn = parsed.expiresIn;
    final rotatedRefreshToken = parsed.refreshToken;
    if (rotatedRefreshToken != null && rotatedRefreshToken.isNotEmpty) {
      _storage.secureStore('gdrive_refresh_token_$accountId', rotatedRefreshToken);
    }

    _accessTokenCache[accountId] = newAccessToken;
    _accessTokenExpiry[accountId] = DateTime.now().add(_expiryDuration(expiresIn));

    return newAccessToken;
  }

  bool _isSupportedRedirect(Uri uri) {
    final isHttp = uri.scheme == 'http';
    final isLocalhost = uri.host == 'localhost' || uri.host == '127.0.0.1' || uri.host == '::1';
    return isHttp && isLocalhost;
  }

  String _normalizedPath(Uri uri) {
    final path = uri.path.isEmpty ? '/' : uri.path;
    // shelf Request strips the leading slash
    return path.startsWith('/') ? path.substring(1) : path;
  }

  void _clearCachedToken(int accountId) {
    _accessTokenCache.remove(accountId);
    _accessTokenExpiry.remove(accountId);
  }

  Future<HttpServer> _bindLoopback(int port) async {
    try {
      return await HttpServer.bind(InternetAddress.loopbackIPv6, port, v6Only: false);
    } on SocketException {
      // Fallback to IPv4 if IPv6 is unavailable
      return HttpServer.bind(InternetAddress.loopbackIPv4, port);
    }
  }

  Future<HttpServer> _bindWithFallback(int port) async {
    try {
      return await _bindLoopback(port);
    } on SocketException {
      if (port != 0) {
        throw GoogleAuthException(
          'Redirect port $port is busy. Close other apps or update kGoogleRedirectUri.',
        );
      }
      return _bindLoopback(0);
    }
  }

  String _generateState() => _randomString(32);

  String _generateCodeVerifier() => _randomString(64);

  String _codeChallengeFor(String verifier) {
    final bytes = sha256.convert(ascii.encode(verifier)).bytes;
    return base64UrlEncode(bytes).replaceAll('=', '');
  }

  String _randomString(int length) {
    final bytes = List<int>.generate(length, (_) => _rng.nextInt(256));
    return base64UrlEncode(bytes).replaceAll('=', '');
  }

  int _expiryBuffer(int expiresInSeconds) {
    if (expiresInSeconds <= 0) return 0;
    final tenth = expiresInSeconds ~/ 10;
    return min(60, max(5, tenth));
  }

  Duration _expiryDuration(int expiresInSeconds) {
    final adjusted = expiresInSeconds - _expiryBuffer(expiresInSeconds);
    return Duration(seconds: max(0, adjusted));
  }

  _TokenResponse _parseTokenResponse(dynamic data, {required bool refreshTokenRequired}) {
    if (data is! Map<String, dynamic>) {
      throw GoogleAuthException('Invalid token response format');
    }

    final accessTokenRaw = data['access_token'];
    final refreshTokenRaw = data['refresh_token'];
    final expiresInRaw = data['expires_in'];

    if (accessTokenRaw is! String || accessTokenRaw.isEmpty) {
      throw GoogleAuthException('Missing access token in response');
    }

    if (refreshTokenRequired && (refreshTokenRaw is! String || refreshTokenRaw.isEmpty)) {
      throw GoogleAuthException('Missing refresh token in response');
    }

    final expiresIn = expiresInRaw is num ? expiresInRaw.toInt() : int.tryParse('$expiresInRaw') ?? -1;
    if (expiresIn <= 0) {
      throw GoogleAuthException('Invalid expires_in in response');
    }

    final refreshToken = refreshTokenRaw is String && refreshTokenRaw.isNotEmpty ? refreshTokenRaw : null;

    return _TokenResponse(
      accessToken: accessTokenRaw,
      refreshToken: refreshToken,
      expiresIn: expiresIn,
    );
  }

  void _ensureStorageReady() {
    if (!_storage.isReady) {
      throw GoogleAuthException('Secure storage not initialized. Call initPairing() first.');
    }
  }

  Future<CloudAccount> signInWithAuthCode(
    String authCode, {
    required Uri redirectUri,
    required String codeVerifier,
  }) async {
    _ensureStorageReady();

    if (kGoogleClientId.isEmpty) {
      throw GoogleAuthException('Google Credentials not configured');
    }

    if (!_isSupportedRedirect(redirectUri)) {
      throw GoogleAuthException(
        'Redirect URI must be loopback http with host localhost/127.0.0.1 (e.g. http://localhost:8765).',
      );
    }

    // 4. Exchange code for tokens
    final tokens = await _exchangeAuthCodeForTokens(authCode, redirectUri, codeVerifier);

    // 5. Get User Info
    Map<String, dynamic> userData;
    try {
      userData = await _fetchUserInfo(tokens.accessToken);
    } catch (e) {
      await _revokeToken(tokens.refreshToken ?? tokens.accessToken);
      rethrow;
    }

    // 6. Store in Database & Secure Storage
    try {
      return await _persistAccount(tokens, userData);
    } catch (e) {
      await _revokeToken(tokens.refreshToken ?? tokens.accessToken);
      rethrow;
    }
  }

  Future<_TokenResponse> _exchangeAuthCodeForTokens(
    String authCode,
    Uri redirectUri,
    String codeVerifier,
  ) async {
    final tokenResponse = await _client.post(
      Uri.parse(_tokenUrl),
      body: {
        'code': authCode,
        'client_id': kGoogleClientId,
        'redirect_uri': redirectUri.toString(),
        'grant_type': 'authorization_code',
        'code_verifier': codeVerifier,
      },
    );

    if (tokenResponse.statusCode != 200) {
      throw GoogleAuthException('Failed to exchange token: ${tokenResponse.body}');
    }

    final dynamic tokenData;
    try {
      tokenData = jsonDecode(tokenResponse.body);
    } on FormatException {
      throw GoogleAuthException('Invalid token response format');
    }
    return _parseTokenResponse(tokenData, refreshTokenRequired: true);
  }

  Future<Map<String, dynamic>> _fetchUserInfo(String accessToken) async {
    final userResponse = await _client.get(
      Uri.parse(_userInfoUrl),
      headers: {'Authorization': 'Bearer $accessToken'},
    );

    if (userResponse.statusCode != 200) {
      throw GoogleAuthException('Failed to get user info: ${userResponse.body}');
    }

    final dynamic userData;
    try {
      userData = jsonDecode(userResponse.body);
    } on FormatException {
      throw GoogleAuthException('Invalid user info response format');
    }
    if (userData is! Map<String, dynamic>) {
      throw GoogleAuthException('Invalid user info response');
    }
    return userData;
  }

  Future<CloudAccount> _persistAccount(_TokenResponse tokens, Map<String, dynamic> userData) async {
    final email = userData['email'];
    if (email is! String || email.isEmpty) {
      throw GoogleAuthException('Email is missing from user info response');
    }

    final nameValue = userData['name'];
    final name = nameValue is String && nameValue.isNotEmpty ? nameValue : email;
    final avatarValue = userData['picture'];
    final avatar = avatarValue is String && avatarValue.isNotEmpty ? avatarValue : null;

    final refreshToken = tokens.refreshToken!;
    final expiresIn = tokens.expiresIn;

    int accountId;
    try {
      accountId = _storage.addCloudAccount(
        'google_drive',
        email,
        name,
        avatar ?? '',
      );
    } catch (e) {
      throw GoogleAuthException('Failed to store account: $e');
    }

    try {
      _storage.secureStore('gdrive_refresh_token_$accountId', refreshToken);

      _accessTokenCache[accountId] = tokens.accessToken;
      _accessTokenExpiry[accountId] = DateTime.now().add(_expiryDuration(expiresIn));

      return CloudAccount(
        id: accountId,
        type: 'google_drive',
        email: email,
        displayName: name,
        avatarUrl: avatar,
        fileCount: 0,
        enabled: true,
        createdAt: DateTime.now(),
      );
    } catch (e) {
      _storage.secureRemove('gdrive_refresh_token_$accountId');
      _storage.removeCloudAccount(accountId);
      _clearCachedToken(accountId);
      throw GoogleAuthException('Failed to store account: $e');
    }
  }

  Future<void> _revokeToken(String token) async {
    try {
      await _client.post(
        Uri.parse(_revokeUrl),
        body: {'token': token},
      );
    } catch (_) {
      // Ignore revoke failures (best-effort)
    }
  }

  void dispose() {
    if (_ownsClient) {
      _client.close();
    }
  }
}

class _TokenResponse {
  final String accessToken;
  final String? refreshToken;
  final int expiresIn;

  _TokenResponse({
    required this.accessToken,
    required this.refreshToken,
    required this.expiresIn,
  });
}
