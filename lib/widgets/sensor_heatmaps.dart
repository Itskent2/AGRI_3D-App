// lib/widgets/sensor_heatmaps.dart

import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:file_picker/file_picker.dart';


import '../providers/theme_provider.dart';
import '../service/ESP32/esp32_service.dart';
import '../models/plot_model.dart'; // For gridCols, gridRows

// ── DATA MODEL ──
class NpkLogEntry {
  final int x, y, ts;
  final double n, p, k, m, ec, ph, temp;

  NpkLogEntry({
    required this.x, required this.y, required this.ts,
    required this.n, required this.p, required this.k, required this.m,
    required this.ec, required this.ph, required this.temp,
  });
}


enum HeatmapType { Moisture, Nitrogen, Phosphorus, Potassium, EC, pH }


class SensorHeatmaps extends ConsumerStatefulWidget {
  const SensorHeatmaps({super.key});

  @override
  ConsumerState<SensorHeatmaps> createState() => _SensorHeatmapsState();
}

class _SensorHeatmapsState extends ConsumerState<SensorHeatmaps> {
  StreamSubscription? _npkSub;
  
  final List<NpkLogEntry> _history = [];
  bool _isLoadingHistory = true;
  
  // Playback state
  bool _isPlaying = false;
  double _playbackTime = 0; 
  double _minTime = 0;
  double _maxTime = 0;
  Timer? _playTimer;

  @override
  void initState() {
    super.initState();

    _loadLocalHistory().then((_) {
      _npkSub = ESP32Service.instance.onNpkUpdate.listen(_onData);
      
      // Request full log history for today
      if (ESP32Service.instance.isConnected) {
        final now = DateTime.now();
        final dateStr = "${now.year}${now.month.toString().padLeft(2, '0')}${now.day.toString().padLeft(2, '0')}";
        ESP32Service.instance.sendCommand("GET_NPK_LOG:$dateStr");
      } else {
        setState(() => _isLoadingHistory = false);
      }
    });
  }

  Future<void> _loadLocalHistory() async {
    final prefs = await SharedPreferences.getInstance();
    final jsonStr = prefs.getString('npk_history');
    if (jsonStr != null) {
      try {
        final list = jsonDecode(jsonStr) as List;
        setState(() {
          for (var r in list) {
            _history.add(NpkLogEntry(
              x: (r['x'] as num).toInt(),
              y: (r['y'] as num).toInt(),
              ts: (r['ts'] as num).toInt(),
              n: (r['n'] as num).toDouble(),
              p: (r['p'] as num).toDouble(),
              k: (r['k'] as num).toDouble(),
              m: (r['m'] as num).toDouble(),
              ec: (r['ec'] ?? 0).toDouble(),
              ph: (r['ph'] ?? 0).toDouble(),
              temp: (r['temp'] ?? 0).toDouble(),
            ));
          }
          _deduplicateAndSortHistory();
        });
      } catch (e) {
        // Ignore parse ecxsv    
      }
    }
  }

  void _deduplicateAndSortHistory() {
    final seen = <int>{};
    final unique = <NpkLogEntry>[];
    for (var entry in _history) {
      if (!seen.contains(entry.ts)) {
        seen.add(entry.ts);
        unique.add(entry);
      }
    }
    _history.clear();
    _history.addAll(unique);
    _history.sort((a, b) => a.ts.compareTo(b.ts));
    
    if (_history.isNotEmpty) {
      _minTime = _history.first.ts.toDouble();
      _maxTime = _history.last.ts.toDouble();
      if (!_isPlaying) _playbackTime = _maxTime;
    }
  }

  Future<void> _saveLocalHistory() async {
    final prefs = await SharedPreferences.getInstance();
    final list = _history.map((e) => {
      'x': e.x, 'y': e.y, 'ts': e.ts,
      'n': e.n, 'p': e.p, 'k': e.k, 'm': e.m,
      'ec': e.ec, 'ph': e.ph, 'temp': e.temp,
    }).toList();
    await prefs.setString('npk_history', jsonEncode(list));
  }

  Future<void> _exportToCSV() async {
    String csv = "Timestamp,GridX,GridY,Moisture,Temp,EC,pH,Nitrogen,Phosphorus,Potassium\n";
    for (var e in _history) {
      final dt = DateTime.fromMillisecondsSinceEpoch(e.ts * 1000);
      csv += "${dt.toIso8601String()},${e.x},${e.y},${e.m},${e.temp},${e.ec},${e.ph},${e.n},${e.p},${e.k}\n";
    }
    
    try {
      final result = await FilePicker.platform.saveFile(
        fileName: 'soil_data_export.csv',
        bytes: utf8.encode(csv),
      );
      if (result != null) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Exported to $result')),
        );
      }
    } catch (e) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Export failed: $e')),
      );
    }
  }


  void _onData(Map<String, dynamic> data) {
    if (!mounted) return;
    try {
      if (data['evt'] == 'NPK_LOG_CHUNK') {
        final readings = data['readings'] as List;
        for (var r in readings) {
          _history.add(NpkLogEntry(
            x: (r['x'] as num).toInt(),
            y: (r['y'] as num).toInt(),
            ts: (r['ts'] as num).toInt(),
            n: (r['n'] as num).toDouble(),
            p: (r['p'] as num).toDouble(),
            k: (r['k'] as num).toDouble(),
            m: (r['m'] as num).toDouble(),
            ec: (r['ec'] ?? 0).toDouble(),
            ph: (r['ph'] ?? 0).toDouble(),
            temp: (r['temp'] ?? 0).toDouble(),
          ));
        }
      } else if (data['evt'] == 'NPK_LOG_END') {
        setState(() {
          _deduplicateAndSortHistory();
          _saveLocalHistory();
          _isLoadingHistory = false;
        });
      } else if (data['evt'] == 'NPK') {
        // Live update - append to history and move slider
        int ts = (data['ts'] as num?)?.toInt() ?? (DateTime.now().millisecondsSinceEpoch ~/ 1000);
        _history.add(NpkLogEntry(
            x: (data['x'] as num).toInt(),
            y: (data['y'] as num).toInt(),
            ts: ts,
            n: (data['n'] as num).toDouble(),
            p: (data['p'] as num).toDouble(),
            k: (data['k'] as num).toDouble(),
            m: (data['m'] ?? -1).toDouble(),
            ec: (data['ec'] ?? 0).toDouble(),
            ph: (data['ph'] ?? 0).toDouble(),
            temp: (data['temp'] ?? 0).toDouble(),
        ));
        setState(() {
          _deduplicateAndSortHistory();
          _saveLocalHistory();
        });
      }

    } catch (e) {
      // Ignore parse errors
    }

  }

  @override
  void dispose() {
    _npkSub?.cancel();
    _playTimer?.cancel();
    super.dispose();
  }

  void _togglePlay() {
    setState(() {
      _isPlaying = !_isPlaying;
      if (_isPlaying) {
        if (_playbackTime >= _maxTime) {
          _playbackTime = _minTime; // Restart if at end
        }
        _playTimer = Timer.periodic(const Duration(milliseconds: 100), (timer) {
          setState(() {
            // Scrub through time (adjust speed based on duration span)
            double step = (_maxTime - _minTime) / 100.0;
            if (step < 1) step = 1;
            
            _playbackTime += step;
            if (_playbackTime >= _maxTime) {
              _playbackTime = _maxTime;
              _isPlaying = false;
              timer.cancel();
            }
          });
        });
      } else {
        _playTimer?.cancel();
      }
    });
  }

  // Generate grid state for the current playback time
  List<List<double>> _getGridAtTime(HeatmapType type) {
    List<List<double>> grid = List.generate(gridRows, (_) => List.filled(gridCols, -1.0));
    
    // Process history chronologically up to _playbackTime
    for (var entry in _history) {
      if (entry.ts > _playbackTime) break;
      if (entry.x >= 0 && entry.x < gridCols && entry.y >= 0 && entry.y < gridRows) {
        switch (type) {
          case HeatmapType.Moisture: grid[entry.y][entry.x] = entry.m; break;
          case HeatmapType.Nitrogen: grid[entry.y][entry.x] = entry.n; break;
          case HeatmapType.Phosphorus: grid[entry.y][entry.x] = entry.p; break;
          case HeatmapType.Potassium: grid[entry.y][entry.x] = entry.k; break;
          case HeatmapType.EC: grid[entry.y][entry.x] = entry.ec; break;
          case HeatmapType.pH: grid[entry.y][entry.x] = entry.ph; break;
        }

      }
    }
    return grid;
  }

  Color _getColor(double v, HeatmapType type) {
    if (v < 0) return const Color(0xFF1C2333); // Empty / No Data
    
    if (type == HeatmapType.Moisture) {
      if (v < 30) return Colors.red.shade400;
      if (v < 60) return Colors.orange.shade400;
      return Colors.blue.shade400;
    } else if (type == HeatmapType.Nitrogen) {
      if (v < 20) return Colors.red.shade400;
      if (v < 50) return Colors.green.shade400;
      return Colors.green.shade700;
    } else if (type == HeatmapType.Phosphorus) {
      if (v < 20) return Colors.red.shade400;
      if (v < 40) return Colors.purple.shade400;
      return Colors.purple.shade700;
    } else if (type == HeatmapType.Potassium) {
      if (v < 20) return Colors.red.shade400;
      if (v < 40) return Colors.amber.shade400;
      return Colors.amber.shade700;
    } else if (type == HeatmapType.EC) {
      if (v < 100) return Colors.red.shade400;
      if (v < 500) return Colors.cyan.shade400;
      return Colors.cyan.shade700;
    } else { // pH
      if (v < 6.0) return Colors.orange.shade400;
      if (v < 7.5) return Colors.green.shade400;
      return Colors.purple.shade400;
    }

  }

  Widget _buildHeatmapCard(HeatmapType type, bool isDark) {
    final grid = _getGridAtTime(type);
    bool hasData = grid.any((row) => row.any((val) => val >= 0));

    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFF1C2333),
        borderRadius: BorderRadius.circular(8),
      ),
      child: Stack(
        children: [
          Positioned.fill(
            child: ClipRRect(
              borderRadius: BorderRadius.circular(8),
              child: CustomPaint(
                painter: _HeatmapPainter(
                  data: grid,
                  getColor: (v) => _getColor(v, type),
                ),
              ),
            ),
          ),
          // Title Label
          Positioned(
            top: 4, left: 6,
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
              decoration: BoxDecoration(
                color: Colors.black.withValues(alpha: 0.6),
                borderRadius: BorderRadius.circular(4),
              ),
              child: Text(
                type.name.toUpperCase(),
                style: const TextStyle(color: Colors.white, fontSize: 8, fontWeight: FontWeight.bold),
              ),
            ),
          ),
          if (!hasData)
            Positioned.fill(
              child: Container(
                color: Colors.black.withValues(alpha: 0.5),
                child: Center(
                  child: Text(
                    "No ${type.name}",
                    style: const TextStyle(color: Colors.white54, fontSize: 10, fontWeight: FontWeight.bold),
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = ref.watch(themeProvider);
    final isDark = theme.isDark(context);
    
    String timeStr = "--:--";
    if (_playbackTime > 0) {
      final dt = DateTime.fromMillisecondsSinceEpoch(_playbackTime.toInt() * 1000);
      timeStr = "${dt.hour.toString().padLeft(2, '0')}:${dt.minute.toString().padLeft(2, '0')}:${dt.second.toString().padLeft(2, '0')}";
    }

    return Container(
      decoration: BoxDecoration(
        color: isDark ? const Color(0xFF1F2937) : Colors.white,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: isDark ? const Color(0xFF374151) : Colors.grey.shade300),
      ),
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                "SOIL HEATMAPS",
                style: TextStyle(
                  color: isDark ? Colors.white : Colors.black87,
                  fontWeight: FontWeight.w900,
                  fontStyle: FontStyle.italic,
                  letterSpacing: 1.0,
                ),
              ),
              Row(
                children: [
                  if (_isLoadingHistory)
                    const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2)),
                  const SizedBox(width: 8),
                  IconButton(
                    icon: Icon(Icons.download, color: isDark ? Colors.white70 : Colors.black54),
                    onPressed: _exportToCSV,
                    tooltip: "Export CSV",
                    constraints: const BoxConstraints(),
                    padding: EdgeInsets.zero,
                  ),
                ],
              )

            ],
          ),
          const SizedBox(height: 16),
          
          // 3x2 Grid of Heatmaps
          SizedBox(
            height: 380,
            child: GridView.count(
              crossAxisCount: 2,
              crossAxisSpacing: 8,
              mainAxisSpacing: 8,
              childAspectRatio: 1.5,
              physics: const NeverScrollableScrollPhysics(),
              children: [
                _buildHeatmapCard(HeatmapType.Moisture, isDark),
                _buildHeatmapCard(HeatmapType.Nitrogen, isDark),
                _buildHeatmapCard(HeatmapType.Phosphorus, isDark),
                _buildHeatmapCard(HeatmapType.Potassium, isDark),
                _buildHeatmapCard(HeatmapType.EC, isDark),
                _buildHeatmapCard(HeatmapType.pH, isDark),
              ],
            ),
          ),

          const SizedBox(height: 16),

          // Playback Controls
          Row(
            children: [
              IconButton(
                icon: Icon(_isPlaying ? Icons.pause_circle_filled : Icons.play_circle_fill),
                color: theme.currentAccentColor,
                iconSize: 32,
                onPressed: _togglePlay,
              ),
              Expanded(
                child: SliderTheme(
                  data: SliderThemeData(
                    trackHeight: 4,
                    thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 6),
                    overlayShape: const RoundSliderOverlayShape(overlayRadius: 14),
                  ),
                  child: Slider(
                    value: _playbackTime,
                    min: _minTime > 0 ? _minTime : 0,
                    max: _maxTime > 0 ? _maxTime : 100,
                    activeColor: theme.currentAccentColor,
                    inactiveColor: isDark ? Colors.grey.shade800 : Colors.grey.shade300,
                    onChanged: (val) {
                      setState(() {
                        _playbackTime = val;
                        _isPlaying = false;
                        _playTimer?.cancel();
                      });
                    },
                  ),
                ),
              ),
              SizedBox(
                width: 60,
                child: Text(
                  timeStr,
                  textAlign: TextAlign.right,
                  style: TextStyle(
                    color: isDark ? Colors.grey.shade400 : Colors.grey.shade600,
                    fontSize: 12,
                    fontFamily: 'monospace',
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

// ── PAINTER ──
class _HeatmapPainter extends CustomPainter {
  final List<List<double>> data;
  final Color Function(double) getColor;

  _HeatmapPainter({required this.data, required this.getColor});

  @override
  void paint(Canvas canvas, Size size) {
    int rows = data.length;
    int cols = data.isNotEmpty ? data[0].length : 0;
    if (rows == 0 || cols == 0) return;

    double w = size.width / cols;
    double h = size.height / rows;

    final paint = Paint();

    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < cols; c++) {
        paint.color = getColor(data[r][c]);
        canvas.drawRect(
          Rect.fromLTWH(c * w + 1, r * h + 1, w - 2, h - 2),
          paint,
        );
      }
    }
  }

  @override
  bool shouldRepaint(covariant _HeatmapPainter oldDelegate) => true;
}