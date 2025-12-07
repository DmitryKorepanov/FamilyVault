import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import 'core/providers/providers.dart';
import 'features/search/search_home_screen.dart';
import 'features/folders/folders_screen.dart';
import 'features/tags/tags_screen.dart';
import 'features/settings/settings_screen.dart';
import 'features/duplicates/duplicates_screen.dart';
import 'features/files/file_view_screen.dart';
import 'shared/widgets/app_error_widget.dart';
import 'theme/app_theme.dart';

/// Навигационные пути
class AppRoutes {
  static const home = '/';
  static const folders = '/folders';
  static const tags = '/tags';
  static const settings = '/settings';
  static const duplicates = '/duplicates';
  static const fileView = '/file/:id';
}

/// Главный роутер приложения
final routerProvider = Provider<GoRouter>((ref) {
  return GoRouter(
    initialLocation: AppRoutes.home,
    routes: [
      // Main search screen
      GoRoute(
        path: AppRoutes.home,
        name: 'home',
        builder: (context, state) => const SearchHomeScreen(),
      ),
      // Secondary screens
      GoRoute(
        path: AppRoutes.folders,
        name: 'folders',
        builder: (context, state) => const FoldersScreen(),
      ),
      GoRoute(
        path: AppRoutes.tags,
        name: 'tags',
        builder: (context, state) => const TagsScreen(),
      ),
      GoRoute(
        path: AppRoutes.settings,
        name: 'settings',
        builder: (context, state) => const SettingsScreen(),
      ),
      GoRoute(
        path: AppRoutes.duplicates,
        name: 'duplicates',
        builder: (context, state) => const DuplicatesScreen(),
      ),
      GoRoute(
        path: AppRoutes.fileView,
        name: 'fileView',
        builder: (context, state) {
          final id = int.tryParse(state.pathParameters['id'] ?? '') ?? 0;
          return FileViewScreen(fileId: id);
        },
      ),
    ],
  );
});

/// Главный виджет приложения
class FamilyVaultApp extends ConsumerWidget {
  const FamilyVaultApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final router = ref.watch(routerProvider);
    final initState = ref.watch(appInitializedProvider);

    return MaterialApp.router(
      title: 'FamilyVault',
      debugShowCheckedModeBanner: false,
      theme: AppTheme.light,
      darkTheme: AppTheme.dark,
      themeMode: ThemeMode.system,
      routerConfig: router,
      builder: (context, child) {
        // Показываем загрузку при инициализации
        return initState.when(
          data: (_) => child ?? const SizedBox.shrink(),
          loading: () => const _InitializingScreen(),
          error: (error, _) => _InitErrorScreen(error: error),
        );
      },
    );
  }
}

class _InitializingScreen extends StatelessWidget {
  const _InitializingScreen();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      theme: AppTheme.light,
      darkTheme: AppTheme.dark,
      themeMode: ThemeMode.system,
      home: Scaffold(
        body: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                Icons.folder_special,
                size: 64,
                color: Theme.of(context).colorScheme.primary,
              ),
              const SizedBox(height: 24),
              const Text(
                'FamilyVault',
                style: TextStyle(
                  fontSize: 24,
                  fontWeight: FontWeight.bold,
                ),
              ),
              const SizedBox(height: 16),
              const CircularProgressIndicator(),
              const SizedBox(height: 8),
              Text(
                'Initializing...',
                style: TextStyle(
                  color: Theme.of(context).colorScheme.outline,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _InitErrorScreen extends StatelessWidget {
  final Object error;

  const _InitErrorScreen({required this.error});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      theme: AppTheme.light,
      darkTheme: AppTheme.dark,
      themeMode: ThemeMode.system,
      home: Scaffold(
        body: AppErrorWidget(
          message: 'Failed to initialize',
          details: error.toString(),
        ),
      ),
    );
  }
}
