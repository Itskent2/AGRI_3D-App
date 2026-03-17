// lib/widgets/side_bar.dart

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/theme_provider.dart';

class Sidebar extends ConsumerWidget {
  final String activeTab;
  final ValueChanged<String> setActiveTab;

  const Sidebar({
    super.key,
    required this.activeTab,
    required this.setActiveTab,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final themeState = ref.watch(themeProvider);
    final accent = themeState.currentAccentColor;
    final isDark = themeState.isDark(context);

    // ── DYNAMIC COLORS BASED ON THEME ──
    final sidebarBg = isDark ? const Color(0xFF0B0F1A) : Colors.white;
    final activeTileBg = isDark ? const Color(0xFF1F2937) : accent.withOpacity(0.1);
    final textColor = isDark ? Colors.white70 : Colors.black87;
    final iconColor = isDark ? Colors.white60 : Colors.black54;
    final borderColor = isDark ? Colors.white10 : Colors.grey.shade300;

    final List<Map<String, dynamic>> menuItems = [
      {'id': 'dashboard', 'icon': Icons.grid_view_rounded, 'label': 'Dashboard'},
      {'id': 'controls', 'icon': Icons.tune_rounded, 'label': 'Manual Control'},
      {'id': 'monitoring', 'icon': Icons.show_chart_rounded, 'label': 'Live Monitoring'},
      {'id': 'weather', 'icon': Icons.wb_sunny_outlined, 'label': 'Weather Forecast'},
      {'id': 'map', 'icon': Icons.map_outlined, 'label': 'Plot Map'},
      {'id': 'logs', 'icon': Icons.history_rounded, 'label': 'Event Logs'},
      {'id': 'settings', 'icon': Icons.settings_outlined, 'label': 'Settings'},
    ];

    return Container(
      width: 260,
      decoration: BoxDecoration(
        color: sidebarBg, // Changed from hardcoded dark color
        border: Border(right: BorderSide(color: borderColor)),
      ),
      child: SafeArea(
        child: Column(
          children: [
            _buildHeader(accent, isDark),
            const SizedBox(height: 20),
            Expanded(
              child: ListView.separated(
                padding: const EdgeInsets.symmetric(horizontal: 12),
                itemCount: menuItems.length,
                separatorBuilder: (_, __) => const SizedBox(height: 4),
                itemBuilder: (context, index) {
                  final item = menuItems[index];
                  final bool isActive = activeTab == item['id'];
                  return _buildNavTile(item, accent, isDark, isActive, activeTileBg, textColor, iconColor);
                },
              ),
            ),
            _buildOperatorSection(borderColor, isDark, textColor),
          ],
        ),
      ),
    );
  }

  Widget _buildNavTile(Map<String, dynamic> item, Color accent, bool isDark, bool isActive, Color activeBg, Color textColor, Color iconColor) {
    return InkWell(
      onTap: () => setActiveTab(item['id']),
      borderRadius: BorderRadius.circular(12),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        decoration: BoxDecoration(
          color: isActive ? activeBg : Colors.transparent,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Row(
          children: [
            Icon(
              item['icon'],
              size: 22,
              color: isActive ? accent : iconColor,
            ),
            const SizedBox(width: 16),
            Expanded(
              child: Text(
                item['label'],
                style: TextStyle(
                  color: isActive ? (isDark ? accent : Colors.black) : textColor,
                  fontSize: 15,
                  fontWeight: isActive ? FontWeight.bold : FontWeight.w500,
                ),
              ),
            ),
            if (isActive)
              Container(
                width: 6,
                height: 6,
                decoration: BoxDecoration(color: accent, shape: BoxShape.circle),
              ),
          ],
        ),
      ),
    );
  }

  Widget _buildHeader(Color accent, bool isDark) {
    return Padding(
      padding: const EdgeInsets.all(24.0),
      child: Row(
        children: [
          Container(
            padding: const EdgeInsets.all(6),
            decoration: BoxDecoration(color: accent, borderRadius: BorderRadius.circular(8)),
            child: const Icon(Icons.show_chart, color: Colors.white, size: 20),
          ),
          const SizedBox(width: 12),
          Text.rich(
            TextSpan(
              text: 'FarmBot ',
              style: TextStyle(color: isDark ? Colors.white : Colors.black, fontSize: 18, fontWeight: FontWeight.bold),
              children: [
                TextSpan(text: 'Gantry', style: TextStyle(color: accent)),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildOperatorSection(Color borderColor, bool isDark, Color textColor) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(border: Border(top: BorderSide(color: borderColor))),
      child: Row(
        children: [
          CircleAvatar(
            radius: 18,
            backgroundColor: isDark ? const Color(0xFF1F2937) : Colors.grey.shade200,
            child: Text('OP', style: TextStyle(color: isDark ? Colors.white : Colors.black87, fontSize: 12, fontWeight: FontWeight.bold)),
          ),
          const SizedBox(width: 12),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('Operator', style: TextStyle(color: isDark ? Colors.white : Colors.black87, fontWeight: FontWeight.bold)),
              Row(
                children: [
                  Container(width: 8, height: 8, decoration: const BoxDecoration(color: Colors.green, shape: BoxShape.circle)),
                  const SizedBox(width: 6),
                  Text('Connected', style: TextStyle(color: isDark ? Colors.white54 : Colors.grey.shade600, fontSize: 12)),
                ],
              ),
            ],
          ),
        ],
      ),
    );
  }
}