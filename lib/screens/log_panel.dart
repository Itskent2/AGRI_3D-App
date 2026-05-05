// lib/screens/log_panel.dart

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:lucide_icons/lucide_icons.dart';
import '../providers/theme_provider.dart'; // 👈 Pointing to your main theme provider
import '../models/log_model.dart'; 
import '../models/log_model.dart' as mock; 

class LogPanel extends ConsumerWidget {
  final List<LogEntry>? logs;

  const LogPanel({super.key, this.logs});

  IconData _iconFor(LogType type) {
    switch (type) {
      case LogType.info: return LucideIcons.info;
      case LogType.alert: return LucideIcons.alertCircle;
      case LogType.success: return LucideIcons.checkCircle2;
    }
  }

  Color _colorFor(LogType type) {
    switch (type) {
      case LogType.info: return Colors.blue;
      case LogType.alert: return Colors.red;
      case LogType.success: return Colors.green;
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // ── Theme Awareness ──
    final themeState = ref.watch(themeProvider);
    final isDark = themeState.isDark(context);
    final accent = themeState.currentAccentColor;

    // Visibility-aware colors
    final bgColor = isDark ? const Color(0xFF0B0F1A) : const Color(0xFFF1F5F9);
    final cardColor = isDark ? const Color(0xFF1F2937) : Colors.white;
    final primaryText = isDark ? Colors.white : const Color(0xFF111827);
    final subText = isDark ? const Color(0xFF9CA3AF) : const Color(0xFF4B5563);
    final borderColor = isDark ? const Color(0xFF374151) : Colors.grey.shade300;
    
    final displayLogs = (logs != null && logs!.isNotEmpty) ? logs! : mock.logs; 

    return Container(
      color: bgColor, // 👈 Dynamic background
      padding: const EdgeInsets.all(24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // ── Header ──
          Wrap(
            alignment: WrapAlignment.spaceBetween,
            crossAxisAlignment: WrapCrossAlignment.center,
            spacing: 16,
            runSpacing: 12,
            children: [
              RichText(
                text: TextSpan(
                  style: TextStyle(
                    fontSize: 20,
                    fontWeight: FontWeight.bold,
                    color: primaryText, // 👈 Dynamic text color
                    fontStyle: FontStyle.italic,
                    letterSpacing: -0.5,
                  ),
                  children: [
                    const TextSpan(text: 'EVENT '),
                    TextSpan(text: 'HISTORY', style: TextStyle(color: accent)),
                  ],
                ),
              ),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  OutlinedButton(
                    onPressed: () {
                      if (displayLogs.isEmpty) return;
                      final textToCopy = displayLogs.join('\n');
                      Clipboard.setData(ClipboardData(text: textToCopy));
                      ScaffoldMessenger.of(context).showSnackBar(
                        SnackBar(
                          content: Text('Copied ${displayLogs.length} logs to clipboard'),
                          backgroundColor: accent,
                        ),
                      );
                    },
                    style: OutlinedButton.styleFrom(
                      backgroundColor: cardColor,
                      side: BorderSide(color: borderColor),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
                    ),
                    child: Text(
                      'EXPORT LOGS',
                      style: TextStyle(fontSize: 10, fontWeight: FontWeight.bold, color: subText, letterSpacing: 1.5),
                    ),
                  ),
                  ElevatedButton(
                    onPressed: () {},
                    style: ElevatedButton.styleFrom(
                      backgroundColor: accent.withOpacity(0.1),
                      foregroundColor: accent,
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
                      side: BorderSide(color: accent.withOpacity(0.3)),
                      elevation: 0,
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
                    ),
                    child: const Text('CLEAR ALL', style: TextStyle(fontSize: 10, fontWeight: FontWeight.bold, letterSpacing: 1.5)),
                  ),
                ],
              ),
            ],
          ),

          const SizedBox(height: 24),

          // ── Log List ──
          Expanded(
            child: ListView.builder(
              itemCount: displayLogs.length,
              itemBuilder: (ctx, i) {
                final log = displayLogs[i];
                final icon = _iconFor(log.type);
                final logColor = _colorFor(log.type);

                return Container(
                  margin: const EdgeInsets.only(bottom: 12),
                  padding: const EdgeInsets.all(16),
                  decoration: BoxDecoration(
                    color: cardColor, // 👈 Dynamic card background
                    borderRadius: BorderRadius.circular(12),
                    border: Border.all(color: borderColor),
                  ),
                  child: Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Container(
                        padding: const EdgeInsets.all(8),
                        decoration: BoxDecoration(
                          color: logColor.withOpacity(0.1),
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: Icon(icon, size: 18, color: logColor),
                      ),
                      const SizedBox(width: 16),
                      
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Row(
                              mainAxisAlignment: MainAxisAlignment.spaceBetween,
                              children: [
                                Text(
                                  log.type.name.toUpperCase(),
                                  style: TextStyle(fontSize: 10, color: logColor, fontWeight: FontWeight.bold, letterSpacing: 1.5),
                                ),
                                Text(
                                  log.time,
                                  style: TextStyle(fontSize: 10, color: subText, fontFamily: "monospace"),
                                ),
                              ],
                            ),
                            const SizedBox(height: 4),
                            Text(
                              log.message,
                              style: TextStyle(fontSize: 13, color: primaryText, fontWeight: FontWeight.w500, height: 1.5),
                            ),
                            const SizedBox(height: 8),
                            Container(
                              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                              decoration: BoxDecoration(
                                color: isDark ? const Color(0xFF111827) : Colors.grey.shade200,
                                borderRadius: BorderRadius.circular(4),
                              ),
                              child: Text(
                                "@${log.user}",
                                style: TextStyle(fontSize: 10, color: subText, fontFamily: 'monospace', fontStyle: FontStyle.italic),
                              ),
                            )
                          ],
                        ),
                      )
                    ],
                  ),
                );
              },
            ),
          ),
        ],
      ),
    );
  }
}