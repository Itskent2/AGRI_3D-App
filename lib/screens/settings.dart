import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/theme_provider.dart';

enum AppThemeMode { dark, light, system }

class AccentColorOption {
  final String id;
  final String label;
  final Color color;

  AccentColorOption(this.id, this.label, this.color);
}

final accentColorOptions = [
  AccentColorOption("blue", "Blue", Colors.blue),
  AccentColorOption("green", "Green", Colors.green),
  AccentColorOption("orange", "Orange", Colors.orange),
  AccentColorOption("red", "Red", Colors.red),
  AccentColorOption("purple", "Purple", Colors.purple),
  AccentColorOption("yellow", "Yellow", Colors.yellow),
];

// This screen uses the global `themeProvider` (from providers/theme_provider.dart)
// to read and update app theme and accent color.

// --- SCREEN IMPLEMENTATION ---

class SettingsScreen extends ConsumerWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // Read global theme state
    final themeState = ref.watch(themeProvider);
    final accentColor = themeState.currentAccentColor;
    final accentId = themeState.accentColor.stringName;
    final accent = accentColorOptions.firstWhere((element) => element.id == accentId, orElse: () => accentColorOptions[0]);
    final isDark = themeState.isDark(context);

    return Scaffold(
      backgroundColor: isDark ? const Color(0xFF0b0f1a) : const Color(0xFFF1F5F9),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.all(24),
          children: [
            Text(
              "App Settings",
              style: TextStyle(
                fontSize: 26,
                fontWeight: FontWeight.w900,
                color: accent.color,
              ),
            ),
            const SizedBox(height: 20),
            SectionDivider(label: "Display", isDark: isDark),
            const SizedBox(height: 20),
            
            // PREVIEW BOX
            Container(
              padding: const EdgeInsets.all(16),
              decoration: BoxDecoration(
                color: isDark ? const Color(0xFF1F2937) : Colors.white,
                borderRadius: BorderRadius.circular(16),
                border: Border.all(
                  color: isDark ? Colors.grey.shade800 : Colors.grey.shade300,
                ),
              ),
              child: Row(
                children: [
                  Container(
                    width: 50, height: 50,
                    decoration: BoxDecoration(
                      color: isDark ? Colors.black : Colors.grey.shade200,
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: Icon(
                      isDark ? Icons.dark_mode : Icons.light_mode,
                      color: accent.color,
                    ),
                  ),
                  const SizedBox(width: 16),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Container(height: 6, width: 100, color: Colors.grey),
                        const SizedBox(height: 6),
                        Container(height: 6, width: 70, color: Colors.grey),
                        const SizedBox(height: 6),
                        Container(
                          height: 6, width: 50,
                          color: accent.color.withOpacity(.5),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 25),

            // THEME OPTIONS LIST
            ...AppThemeMode.values.map((mode) {
              final ThemeMode mapped = mode == AppThemeMode.dark
                  ? ThemeMode.dark
                  : mode == AppThemeMode.light
                      ? ThemeMode.light
                      : ThemeMode.system;
              bool active = themeState.themeMode == mapped;
              return Padding(
                padding: const EdgeInsets.only(bottom: 10),
                child: InkWell(
                  borderRadius: BorderRadius.circular(16),
                  onTap: () {
                    // Update global theme provider
                    ref.read(themeProvider.notifier).setTheme(mapped);
                  },
                  child: Container(
                    padding: const EdgeInsets.all(16),
                    decoration: BoxDecoration(
                      color: active ? accentColor.withOpacity(.1) : (isDark ? const Color(0xFF111827) : Colors.white),
                      borderRadius: BorderRadius.circular(16),
                      border: Border.all(
                        color: active ? accentColor : (isDark ? Colors.grey.shade800 : Colors.grey.shade300),
                      ),
                    ),
                    child: Row(
                      children: [
                        Icon(
                          mode == AppThemeMode.dark ? Icons.dark_mode : mode == AppThemeMode.light ? Icons.light_mode : Icons.phone_android,
                          color: active ? accentColor : Colors.grey,
                        ),
                        const SizedBox(width: 16),
                        Expanded(child: Text(mode.name.toUpperCase(), style: TextStyle(fontWeight: FontWeight.bold, color: active ? accentColor : null))),
                        if (active) Icon(Icons.check_circle, color: accentColor)
                      ],
                    ),
                  ),
                ),
              );
            }),

            const SizedBox(height: 25),
            SectionDivider(label: "Accent Color", isDark: isDark),
            const SizedBox(height: 20),

            // ACCENT COLOR GRID
            GridView.builder(
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              itemCount: accentColorOptions.length,
              gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
                crossAxisCount: 3, mainAxisSpacing: 10, crossAxisSpacing: 10,
              ),
              itemBuilder: (context, index) {
                final color = accentColorOptions[index];
                bool active = accentId == color.id;
                return InkWell(
                  borderRadius: BorderRadius.circular(14),
                  onTap: () {
                    final AccentColor selected = AccentColor.values.firstWhere((e) => e.stringName == color.id, orElse: () => AccentColor.orange);
                    ref.read(themeProvider.notifier).setAccentColor(selected);
                  },
                  child: Container(
                    decoration: BoxDecoration(
                      color: active ? color.color.withOpacity(.15) : Colors.transparent,
                      borderRadius: BorderRadius.circular(14),
                      border: Border.all(color: active ? color.color : (isDark ? Colors.grey.shade800 : Colors.grey.shade300)),
                    ),
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        CircleAvatar(radius: 16, backgroundColor: color.color),
                        const SizedBox(height: 6),
                        Text(color.label, style: TextStyle(fontSize: 12, color: active ? color.color : Colors.grey)),
                      ],
                    ),
                  ),
                );
              },
            ),
          ],
        ),
      ),
    );
  }
}

class SectionDivider extends StatelessWidget {
  final String label;
  final bool isDark;
  const SectionDivider({super.key, required this.label, required this.isDark});

  @override
  Widget build(BuildContext context) {
    final color = isDark ? Colors.grey.shade800 : Colors.grey.shade300;
    return Row(
      children: [
        Expanded(child: Divider(color: color)),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8),
          child: Text(label.toUpperCase(), style: TextStyle(fontSize: 11, fontWeight: FontWeight.bold, color: isDark ? Colors.grey : Colors.grey.shade600)),
        ),
        Expanded(child: Divider(color: color)),
      ],
    );
  }
}