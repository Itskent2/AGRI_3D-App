import 'package:flutter/material.dart';

/// Accent color hex values — used across the entire app
class AppColors {
  // Accent palette
  static const Color orange = Color(0xFFF97316);
  static const Color blue   = Color(0xFF3B82F6);
  static const Color green  = Color(0xFF22C55E);
  static const Color purple = Color(0xFFA855F7);
  static const Color red    = Color(0xFFEF4444);
  static const Color yellow = Color(0xFFEAB308);

  // Background shades (dark mode)
  static const Color bgDeepest = Color(0xFF030712); // deepest surface
  static const Color bgDark    = Color(0xFF0B0F1A); // app root background
  static const Color bgPage    = Color(0xFF111827); // main content area
  static const Color bgCard    = Color(0xFF1F2937); // card / panel
  static const Color bgInput   = Color(0xFF111827); // input field

  // Border
  static const Color borderDefault = Color(0xFF374151); // gray-700
  static const Color borderSubtle  = Color(0xFF1F2937); // gray-800

  // Text
  static const Color textPrimary   = Color(0xFFFFFFFF);
  static const Color textSecondary = Color(0xFF9CA3AF); // gray-400
  static const Color textMuted     = Color(0xFF6B7280); // gray-500
  static const Color textDim       = Color(0xFF4B5563); // gray-600

  // Status colors
  static const Color statusGreen  = Color(0xFF22C55E);
  static const Color statusRed    = Color(0xFFEF4444);
  static const Color statusBlue   = Color(0xFF3B82F6);
  static const Color statusYellow = Color(0xFFEAB308);
}

/// Available accent color keys
enum AccentColorKey { orange, blue, green, purple, red, yellow }

/// Accent color selector options shown in Settings screen.
final List<Map<String, dynamic>> accentColorOptions = [
  {'id': AccentColorKey.orange, 'label': 'Orange', 'color': AppColors.orange},
  {'id': AccentColorKey.blue,   'label': 'Blue',   'color': AppColors.blue},
  {'id': AccentColorKey.green,  'label': 'Green',  'color': AppColors.green},
  {'id': AccentColorKey.purple, 'label': 'Purple', 'color': AppColors.purple},
  {'id': AccentColorKey.red,    'label': 'Red',    'color': AppColors.red},
  {'id': AccentColorKey.yellow, 'label': 'Yellow', 'color': AppColors.yellow},
];