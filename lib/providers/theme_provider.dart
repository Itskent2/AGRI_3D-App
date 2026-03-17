import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

// ─── Enums ───────────────────────────────────────────────────────────────────

enum AccentColor { orange, blue, green, purple, red, yellow }

extension AccentColorExtension on AccentColor {
  Color get color {
    switch (this) {
      case AccentColor.orange: return const Color(0xFFF97316);
      case AccentColor.blue:   return const Color(0xFF3B82F6);
      case AccentColor.green:  return const Color(0xFF22C55E);
      case AccentColor.purple: return const Color(0xFFA855F7);
      case AccentColor.red:    return const Color(0xFFEF4444);
      case AccentColor.yellow: return const Color(0xFFEAB308);
    }
  }

  String get stringName => toString().split('.').last;
}

// ─── State ───────────────────────────────────────────────────────────────────

class ThemeState {
  final ThemeMode themeMode;
  final AccentColor accentColor;

  const ThemeState({
    this.themeMode = ThemeMode.system,
    this.accentColor = AccentColor.orange,
  });

  // ✅ Reads brightness AFTER MaterialApp resolves dark/light/system
  // This means all three modes work correctly including device/system mode
  bool isDark(BuildContext context) {
    return Theme.of(context).brightness == Brightness.dark;
  }

  Color get currentAccentColor => accentColor.color;

  ThemeState copyWith({ThemeMode? themeMode, AccentColor? accentColor}) {
    return ThemeState(
      themeMode: themeMode ?? this.themeMode,
      accentColor: accentColor ?? this.accentColor,
    );
  }
}

// ─── Notifier ────────────────────────────────────────────────────────────────

class ThemeNotifier extends Notifier<ThemeState> {
  @override
  ThemeState build() {
    _loadFromPrefs();
    return const ThemeState(); // Default: system mode, orange accent
  }

  Future<void> _loadFromPrefs() async {
    try {
      final prefs = await SharedPreferences.getInstance();

      final themeString = prefs.getString('farmbot_theme');
      final themeMode = themeString == 'light'
          ? ThemeMode.light
          : themeString == 'dark'
              ? ThemeMode.dark
              : ThemeMode.system;

      final accentString = prefs.getString('farmbot_accent');
      final accentColor = AccentColor.values.firstWhere(
        (e) => e.stringName == accentString,
        orElse: () => AccentColor.orange,
      );

      state = ThemeState(themeMode: themeMode, accentColor: accentColor);
    } catch (_) {}
  }

  Future<void> setTheme(ThemeMode mode) async {
    if (state.themeMode == mode) return;
    state = state.copyWith(themeMode: mode);
    final prefs = await SharedPreferences.getInstance();
    final value = mode == ThemeMode.light
        ? 'light'
        : mode == ThemeMode.dark
            ? 'dark'
            : 'system';
    await prefs.setString('farmbot_theme', value);
  }

  Future<void> setAccentColor(AccentColor color) async {
    if (state.accentColor == color) return;
    state = state.copyWith(accentColor: color);
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('farmbot_accent', color.stringName);
  }
}

// ─── Provider ────────────────────────────────────────────────────────────────

final themeProvider = NotifierProvider<ThemeNotifier, ThemeState>(
  ThemeNotifier.new,
);