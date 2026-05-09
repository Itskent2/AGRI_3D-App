import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:intl/intl.dart';
import 'dart:ui' as ui;
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
  
  bool _isBusy = false;
  DateTime? _lastMoveTime;

  final Set<String> _activeTags = {
    "SYSTEM",
    "NET",
    "GRBL",
    "CAM",
    "AI",
    "ROUTINE",
    "SCAN",
    "WEED",
    "ENV",
    "FERT",
    "SD",
    "SENSORS",
    "CMD",
    "STATE",
  };

  @override
  void initState() {
    super.initState();
    ESP32Service.instance.addListener(_scrollConsole);
    ESP32Service.instance.addListener(_onServiceUpdate);
  }

  @override
  void dispose() {
    ESP32Service.instance.removeListener(_scrollConsole);
    ESP32Service.instance.removeListener(_onServiceUpdate);
    _consoleScrollController.dispose();
    super.dispose();
  }

  void _onServiceUpdate() {
    if (!mounted) return;
    final service = ESP32Service.instance;

    if (_isBusy && service.machineState == 'Idle') {
      setState(() {
        _isBusy = false;
      });
    }
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
                        ? Stack(
                            children: [
                              const Esp32LiveFeed(isDark: true),
                              ValueListenableBuilder<
                                List<Map<String, dynamic>>
                              >(
                                valueListenable:
                                    ESP32Service.instance.aiDetections,
                                builder: (context, detections, child) {
                                  return Positioned.fill(
                                    child: CustomPaint(
                                      painter: DetectionPainter(detections),
                                    ),
                                  );
                                },
                              ),
                            ],
                          )
                        : AspectRatio(
                            aspectRatio: 4 / 3,
                            child: Container(
                              color: Colors.black,
                              child: const Center(
                                child: Column(
                                  mainAxisAlignment: MainAxisAlignment.center,
                                  children: [
                                    Icon(
                                      Icons.pause_circle_outline,
                                      color: Colors.white38,
                                      size: 56,
                                    ),
                                    SizedBox(height: 12),
                                    Text(
                                      'Feed Paused',
                                      style: TextStyle(
                                        color: Colors.white38,
                                        fontSize: 14,
                                      ),
                                    ),
                                  ],
                                ),
                              ),
                            ),
                          ),

                    // ── Controls Row ──
                    Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 20,
                        vertical: 14,
                      ),
                      decoration: BoxDecoration(
                        border: Border(top: BorderSide(color: borderColor)),
                      ),
                      child: Row(
                        children: [
                          ElevatedButton.icon(
                            style: ElevatedButton.styleFrom(
                              backgroundColor: isPlaying
                                  ? Colors.redAccent
                                  : Colors.green,
                              foregroundColor: Colors.white,
                              padding: const EdgeInsets.symmetric(
                                horizontal: 24,
                                vertical: 10,
                              ),
                              shape: RoundedRectangleBorder(
                                borderRadius: BorderRadius.circular(30),
                              ),
                            ),
                            onPressed: () {
                              setState(() => isPlaying = !isPlaying);
                            },
                            icon: Icon(
                              isPlaying ? Icons.pause : Icons.play_arrow,
                              size: 18,
                            ),
                            label: Text(
                              isPlaying ? 'Pause Feed' : 'Resume Feed',
                            ),
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
                                style: TextStyle(
                                  color: textColor,
                                  fontSize: 13,
                                ),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ),

                    // ── Camera Settings (Resolution & FPM) ──
                    if (isPlaying) ...[
                      Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 20,
                          vertical: 8,
                        ),
                        decoration: BoxDecoration(
                          border: Border(top: BorderSide(color: borderColor)),
                          color: isDark
                              ? Colors.white.withOpacity(0.02)
                              : Colors.black.withOpacity(0.02),
                        ),
                        child: Row(
                          children: [
                            Icon(
                              Icons.photo_size_select_large,
                              size: 18,
                              color: subTextColor,
                            ),
                            const SizedBox(width: 8),
                            Text(
                              "Resolution:",
                              style: TextStyle(
                                color: subTextColor,
                                fontSize: 12,
                              ),
                            ),
                            const SizedBox(width: 16),
                            Expanded(
                              child: DropdownButton<int>(
                                value: service.resolution,
                                isExpanded: true,
                                underline: const SizedBox(),
                                style: TextStyle(
                                  color: textColor,
                                  fontSize: 13,
                                ),
                                dropdownColor: cardColor,
                                items: const [
                                  DropdownMenuItem(
                                    value: 1,
                                    child: Text("QQVGA (160x120)"),
                                  ),
                                  DropdownMenuItem(
                                    value: 5,
                                    child: Text("QVGA (320x240)"),
                                  ),
                                  DropdownMenuItem(
                                    value: 8,
                                    child: Text("VGA (640x480)"),
                                  ),
                                  DropdownMenuItem(
                                    value: 9,
                                    child: Text("SVGA (800x600)"),
                                  ),
                                  DropdownMenuItem(
                                    value: 13,
                                    child: Text("UXGA (1600x1200)"),
                                  ),
                                ],
                                onChanged: (val) {
                                  if (val != null) {
                                    service.setResolution(val);
                                  }
                                },
                              ),
                            ),
                          ],
                        ),
                      ),
                      Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 20,
                          vertical: 8,
                        ),
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
                                color: subTextColor,
                                fontSize: 12,
                              ),
                            ),
                            Expanded(
                              child: Slider(
                                value: _currentFpm,
                                min: 1,
                                max: 600,
                                divisions: 599,
                                activeColor: Colors.blueAccent,
                                inactiveColor: Colors.blueAccent.withOpacity(
                                  0.2,
                                ),
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
                                  fontWeight: FontWeight.bold,
                                ),
                                textAlign: TextAlign.right,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ],
                ),
              ),

              const SizedBox(height: 16),

              // ── Manual Jog Card ──
              Container(
                width: double.infinity,
                padding: const EdgeInsets.all(16),
                decoration: BoxDecoration(
                  color: cardColor,
                  borderRadius: BorderRadius.circular(16),
                  border: Border.all(color: borderColor),
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'MANUAL JOG',
                      style: TextStyle(
                        fontSize: 11,
                        letterSpacing: 2,
                        fontWeight: FontWeight.bold,
                        color: subTextColor,
                      ),
                    ),
                    const SizedBox(height: 16),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        // D-Pad
                        Column(
                          children: [
                            _buildJogButton(Icons.arrow_upward, () => _move('Y', 10.0), isDark),
                            Row(
                              children: [
                                _buildJogButton(Icons.arrow_back, () => _move('X', -10.0), isDark),
                                const SizedBox(width: 40),
                                _buildJogButton(Icons.arrow_forward, () => _move('X', 10.0), isDark),
                              ],
                            ),
                            _buildJogButton(Icons.arrow_downward, () => _move('Y', -10.0), isDark),
                          ],
                        ),
                        const SizedBox(width: 60),
                        // Z Controls
                        Column(
                          children: [
                            Text('Z Axis', style: TextStyle(color: subTextColor, fontSize: 12)),
                            const SizedBox(height: 8),
                            _buildJogButton(Icons.keyboard_arrow_up, () => _move('Z', 5.0), isDark),
                            const SizedBox(height: 8),
                            _buildJogButton(Icons.keyboard_arrow_down, () => _move('Z', -5.0), isDark),
                          ],
                        ),
                      ],
                    ),
                  ],
                ),
              ),

              const SizedBox(height: 16),

              // ── Filtered Camera Console ──
              _buildCameraConsole(
                service,
                isDark,
                cardColor,
                borderColor,
                textColor,
                subTextColor,
              ),

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
                      subTextColor,
                    ),
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
                      subTextColor,
                    ),
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
                  padding: const EdgeInsets.symmetric(
                    horizontal: 8,
                    vertical: 3,
                  ),
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

  // ── Manual Jog Methods ──────────────────────────────────────────────

  void _move(String axis, double delta) {
    if (_isBusy) return;

    final now = DateTime.now();
    if (_lastMoveTime != null &&
        now.difference(_lastMoveTime!).inMilliseconds < 200) {
      return;
    }
    _lastMoveTime = now;

    final service = ESP32Service.instance;

    if (service.machineState == 'Alarm') {
      service.addLog("SYS: Move rejected. Machine not homed.");
      HapticFeedback.heavyImpact();
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Motors locked! Press "HOME ALL" to unlock.'),
          backgroundColor: Color(0xFFEF4444),
          duration: Duration(seconds: 2),
        ),
      );
      return;
    }

    double nextVal = 0;
    double currentMax = 1000.0;
    double minLimit = 0;
    double maxLimit = 1000.0;

    switch (axis.toLowerCase()) {
      case 'x':
        nextVal = service.x + delta;
        currentMax = service.maxX;
        minLimit = 0;
        maxLimit = currentMax;
        break;
      case 'y':
        nextVal = service.y + delta;
        currentMax = service.maxY;
        minLimit = 0;
        maxLimit = currentMax;
        break;
      case 'z':
        nextVal = service.z + delta;
        currentMax = service.maxZ;
        minLimit = 5.0;
        maxLimit = currentMax - 5.0;
        break;
    }

    if (nextVal < minLimit || nextVal > maxLimit) {
      _showLimitWarning(axis, nextVal < minLimit ? "Minimum" : "Maximum", maxLimit);
      service.addLog("SYS: Boundary limit reached on $axis");
      HapticFeedback.heavyImpact();
      return;
    }

    setState(() => _isBusy = true);

    // Format command exactly like control_panel.dart for compatibility
    final gcode = "\$J=G21G91${axis.toUpperCase()}${delta.toStringAsFixed(1)}F1000";
    service.sendCommand(gcode);
    HapticFeedback.lightImpact();
  }

  void _showLimitWarning(String axis, String limitType, double maxLimit) {
    final isDark = Theme.of(context).brightness == Brightness.dark;
    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        backgroundColor: isDark ? const Color(0xFF1F2937) : Colors.white,
        shape: RoundedRectangleBorder(
          side: const BorderSide(color: Color(0xFFF59E0B), width: 2),
          borderRadius: BorderRadius.circular(16),
        ),
        title: Row(
          children: [
            const Icon(
              Icons.warning_amber_rounded,
              color: Color(0xFFF59E0B),
              size: 28,
            ),
            const SizedBox(width: 12),
            Text(
              '${axis.toUpperCase()}-Axis Limit',
              style: TextStyle(
                color: isDark ? Colors.white : Colors.black,
                fontSize: 18,
                fontWeight: FontWeight.bold,
              ),
            ),
          ],
        ),
        content: Text(
          'Movement blocked. You have reached the $limitType physical limit for the ${axis.toUpperCase()} axis (0mm - ${maxLimit.toStringAsFixed(1)}mm).',
          style: TextStyle(
            color: isDark ? const Color(0xFFD1D5DB) : Colors.grey.shade700,
            height: 1.5,
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            style: TextButton.styleFrom(
              backgroundColor: isDark
                  ? const Color(0xFF374151)
                  : Colors.grey.shade200,
              foregroundColor: isDark ? Colors.white : Colors.black,
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(8),
              ),
            ),
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }

  Widget _buildJogButton(IconData icon, VoidCallback onPressed, bool isDark) {
    return Material(
      color: isDark ? Colors.white.withOpacity(0.05) : Colors.black.withOpacity(0.05),
      borderRadius: BorderRadius.circular(12),
      child: InkWell(
        onTap: onPressed,
        borderRadius: BorderRadius.circular(12),
        child: Container(
          width: 44,
          height: 44,
          alignment: Alignment.center,
          child: Icon(icon, color: isDark ? Colors.white : Colors.black),
        ),
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
      "SYSTEM",
      "NET",
      "GRBL",
      "CAM",
      "AI",
      "ROUTINE",
      "SCAN",
      "WEED",
      "ENV",
      "FERT",
      "SD",
      "SENSORS",
      "CMD",
      "STATE",
      "PING",
    ];

    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
      child: Wrap(
        spacing: 4,
        runSpacing: 4,
        children: [
          // ALL Chip
          GestureDetector(
            onTap: () => setState(() => _activeTags.addAll(allFilters)),
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 3),
              decoration: BoxDecoration(
                color:
                    (_activeTags.length == allFilters.length
                            ? const Color(0xFF10B981)
                            : (isDark ? Colors.white12 : Colors.grey.shade200))
                        .withValues(alpha: 0.15),
                borderRadius: BorderRadius.circular(4),
              ),
              child: Text(
                "ALL",
                style: TextStyle(
                  color: _activeTags.length == allFilters.length
                      ? const Color(0xFF10B981)
                      : (isDark ? Colors.white54 : Colors.grey.shade600),
                  fontSize: 8,
                  fontWeight: FontWeight.bold,
                  letterSpacing: 0.5,
                ),
              ),
            ),
          ),
          // NONE Chip
          GestureDetector(
            onTap: () => setState(() => _activeTags.clear()),
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 3),
              decoration: BoxDecoration(
                color:
                    (_activeTags.isEmpty
                            ? const Color(0xFFEF4444)
                            : (isDark ? Colors.white12 : Colors.grey.shade200))
                        .withValues(alpha: 0.15),
                borderRadius: BorderRadius.circular(4),
              ),
              child: Text(
                "NONE",
                style: TextStyle(
                  color: _activeTags.isEmpty
                      ? const Color(0xFFEF4444)
                      : (isDark ? Colors.white54 : Colors.grey.shade600),
                  fontSize: 8,
                  fontWeight: FontWeight.bold,
                  letterSpacing: 0.5,
                ),
              ),
            ),
          ),
          ...allFilters.map((tag) {
            final isActive = _activeTags.contains(tag);

            Color tagColor;
            switch (tag) {
              case "CAM":
              case "SCAN":
                tagColor = const Color(0xFF60A5FA);
                break;
              case "GRBL":
              case "ROUTINE":
                tagColor = const Color(0xFF34D399);
                break;
              case "AI":
              case "WEED":
                tagColor = const Color(0xFFA855F7);
                break;
              case "FERT":
                tagColor = const Color(0xFFEC4899);
                break;
              case "TX":
              case "CMD":
                tagColor = const Color(0xFFFBBF24);
                break;
              case "RX":
                tagColor = const Color(0xFF8B5CF6);
                break;
              case "ENV":
                tagColor = const Color(0xFF0EA5E9);
                break;
              case "SD":
                tagColor = const Color(0xFFF97316);
                break;
              case "PING":
                tagColor = const Color(0xFF14B8A6);
                break;
              default:
                tagColor = isDark ? Colors.white54 : Colors.grey.shade600;
            }

            final displayColor = isActive
                ? tagColor
                : (isDark ? Colors.white12 : Colors.grey.shade200);

            return GestureDetector(
              onTap: () => setState(
                () => isActive ? _activeTags.remove(tag) : _activeTags.add(tag),
              ),
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
          }),
        ],
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
        if (entry.tag == "CAM" || entry.tag == "SCAN")
          color = const Color(0xFF60A5FA);
        else if (entry.tag == "TX")
          color = const Color(0xFFFBBF24);
        else
          color = isDark ? Colors.white54 : Colors.grey.shade600;
    }

    final time = DateFormat('HH:mm:ss').format(entry.time);
    final display = entry.message;

    // UI Styling
    final lineColor = color;
    final prefix = entry.level == LogLevel.error ? "✘" : "•";

    return Padding(
      padding: const EdgeInsets.only(bottom: 2),
      child: Text(
        "$time [${entry.tag}] $prefix $display",
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
                Text(
                  value,
                  style: TextStyle(
                    fontSize: 20,
                    fontWeight: FontWeight.bold,
                    color: textColor,
                  ),
                ),
                Text(
                  label,
                  style: TextStyle(color: subTextColor, fontSize: 12),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class DetectionPainter extends CustomPainter {
  final List<Map<String, dynamic>> detections;

  DetectionPainter(this.detections);

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = Colors.red
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.0;

    final textPainter = TextPainter(textDirection: ui.TextDirection.ltr);

    // Scale coordinates based on the current resolution
    double frameWidth = 160.0;
    double frameHeight = 120.0;
    
    final res = ESP32Service.instance.resolution;
    if (res == 5) {
      frameWidth = 320.0;
      frameHeight = 240.0;
    } else if (res == 8) {
      frameWidth = 640.0;
      frameHeight = 480.0;
    } else if (res == 9) {
      frameWidth = 800.0;
      frameHeight = 600.0;
    } else if (res == 13) {
      frameWidth = 1600.0;
      frameHeight = 1200.0;
    }

    final scaleX = size.width / frameWidth;
    final scaleY = size.height / frameHeight;

    for (final det in detections) {
      final x = (det['x'] as num).toDouble() * scaleX;
      final y = (det['y'] as num).toDouble() * scaleY;
      final w = (det['w'] as num).toDouble() * scaleX;
      final h = (det['h'] as num).toDouble() * scaleY;

      // Edge Impulse FOMO outputs center coordinates!
      // Often, width and height are 0. We'll default to a 40x40 box in original camera pixels.
      final rectW = w > 0 ? w : (40.0 * scaleX);
      final rectH = h > 0 ? h : (40.0 * scaleY);

      final rect = Rect.fromCenter(
        center: Offset(x, y),
        width: rectW,
        height: rectH,
      );
      canvas.drawRect(rect, paint);

      final label = det['label'] ?? 'Unknown';
      final conf = det['conf'] != null
          ? (det['conf'] as num).toStringAsFixed(2)
          : '';

      textPainter.text = TextSpan(
        text: '$label $conf',
        style: const TextStyle(
          color: Colors.red,
          fontSize: 10,
          fontWeight: FontWeight.bold,
        ),
      );
      textPainter.layout();
      textPainter.paint(canvas, Offset(x, y - 12));
    }
  }

  @override
  bool shouldRepaint(covariant DetectionPainter oldDelegate) {
    return oldDelegate.detections != detections;
  }
}
