import 'package:flutter/material.dart';

/// Тема приложения FamilyVault
class AppTheme {
  AppTheme._();

  // Цветовая палитра — глубокий синий с акцентами
  static const Color _primaryLight = Color(0xFF1565C0);
  static const Color _primaryDark = Color(0xFF64B5F6);
  
  /// Светлая тема
  static ThemeData get light {
    return ThemeData(
      useMaterial3: true,
      brightness: Brightness.light,
      colorScheme: ColorScheme.fromSeed(
        seedColor: _primaryLight,
        brightness: Brightness.light,
      ),
      fontFamily: 'Segoe UI',
      appBarTheme: const AppBarTheme(
        centerTitle: false,
        elevation: 0,
        scrolledUnderElevation: 1,
      ),
      cardTheme: CardThemeData(
        elevation: 0,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
          side: BorderSide(color: Colors.grey.shade200),
        ),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: Colors.grey.shade100,
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(12),
          borderSide: BorderSide.none,
        ),
        contentPadding: const EdgeInsets.symmetric(
          horizontal: 16,
          vertical: 14,
        ),
      ),
      chipTheme: ChipThemeData(
        backgroundColor: Colors.grey.shade100,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(8),
        ),
      ),
      navigationRailTheme: NavigationRailThemeData(
        backgroundColor: Colors.grey.shade50,
        indicatorColor: _primaryLight.withValues(alpha: 0.15),
        selectedIconTheme: IconThemeData(color: _primaryLight),
        selectedLabelTextStyle: TextStyle(
          color: _primaryLight,
          fontWeight: FontWeight.w600,
        ),
      ),
      navigationBarTheme: NavigationBarThemeData(
        backgroundColor: Colors.grey.shade50,
        indicatorColor: _primaryLight.withValues(alpha: 0.15),
      ),
      floatingActionButtonTheme: FloatingActionButtonThemeData(
        backgroundColor: _primaryLight,
        foregroundColor: Colors.white,
        elevation: 4,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
        ),
      ),
    );
  }

  /// Тёмная тема
  static ThemeData get dark {
    return ThemeData(
      useMaterial3: true,
      brightness: Brightness.dark,
      colorScheme: ColorScheme.fromSeed(
        seedColor: _primaryDark,
        brightness: Brightness.dark,
      ),
      fontFamily: 'Segoe UI',
      appBarTheme: const AppBarTheme(
        centerTitle: false,
        elevation: 0,
        scrolledUnderElevation: 1,
      ),
      cardTheme: CardThemeData(
        elevation: 0,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
          side: BorderSide(color: Colors.grey.shade800),
        ),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: Colors.grey.shade900,
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(12),
          borderSide: BorderSide.none,
        ),
        contentPadding: const EdgeInsets.symmetric(
          horizontal: 16,
          vertical: 14,
        ),
      ),
      chipTheme: ChipThemeData(
        backgroundColor: Colors.grey.shade800,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(8),
        ),
      ),
      navigationRailTheme: NavigationRailThemeData(
        backgroundColor: Colors.grey.shade900,
        indicatorColor: _primaryDark.withValues(alpha: 0.2),
        selectedIconTheme: IconThemeData(color: _primaryDark),
        selectedLabelTextStyle: TextStyle(
          color: _primaryDark,
          fontWeight: FontWeight.w600,
        ),
      ),
      navigationBarTheme: NavigationBarThemeData(
        backgroundColor: Colors.grey.shade900,
        indicatorColor: _primaryDark.withValues(alpha: 0.2),
      ),
      floatingActionButtonTheme: FloatingActionButtonThemeData(
        backgroundColor: _primaryDark,
        foregroundColor: Colors.black,
        elevation: 4,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
        ),
      ),
    );
  }
}

/// Адаптивные размеры
class AppSizes {
  AppSizes._();

  /// Ширина ячейки сетки
  static double gridCellWidth(BuildContext context) {
    final width = MediaQuery.of(context).size.width;
    if (width > 1200) return 200;  // Desktop large
    if (width > 800) return 180;   // Desktop
    if (width > 600) return 150;   // Tablet
    return 120;                     // Mobile
  }

  /// Количество колонок в сетке
  static int gridCrossAxisCount(BuildContext context) {
    final width = MediaQuery.of(context).size.width;
    if (width > 1400) return 7;
    if (width > 1200) return 6;
    if (width > 1000) return 5;
    if (width > 800) return 4;
    if (width > 600) return 3;
    return 2;
  }

  /// Desktop layout (sidebar + content)
  static bool isDesktop(BuildContext context) {
    return MediaQuery.of(context).size.width > 800;
  }

  /// Tablet layout
  static bool isTablet(BuildContext context) {
    final width = MediaQuery.of(context).size.width;
    return width > 600 && width <= 800;
  }

  /// Mobile layout
  static bool isMobile(BuildContext context) {
    return MediaQuery.of(context).size.width <= 600;
  }

  /// Content padding
  static EdgeInsets contentPadding(BuildContext context) {
    if (isDesktop(context)) {
      return const EdgeInsets.all(24);
    }
    return const EdgeInsets.all(16);
  }

  /// Max content width for large screens
  static double? maxContentWidth(BuildContext context) {
    final width = MediaQuery.of(context).size.width;
    if (width > 1400) return 1200;
    return null;
  }
}
