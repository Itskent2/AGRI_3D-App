import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import '../service/ESP32/esp32_live_feed.dart';
import '../service/ESP32/esp32_service.dart';

class LiveScreen extends StatefulWidget {
  final String streamUrl;

  const LiveScreen({super.key, required this.streamUrl});

  @override
  State<LiveScreen> createState() => _LiveScreenState();
}

class _LiveScreenState extends State<LiveScreen> {
  bool isPlaying = true;
  double _currentFpm = 60.0; // Default FPM (1 FPS)
  final ScrollController _consoleScrollController = ScrollController();

  final Set<String> _activeTags = {
    "SYSTEM", "NET", "GRBL", "CAM", "AI", "ROUTINE", "SCAN", "WEED", "ENV", "FERT", "SD", "SENSORS", "CMD", "STATE"
  };

  @override
  void initState() {
    super.initState();
    ESP32Service.instance.addListener(_scrollConsole);
  }

  @override
  void dispose() {
    ESP32Service.instance.removeListener(_scrollConsole);
    _consoleScrollController.dispose();
    super.dispose();
  }

  void _scrollConsole() {
    Future.delayed(const Duration(milliseconds: 50), () {
      if (_consoleScrollController.hasClients) {
        for (var pos in _consoleScrollController.positions) {
          try {
            pos.animateTo(
              pos.maxScrollExtent,
              duration: const Duration(milliseconds: 200),
              curve: Curves.easeOut,
            );
          } catch (_) {}
        }
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final isDark = Theme.of(context).brightness == Brightness.dark;
    final cardColor = isDark ? const Color(0xFF1F2937) : Colors.white;
    final borderColor = isDark ? const Color(0xFF374151) : Colors.grey.shade300;
    final textColor = isDark ? Colors.white70 : Colors.black87;
    final subTextColor = isDark ? Colors.white38 : Colors.black45;

    return ListenableBuilder(
      listenable: ESP32Service.instance,
      builder: (context, _) {
        final service = ESP32Service.instance;

        return SingleChildScrollView(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // ── Section Label ──
              Padding(
                padding: const EdgeInsets.only(bottom: 12),
                child: Text(
                  'LIVE CAMERA FEED',
                  style: TextStyle(
                    fontSize: 11,
                    letterSpacing: 2,
                    fontWeight: FontWeight.bold,
                    color: subTextColor,
                  ),
                ),
              ),

              // ── Camera Feed Card ──
              Container(
                width: double.infinity,
                decoration: BoxDecoration(
                  color: cardColor,
                  borderRadius: BorderRadius.circular(16),
                  border: Border.all(color: borderColor),
                ),
                clipBehavior: Clip.hardEdge,
                child: Column(
                  children: [
                    // The live feed widget (now handles its own aspect ratio)
                    isPlaying
                        ? const Esp32LiveFeed(isDark: true)
                        : AspectRatio(
                            aspectRatio: 4 / 3,
                            child: Container(
                              color: Colors.black,
                              child: const Center(
                                child: Column(
                                  mainAxisAlignment: MainAxisAlignment.center,
                                  children: [
                                    Icon(Icons.pause_circle_outline,
                                        color: Colors.white38, size: 56),
                                    SizedBox(height: 12),
                                    Text(
                                      'Feed Paused',
                                      style: TextStyle(
                                          color: Colors.white38, fontSize: 14),
                                    ),
                                  ],
                                ),
                              ),
                            ),
                    ),

                    // ── Controls Row ──
                    Container(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 20, vertical: 14),
                      decoration: BoxDecoration(
                        border: Border(top: BorderSide(color: borderColor)),
                      ),
                      child: Row(
                        children: [
                          ElevatedButton.icon(
                            style: ElevatedButton.styleFrom(
                              backgroundColor:
                                  isPlaying ? Colors.redAccent : Colors.green,
                              foregroundColor: Colors.white,
                              padding: const EdgeInsets.symmetric(
                                  horizontal: 24, vertical: 10),
                              shape: RoundedRectangleBorder(
                                  borderRadius: BorderRadius.circular(30)),
                            ),
                            onPressed: () {
                              setState(() => isPlaying = !isPlaying);
                            },
                            icon: Icon(
                                isPlaying ? Icons.pause : Icons.play_arrow,
                                size: 18),
                            label:
                                Text(isPlaying ? 'Pause Feed' : 'Resume Feed'),
                          ),
                          const Spacer(),
                          Row(
                            children: [
                              Container(
                                width: 8,
                                height: 8,
                                decoration: BoxDecoration(
                                  color: isPlaying
                                      ? Colors.greenAccent
                                      : Colors.grey,
                                  shape: BoxShape.circle,
                                ),
                              ),
                              const SizedBox(width: 6),
                              Text(
                                isPlaying ? 'Streaming' : 'Stopped',
                                style:
                                    TextStyle(color: textColor, fontSize: 13),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ),

                    // ── FPM Slider ──
                    if (isPlaying)
                      Container(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 20, vertical: 8),
                        decoration: BoxDecoration(
                          border: Border(top: BorderSide(color: borderColor)),
                          color: isDark
                              ? Colors.white.withOpacity(0.02)
                              : Colors.black.withOpacity(0.02),
                        ),
                        child: Row(
                          children: [
                            Icon(Icons.speed, size: 18, color: subTextColor),
                            const SizedBox(width: 8),
                            Text(
                              "Frame Rate:",
                              style: TextStyle(
                                  color: subTextColor, fontSize: 12),
                            ),
                            Expanded(
                              child: Slider(
                                value: _currentFpm,
                                min: 1,
                                max: 600,
                                divisions: 599,
                                activeColor: Colors.blueAccent,
                                inactiveColor:
                                    Colors.blueAccent.withOpacity(0.2),
                                onChanged: (val) {
                                  setState(() => _currentFpm = val);
                                },
                                onChangeEnd: (val) {
                                  ESP32Service.instance.setFPM(val.toInt());
                                },
                              ),
                            ),
                            SizedBox(
                              width: 40,
                              child: Text(
                                "${_currentFpm.toInt()} FPM",
                                style: TextStyle(
                                    color: textColor,
                                    fontSize: 12,
                                    fontWeight: FontWeight.bold),
                                textAlign: TextAlign.right,
                              ),
                            ),
                          ],
                        ),
                      ),
                  ],
                ),
              ),

              const SizedBox(height: 16),

              // ── Filtered Camera Console ──
              _buildCameraConsole(
                  service, isDark, cardColor, borderColor, textColor, subTextColor),

              const SizedBox(height: 16),

              // ── Telemetry Cards ──
              Padding(
                padding: const EdgeInsets.only(bottom: 12),
                child: Text(
                  'SENSOR TELEMETRY',
                  style: TextStyle(
                    fontSize: 11,
                    letterSpacing: 2,
                    fontWeight: FontWeight.bold,
                    color: subTextColor,
                  ),
                ),
              ),
              Row(
                children: [
                  Expanded(
                    child: _buildDataCard(
                        'Soil Moisture',
                        '-- %',
                        Icons.water_drop,
                        Colors.blueAccent,
                        cardColor,
                        borderColor,
                        textColor,
                        subTextColor),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: _buildDataCard(
                        'Nitrogen',
                        '-- mg/kg',
                        Icons.science,
                        Colors.greenAccent,
                        cardColor,
                        borderColor,
                        textColor,
                        subTextColor),
                  ),
                ],
              ),
            ],
          ),
        );
      },
    );
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // FILTERED CAMERA & MOVEMENT CONSOLE
  // ═══════════════════════════════════════════════════════════════════════════

  Widget _buildCameraConsole(
    ESP32Service service,
    bool isDark,
    Color cardColor,
    Color borderColor,
    Color textColor,
    Color subTextColor,
  ) {
    final filteredLogs = _getFilteredLogs(service);

    return Container(
      decoration: BoxDecoration(
        color: cardColor,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: borderColor),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // ── Header ──
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 16, 16, 4),
            child: Row(
              children: [
                const Icon(Icons.videocam, color: Color(0xFF60A5FA), size: 18),
                const SizedBox(width: 8),
                Text(
                  "CAMERA & MOVEMENT LOG",
                  style: TextStyle(
                    color: textColor,
                    fontWeight: FontWeight.w900,
                    fontStyle: FontStyle.italic,
                    fontSize: 12,
                    letterSpacing: 1,
                  ),
                ),
                const Spacer(),
                Container(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                  decoration: BoxDecoration(
                    color: isDark
                        ? Colors.white.withOpacity(0.06)
                        : Colors.black.withOpacity(0.04),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Text(
                    "${filteredLogs.length} entries",
                    style: TextStyle(
                      color: subTextColor,
                      fontSize: 10,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                ),
              ],
            ),
          ),

          // ── Console Section Label ──
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 24, 16, 12),
            child: Text(
              'CAMERA & MOVEMENT LOG',
              style: TextStyle(
                fontSize: 11,
                letterSpacing: 2,
                fontWeight: FontWeight.bold,
                color: subTextColor,
              ),
            ),
          ),

          // ── Interactive Filter Bar ──
          _buildFilterBar(isDark),

          // ── Console Output ──
          Container(
            height: 200,
            margin: const EdgeInsets.symmetric(horizontal: 16),
            padding: const EdgeInsets.all(10),
            decoration: BoxDecoration(
              color: isDark ? const Color(0xFF030712) : const Color(0xFFF8FAFC),
              borderRadius: BorderRadius.circular(8),
              border: Border.all(color: borderColor),
            ),
            child: filteredLogs.isEmpty
                ? Center(
                    child: Text(
                      "No filtered events to show...",
                      style: TextStyle(
                        color: subTextColor,
                        fontSize: 11,
                        fontStyle: FontStyle.italic,
                      ),
                    ),
                  )
                : ListView.builder(
                    controller: _consoleScrollController,
                    itemCount: filteredLogs.length,
                    itemBuilder: (context, i) {
                       final entry = filteredLogs[i];
                      return _buildFilteredLogLine(entry, isDark);
                    },
                  ),
          ),
          const SizedBox(height: 16),
        ],
      ),
    );
  }

  // ── Helper Methods ──────────────────────────────────────────────────

  List<LogEntry> _getFilteredLogs(ESP32Service service) {
    return service.logs.where((l) {
      // Always show errors regardless of tag filter
      if (l.level == LogLevel.error) return true;
      return _activeTags.contains(l.tag);
    }).toList();
  }

  Widget _buildFilterBar(bool isDark) {
    // Curated list for Live Screen (less noise than Control Panel)
    final List<String> allFilters = [
      "SYSTEM", "NET", "GRBL", "CAM", "AI", "ROUTINE", "SCAN", "WEED", "ENV", "FERT", "SD", "SENSORS", "CMD", "STATE", "PING"
    ];

    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
      child: Wrap(
        spacing: 4,
        runSpacing: 4,
        children: allFilters.map((tag) {
          final isActive = _activeTags.contains(tag);
          
          Color tagColor;
          switch (tag) {
            case "CAM": case "SCAN": tagColor = const Color(0xFF60A5FA); break;
            case "GRBL": case "ROUTINE": tagColor = const Color(0xFF34D399); break;
            case "AI": case "WEED": tagColor = const Color(0xFFA855F7); break;
            case "FERT": tagColor = const Color(0xFFEC4899); break;
            case "TX": case "CMD": tagColor = const Color(0xFFFBBF24); break;
            case "RX": tagColor = const Color(0xFF8B5CF6); break;
            case "ENV": tagColor = const Color(0xFF0EA5E9); break;
            case "SD": tagColor = const Color(0xFFF97316); break;
            case "PING": tagColor = const Color(0xFF14B8A6); break;
            default: tagColor = isDark ? Colors.white54 : Colors.grey.shade600;
          }

          final displayColor = isActive ? tagColor : (isDark ? Colors.white12 : Colors.grey.shade200);

          return GestureDetector(
            onTap: () => setState(() => isActive ? _activeTags.remove(tag) : _activeTags.add(tag)),
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 3),
              decoration: BoxDecoration(
                color: displayColor.withValues(alpha: 0.15),
                borderRadius: BorderRadius.circular(4),
              ),
              child: Text(
                tag,
                style: TextStyle(
                  color: displayColor,
                  fontSize: 8,
                  fontWeight: FontWeight.bold,
                  letterSpacing: 0.5,
                ),
              ),
            ),
          );
        }).toList(),
      ),
    );
  }

  Widget _tagBadge(String label, Color color) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
      decoration: BoxDecoration(
        color: color.withOpacity(0.15),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: color.withOpacity(0.4)),
      ),
      child: Text(
        label,
        style: TextStyle(
          color: color,
          fontSize: 8,
          fontWeight: FontWeight.bold,
          letterSpacing: 0.5,
        ),
      ),
    );
  }

  Widget _buildFilteredLogLine(LogEntry entry, bool isDark) {
    Color color;
    switch (entry.level) {
      case LogLevel.error:
        color = const Color(0xFFEF4444);
        break;
      case LogLevel.warn:
        color = const Color(0xFFFBBF24);
        break;
      case LogLevel.success:
        color = const Color(0xFF34D399);
        break;
      default:
        if (entry.tag == "CAM" || entry.tag == "SCAN") color = const Color(0xFF60A5FA);
        else if (entry.tag == "TX") color = const Color(0xFFFBBF24);
        else color = isDark ? Colors.white54 : Colors.grey.shade600;
    }

    final time = DateFormat('HH:mm:ss').format(entry.time);
    final display = entry.message;
    
    // UI Styling
    final lineColor = color;
    final prefix = entry.level == LogLevel.error ? "✘" : "•";

    return Padding(
      padding: const EdgeInsets.only(bottom: 2),
      child: Text(
        "$time $prefix $display",
        style: TextStyle(
          color: lineColor,
          fontSize: 11,
          fontFamily: 'monospace',
          height: 1.4,
        ),
        maxLines: 2,
        overflow: TextOverflow.ellipsis,
      ),
    );
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // DATA CARD
  // ═══════════════════════════════════════════════════════════════════════════

  Widget _buildDataCard(
    String label,
    String value,
    IconData icon,
    Color accentColor,
    Color cardColor,
    Color borderColor,
    Color textColor,
    Color subTextColor,
  ) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: cardColor,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: borderColor),
      ),
      child: Row(
        children: [
          Container(
            padding: const EdgeInsets.all(10),
            decoration: BoxDecoration(
              color: accentColor.withValues(alpha: 0.15),
              borderRadius: BorderRadius.circular(10),
            ),
            child: Icon(icon, color: accentColor, size: 22),
          ),
          const SizedBox(width: 14),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(value,
                    style: TextStyle(
                        fontSize: 20,
                        fontWeight: FontWeight.bold,
                        color: textColor)),
                Text(label,
                    style: TextStyle(color: subTextColor, fontSize: 12)),
              ],
            ),
          ),
        ],
      ),
    );
  }
}