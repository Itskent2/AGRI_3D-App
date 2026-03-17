// lib/providers/accent_color_provider.dart
//
// Flutter equivalent of:
//   src/app/providers/useAccentColor.tsx
//
// Usage in any ConsumerWidget:
//   final accentEnum = ref.watch(accentColorProvider);
//   final accent     = colorMap[accentEnum]!;
//   → Then use accent.text, accent.bg, accent.bgLight, etc.
//
// To change accent color from any widget:
//   ref.read(accentColorProvider.notifier).set(AccentColor.blue);

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

// ─────────────────────────────────────────────────────────────
// Enum  (≈ type AccentColor = 'orange' | 'blue' | ... in TS)
// ─────────────────────────────────────────────────────────────

enum AccentColor { orange, blue, green, purple, red, yellow }

// ─────────────────────────────────────────────────────────────
// Data class  (≈ AccentColorClasses interface in TS)
//
// Tailwind opacity utilities are converted to Flutter Color.withOpacity():
//   bg-orange-500/10  →  bg.withOpacity(0.10)  →  stored as bgLight
//   bg-orange-500/5   →  bg.withOpacity(0.05)  →  stored as bgLighter
//   bg-orange-500/15  →  bg.withOpacity(0.15)  →  stored as bgMedium
//   border-*/20       →  bg.withOpacity(0.20)  →  stored as borderLight
//   border-*/30       →  bg.withOpacity(0.30)  →  stored as borderHoverLight
//   hover:bg-*/20     →  bg.withOpacity(0.20)  →  stored as bgHoverLight
// ─────────────────────────────────────────────────────────────

class AccentColorData {
  /// The raw accent Color (≈ hex field).
  final Color hex;

  /// Primary accent color — use for icon/text tints.  (≈ text-*-500)
  final Color text;

  /// Slightly lighter text tint.  (≈ text-*-400)
  final Color textMuted;

  /// Lightest text tint.  (≈ text-*-300)
  final Color textLight;

  /// Solid background fill.  (≈ bg-*-500)
  final Color bg;

  /// Darker solid fill for hover states.  (≈ hover:bg-*-600)
  final Color hover;

  const AccentColorData({
    required this.hex,
    required this.text,
    required this.textMuted,
    required this.textLight,
    required this.bg,
    required this.hover,
  });

  // ── Computed opacity variants ───────────────────────────
  // Derived from [bg] at runtime — no extra constructor params needed.

  /// bg-*-500/10  — subtle tinted background (e.g. icon circles)
  Color get bgLight => bg.withOpacity(0.10);

  /// bg-*-500/5   — barely-there tint
  Color get bgLighter => bg.withOpacity(0.05);

  /// bg-*-500/15  — medium tint
  Color get bgMedium => bg.withOpacity(0.15);

  /// border-*-500/20  — subtle border
  Color get borderLight => bg.withOpacity(0.20);

  /// hover:border-*-500/30  — border on hover
  Color get borderHoverLight => bg.withOpacity(0.30);

  /// hover:bg-*-500/20  — hover background tint
  Color get bgHoverLight => bg.withOpacity(0.20);

  // ── Convenience widget helpers ──────────────────────────

  /// TextStyle using the primary accent color.
  TextStyle get textStyle => TextStyle(color: text);

  /// TextStyle using the muted accent color.
  TextStyle get textMutedStyle => TextStyle(color: textMuted);

  /// BoxDecoration with a subtle tinted background (bgLight).
  BoxDecoration get bgLightDecoration => BoxDecoration(color: bgLight);

  /// BoxDecoration with solid accent background.
  BoxDecoration get bgDecoration => BoxDecoration(color: bg);

  /// Border using the subtle borderLight color.
  Border get subtleBorder => Border.all(color: borderLight);

  /// Border using the primary accent color.
  Border get accentBorder => Border.all(color: text);
}

// ─────────────────────────────────────────────────────────────
// Color map  (≈ colorMap Record<AccentColor, AccentColorClasses>)
// ─────────────────────────────────────────────────────────────

const Map<AccentColor, AccentColorData> colorMap = {
  AccentColor.orange: AccentColorData(
    hex:       Color(0xFFF97316),
    text:      Color(0xFFF97316), // orange-500
    textMuted: Color(0xFFFB923C), // orange-400
    textLight: Color(0xFFFDBA74), // orange-300
    bg:        Color(0xFFF97316), // orange-500
    hover:     Color(0xFFEA580C), // orange-600
  ),
  AccentColor.blue: AccentColorData(
    hex:       Color(0xFF3B82F6),
    text:      Color(0xFF3B82F6), // blue-500
    textMuted: Color(0xFF60A5FA), // blue-400
    textLight: Color(0xFF93C5FD), // blue-300
    bg:        Color(0xFF3B82F6), // blue-500
    hover:     Color(0xFF2563EB), // blue-600
  ),
  AccentColor.green: AccentColorData(
    hex:       Color(0xFF22C55E),
    text:      Color(0xFF22C55E), // green-500
    textMuted: Color(0xFF4ADE80), // green-400
    textLight: Color(0xFF86EFAC), // green-300
    bg:        Color(0xFF22C55E), // green-500
    hover:     Color(0xFF16A34A), // green-600
  ),
  AccentColor.purple: AccentColorData(
    hex:       Color(0xFFA855F7),
    text:      Color(0xFFA855F7), // purple-500
    textMuted: Color(0xFFC084FC), // purple-400
    textLight: Color(0xFFD8B4FE), // purple-300
    bg:        Color(0xFFA855F7), // purple-500
    hover:     Color(0xFF9333EA), // purple-600
  ),
  AccentColor.red: AccentColorData(
    hex:       Color(0xFFEF4444),
    text:      Color(0xFFEF4444), // red-500
    textMuted: Color(0xFFF87171), // red-400
    textLight: Color(0xFFFCA5A5), // red-300
    bg:        Color(0xFFEF4444), // red-500
    hover:     Color(0xFFDC2626), // red-600
  ),
  AccentColor.yellow: AccentColorData(
    hex:       Color(0xFFEAB308),
    text:      Color(0xFFEAB308), // yellow-500
    textMuted: Color(0xFFFACC15), // yellow-400
    textLight: Color(0xFFFDE047), // yellow-300
    bg:        Color(0xFFEAB308), // yellow-500
    hover:     Color(0xFFCA8A04), // yellow-600
  ),
};

// ─────────────────────────────────────────────────────────────
// Notifier  (Riverpod 3.x — StateProvider was removed)
// ─────────────────────────────────────────────────────────────

class AccentColorNotifier extends Notifier<AccentColor> {
  @override
  AccentColor build() => AccentColor.orange; // default accent

  /// Change the active accent color from any widget:
  ///   ref.read(accentColorProvider.notifier).set(AccentColor.blue);
  void set(AccentColor color) => state = color;
}

final accentColorProvider =
    NotifierProvider<AccentColorNotifier, AccentColor>(
        AccentColorNotifier.new);