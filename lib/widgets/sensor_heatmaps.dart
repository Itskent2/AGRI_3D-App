// lib/widgets/sensor_heatmaps.dart

import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:file_picker/file_picker.dart';
import 'dart:convert';

import '../providers/theme_provider.dart';
import '../service/ESP32/esp32_service.dart';

// ── CONFIG ──────────────────────────────────────────────────────────────────
// Fine-grained grid rendered over the 1000×1000mm bed for smooth gradients
const int _gridSize = 50;
// Physical bed dimensions (mm) — grid cell = 20mm²
const double _bedMm = 1000.0;
const double _cellSize = _bedMm / _gridSize;

// ── HEATMAP TYPE ─────────────────────────────────────────────────────────────
enum HeatmapType { Moisture, Nitrogen, Phosphorus, Potassium }

// ── COLOR STOP ───────────────────────────────────────────────────────────────
class _ColorStop {
  final Color color;
  final String value;
  final String label;
  const _ColorStop({required this.color, required this.value, this.label = ''});
}

// ── IDW INTERPOLATION ────────────────────────────────────────────────────────
List<List<double>> _buildInterpolatedGrid(
  List<NpkLogEntry> history,
  double playbackTime,
  HeatmapType type,
) {
  final grid = List.generate(_gridSize, (_) => List.filled(_gridSize, -1.0));

  // Collect the latest reading per dip coordinate up to playbackTime
  final knownPoints = <NpkLogEntry>[];
  for (final e in history) {
    if (e.ts > playbackTime) break;
    knownPoints.removeWhere((p) => p.x == e.x && p.y == e.y);
    knownPoints.add(e);
  }
  if (knownPoints.isEmpty) return grid;

  double _val(NpkLogEntry e) {
    switch (type) {
      case HeatmapType.Moisture:    return e.m;
      case HeatmapType.Nitrogen:    return e.n;
      case HeatmapType.Phosphorus:  return e.p;
      case HeatmapType.Potassium:   return e.k;
    }
  }

  // Map dip mm-coords → 50×50 grid indices
  final pts = knownPoints
      .map((e) => (
            row: (e.y / _cellSize).clamp(0, _gridSize - 1).toInt(),
            col: (e.x / _cellSize).clamp(0, _gridSize - 1).toInt(),
            val: _val(e),
          ))
      .where((p) => p.val >= 0)
      .toList();

  if (pts.isEmpty) return grid;

  if (pts.length == 1) {
    // Single point → solid fill
    for (int r = 0; r < _gridSize; r++) {
      for (int c = 0; c < _gridSize; c++) {
        grid[r][c] = pts.first.val;
      }
    }
    return grid;
  }

  // IDW (power = 2) across all cells
  const p = 2.0;
  for (int r = 0; r < _gridSize; r++) {
    for (int c = 0; c < _gridSize; c++) {
      double num = 0, den = 0;
      for (final pt in pts) {
        final dist = math.sqrt(math.pow(c - pt.col, 2) + math.pow(r - pt.row, 2));
        if (dist == 0) { grid[r][c] = pt.val; num = double.nan; break; }
        final w = 1.0 / math.pow(dist, p);
        num += w * pt.val;
        den += w;
      }
      if (!num.isNaN && den > 0) grid[r][c] = num / den;
    }
  }
  return grid;
}

// ── WIDGET ───────────────────────────────────────────────────────────────────
class SensorHeatmaps extends ConsumerStatefulWidget {
  const SensorHeatmaps({super.key});
  @override
  ConsumerState<SensorHeatmaps> createState() => _SensorHeatmapsState();
}

class _SensorHeatmapsState extends ConsumerState<SensorHeatmaps> {
  bool _isPlaying = false;
  double _playbackTime = 0;
  double _minTime = 0;
  double _maxTime = 0;

  @override
  void initState() {
    super.initState();
    ESP32Service.instance.addListener(_onUpdate);
    _syncTimeBounds();
    if (ESP32Service.instance.isConnected && ESP32Service.instance.npkHistory.isEmpty) {
      final now = DateTime.now();
      final d = "${now.year}${now.month.toString().padLeft(2,'0')}${now.day.toString().padLeft(2,'0')}";
      ESP32Service.instance.sendCommand("GET_NPK_LOG:$d");
    }
  }

  @override
  void dispose() {
    ESP32Service.instance.removeListener(_onUpdate);
    super.dispose();
  }

  void _onUpdate() {
    if (mounted) setState(() => _syncTimeBounds());
  }

  void _syncTimeBounds() {
    final h = ESP32Service.instance.npkHistory;
    if (h.isNotEmpty) {
      _minTime = h.first.ts.toDouble();
      _maxTime = h.last.ts.toDouble();
      if (!_isPlaying) _playbackTime = _maxTime;
    }
  }

  Future<void> _exportCSV() async {
    final h = ESP32Service.instance.npkHistory;
    var csv = "Timestamp,GridX,GridY,Moisture,Temp,EC,pH,Nitrogen,Phosphorus,Potassium\n";
    for (final e in h) {
      final dt = DateTime.fromMillisecondsSinceEpoch(e.ts * 1000);
      csv += "${dt.toIso8601String()},${e.x},${e.y},${e.m},${e.temp},${e.ec},${e.ph},${e.n},${e.p},${e.k}\n";
    }
    try {
      final result = await FilePicker.platform.saveFile(
        fileName: 'soil_data_export.csv',
        bytes: utf8.encode(csv),
      );
      if (result != null && mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Exported to $result')));
      }
    } catch (_) {}
  }

  // ── Time-slider playback ─────────────────────────────────────────────────
  void _togglePlay() {
    setState(() {
      _isPlaying = !_isPlaying;
      if (_isPlaying) {
        if (_playbackTime >= _maxTime) _playbackTime = _minTime;
        _runPlayback();
      }
    });
  }

  void _runPlayback() {
    if (!_isPlaying || !mounted) return;
    final step = ((_maxTime - _minTime) / 100.0).clamp(1.0, double.infinity);
    setState(() {
      _playbackTime += step;
      if (_playbackTime >= _maxTime) {
        _playbackTime = _maxTime;
        _isPlaying = false;
        return;
      }
    });
    Future.delayed(const Duration(milliseconds: 100), _runPlayback);
  }

  // ── Color maps ───────────────────────────────────────────────────────────
  static const _moistureStops = [
    _ColorStop(color: Color(0xFF7F1D1D), value: '0%',   label: 'Dry'),
    _ColorStop(color: Color(0xFFDC2626), value: '20%'),
    _ColorStop(color: Color(0xFFF97316), value: '40%'),
    _ColorStop(color: Color(0xFF3B82F6), value: '60%'),
    _ColorStop(color: Color(0xFF1D4ED8), value: '80%',  label: 'Wet'),
  ];
  static const _nitrogenStops = [
    _ColorStop(color: Color(0xFF7F1D1D), value: '0',    label: 'None'),
    _ColorStop(color: Color(0xFFDC2626), value: '50'),
    _ColorStop(color: Color(0xFF16A34A), value: '150'),
    _ColorStop(color: Color(0xFF14532D), value: '300',  label: 'Rich'),
  ];
  static const _phosphorusStops = [
    _ColorStop(color: Color(0xFF581C87), value: '0',    label: 'None'),
    _ColorStop(color: Color(0xFF9333EA), value: '50'),
    _ColorStop(color: Color(0xFFC084FC), value: '150'),
    _ColorStop(color: Color(0xFFF0ABFC), value: '200',  label: 'High'),
  ];
  static const _potassiumStops = [
    _ColorStop(color: Color(0xFF713F12), value: '0',    label: 'None'),
    _ColorStop(color: Color(0xFFD97706), value: '100'),
    _ColorStop(color: Color(0xFFFBBF24), value: '250'),
    _ColorStop(color: Color(0xFFFEF3C7), value: '400',  label: 'Rich'),
  ];

  List<_ColorStop> _stops(HeatmapType t) {
    switch (t) {
      case HeatmapType.Moisture:   return _moistureStops;
      case HeatmapType.Nitrogen:   return _nitrogenStops;
      case HeatmapType.Phosphorus: return _phosphorusStops;
      case HeatmapType.Potassium:  return _potassiumStops;
    }
  }

  double _maxVal(HeatmapType t) {
    switch (t) {
      case HeatmapType.Moisture:   return 100;
      case HeatmapType.Nitrogen:   return 300;
      case HeatmapType.Phosphorus: return 200;
      case HeatmapType.Potassium:  return 400;
    }
  }

  String _unit(HeatmapType t) {
    switch (t) {
      case HeatmapType.Moisture:   return '%';
      default:                     return ' mg/kg';
    }
  }

  Color _colorForValue(double v, HeatmapType t) {
    if (v < 0) return const Color(0xFF111827);
    final stops = _stops(t);
    final maxV = _maxVal(t);
    final frac = (v / maxV).clamp(0.0, 1.0);
    final seg = frac * (stops.length - 1);
    final lo = seg.floor().clamp(0, stops.length - 2);
    final hi = lo + 1;
    return Color.lerp(stops[lo].color, stops[hi].color, seg - lo)!;
  }

  // ── Build ────────────────────────────────────────────────────────────────
  @override
  Widget build(BuildContext context) {
    final theme = ref.watch(themeProvider);
    final isDark = theme.isDark(context);
    final accent = theme.currentAccentColor;
    final history = ESP32Service.instance.npkHistory;
    final hasData = history.isNotEmpty;

    String timeStr = '--:--:--';
    if (_playbackTime > 0) {
      final dt = DateTime.fromMillisecondsSinceEpoch(_playbackTime.toInt() * 1000);
      timeStr = "${dt.hour.toString().padLeft(2,'0')}:${dt.minute.toString().padLeft(2,'0')}:${dt.second.toString().padLeft(2,'0')}";
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
          // ── Header ──
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              RichText(
                text: TextSpan(
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w900,
                    fontStyle: FontStyle.italic,
                    color: isDark ? Colors.white : Colors.black87,
                    letterSpacing: 1.0,
                  ),
                  children: [
                    const TextSpan(text: 'SOIL '),
                    TextSpan(text: 'HEATMAPS', style: TextStyle(color: accent)),
                  ],
                ),
              ),
              Row(children: [
                if (ESP32Service.instance.isLoadingHistory)
                  const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2)),
                const SizedBox(width: 8),
                IconButton(
                  icon: Icon(Icons.download, color: isDark ? Colors.white70 : Colors.black54, size: 18),
                  onPressed: _exportCSV,
                  tooltip: "Export CSV",
                  constraints: const BoxConstraints(),
                  padding: EdgeInsets.zero,
                ),
              ]),
            ],
          ),
          const SizedBox(height: 4),
          Text(
            '1000mm × 1000mm bed · ${_gridSize}×$_gridSize interpolated grid · ${history.length} readings',
            style: TextStyle(fontSize: 9, color: isDark ? Colors.grey.shade500 : Colors.grey.shade600, fontFamily: 'monospace'),
          ),
          const SizedBox(height: 14),

          // ── 2×2 Heatmap grid ──
          SizedBox(
            height: 420,
            child: Row(children: [
              Expanded(child: Column(children: [
                Expanded(child: _HeatmapCard(
                  type: HeatmapType.Moisture,
                  grid: _buildInterpolatedGrid(history, _playbackTime, HeatmapType.Moisture),
                  stops: _stops(HeatmapType.Moisture),
                  unit: _unit(HeatmapType.Moisture),
                  maxVal: _maxVal(HeatmapType.Moisture),
                  colorFor: (v) => _colorForValue(v, HeatmapType.Moisture),
                  hasData: hasData,
                  isDark: isDark,
                  accent: accent,
                )),
                const SizedBox(height: 8),
                Expanded(child: _HeatmapCard(
                  type: HeatmapType.Nitrogen,
                  grid: _buildInterpolatedGrid(history, _playbackTime, HeatmapType.Nitrogen),
                  stops: _stops(HeatmapType.Nitrogen),
                  unit: _unit(HeatmapType.Nitrogen),
                  maxVal: _maxVal(HeatmapType.Nitrogen),
                  colorFor: (v) => _colorForValue(v, HeatmapType.Nitrogen),
                  hasData: hasData,
                  isDark: isDark,
                  accent: accent,
                )),
              ])),
              const SizedBox(width: 8),
              Expanded(child: Column(children: [
                Expanded(child: _HeatmapCard(
                  type: HeatmapType.Phosphorus,
                  grid: _buildInterpolatedGrid(history, _playbackTime, HeatmapType.Phosphorus),
                  stops: _stops(HeatmapType.Phosphorus),
                  unit: _unit(HeatmapType.Phosphorus),
                  maxVal: _maxVal(HeatmapType.Phosphorus),
                  colorFor: (v) => _colorForValue(v, HeatmapType.Phosphorus),
                  hasData: hasData,
                  isDark: isDark,
                  accent: accent,
                )),
                const SizedBox(height: 8),
                Expanded(child: _HeatmapCard(
                  type: HeatmapType.Potassium,
                  grid: _buildInterpolatedGrid(history, _playbackTime, HeatmapType.Potassium),
                  stops: _stops(HeatmapType.Potassium),
                  unit: _unit(HeatmapType.Potassium),
                  maxVal: _maxVal(HeatmapType.Potassium),
                  colorFor: (v) => _colorForValue(v, HeatmapType.Potassium),
                  hasData: hasData,
                  isDark: isDark,
                  accent: accent,
                )),
              ])),
            ]),
          ),

          const SizedBox(height: 14),

          // ── Playback controls ──
          Row(children: [
            IconButton(
              icon: Icon(_isPlaying ? Icons.pause_circle_filled : Icons.play_circle_fill),
              color: accent,
              iconSize: 30,
              onPressed: hasData ? _togglePlay : null,
            ),
            Expanded(
              child: SliderTheme(
                data: SliderThemeData(
                  trackHeight: 3,
                  thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 5),
                  overlayShape: const RoundSliderOverlayShape(overlayRadius: 12),
                ),
                child: Slider(
                  value: _playbackTime.clamp(_minTime > 0 ? _minTime : 0, _maxTime > 0 ? _maxTime : 100),
                  min: _minTime > 0 ? _minTime : 0,
                  max: _maxTime > 0 ? _maxTime : 100,
                  activeColor: accent,
                  inactiveColor: isDark ? Colors.grey.shade800 : Colors.grey.shade300,
                  onChanged: hasData ? (v) => setState(() { _playbackTime = v; _isPlaying = false; }) : null,
                ),
              ),
            ),
            SizedBox(
              width: 68,
              child: Text(
                timeStr,
                textAlign: TextAlign.right,
                style: TextStyle(
                  color: isDark ? Colors.grey.shade400 : Colors.grey.shade600,
                  fontSize: 11,
                  fontFamily: 'monospace',
                  fontWeight: FontWeight.bold,
                ),
              ),
            ),
          ]),
        ],
      ),
    );
  }
}

// ── HEATMAP CARD ─────────────────────────────────────────────────────────────
class _HeatmapCard extends StatefulWidget {
  final HeatmapType type;
  final List<List<double>> grid;
  final List<_ColorStop> stops;
  final String unit;
  final double maxVal;
  final Color Function(double) colorFor;
  final bool hasData;
  final bool isDark;
  final Color accent;

  const _HeatmapCard({
    required this.type,
    required this.grid,
    required this.stops,
    required this.unit,
    required this.maxVal,
    required this.colorFor,
    required this.hasData,
    required this.isDark,
    required this.accent,
  });

  @override
  State<_HeatmapCard> createState() => _HeatmapCardState();
}

class _HeatmapCardState extends State<_HeatmapCard> {
  Offset? _hover;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFF111827),
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: Colors.white.withOpacity(0.06)),
      ),
      padding: const EdgeInsets.all(8),
      child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        // Label row
        Row(children: [
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
            decoration: BoxDecoration(
              color: widget.accent.withOpacity(0.18),
              borderRadius: BorderRadius.circular(4),
              border: Border.all(color: widget.accent.withOpacity(0.4)),
            ),
            child: Text(
              widget.type.name.toUpperCase(),
              style: TextStyle(color: widget.accent, fontSize: 8, fontWeight: FontWeight.w900, letterSpacing: 1.2),
            ),
          ),
          const Spacer(),
          Text(
            '${_gridSize}×$_gridSize',
            style: TextStyle(color: Colors.white24, fontSize: 8, fontFamily: 'monospace'),
          ),
        ]),
        const SizedBox(height: 6),

        // Heatmap canvas with hover
        Expanded(
          child: MouseRegion(
            onHover: (e) => setState(() => _hover = e.localPosition),
            onExit: (_) => setState(() => _hover = null),
            child: ClipRRect(
              borderRadius: BorderRadius.circular(6),
              child: Stack(fit: StackFit.expand, children: [
                CustomPaint(
                  painter: _HeatmapPainter(data: widget.grid, colorFor: widget.colorFor),
                ),
                if (!widget.hasData)
                  Container(
                    color: Colors.black54,
                    child: Center(
                      child: Column(mainAxisSize: MainAxisSize.min, children: [
                        Icon(Icons.water_drop_outlined, color: Colors.white24, size: 20),
                        const SizedBox(height: 4),
                        Text('No data — run Dip NPK',
                          style: const TextStyle(color: Colors.white38, fontSize: 9, fontWeight: FontWeight.bold),
                          textAlign: TextAlign.center,
                        ),
                      ]),
                    ),
                  ),
                if (_hover != null && widget.hasData)
                  CustomPaint(
                    painter: _HoverPainter(
                      hoverPos: _hover!,
                      data: widget.grid,
                      unit: widget.unit,
                      isDark: widget.isDark,
                    ),
                  ),
              ]),
            ),
          ),
        ),

        // Gradient legend
        const SizedBox(height: 6),
        ClipRRect(
          borderRadius: BorderRadius.circular(4),
          child: SizedBox(
            height: 14,
            child: CustomPaint(
              painter: _LegendPainter(stops: widget.stops),
            ),
          ),
        ),
        const SizedBox(height: 3),
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: widget.stops.where((s) => s.label.isNotEmpty).map((s) =>
            Text(s.label, style: const TextStyle(color: Colors.white38, fontSize: 7, fontFamily: 'monospace'))
          ).toList(),
        ),
      ]),
    );
  }
}

// ── PAINTERS ─────────────────────────────────────────────────────────────────
class _HeatmapPainter extends CustomPainter {
  final List<List<double>> data;
  final Color Function(double) colorFor;
  _HeatmapPainter({required this.data, required this.colorFor});

  @override
  void paint(Canvas canvas, Size size) {
    final rows = data.length;
    final cols = rows > 0 ? data[0].length : 0;
    if (rows == 0 || cols == 0) return;
    final cw = size.width / cols;
    final ch = size.height / rows;
    final p = Paint();
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < cols; c++) {
        p.color = colorFor(data[r][c]);
        canvas.drawRect(Rect.fromLTWH(c * cw, r * ch, cw + 0.5, ch + 0.5), p);
      }
    }
  }

  @override
  bool shouldRepaint(covariant _HeatmapPainter old) => true;
}

class _HoverPainter extends CustomPainter {
  final Offset hoverPos;
  final List<List<double>> data;
  final String unit;
  final bool isDark;
  _HoverPainter({required this.hoverPos, required this.data, required this.unit, required this.isDark});

  @override
  void paint(Canvas canvas, Size size) {
    final rows = data.length;
    final cols = rows > 0 ? data[0].length : 0;
    if (rows == 0 || cols == 0) return;

    final cw = size.width / cols;
    final ch = size.height / rows;
    final col = (hoverPos.dx / cw).floor().clamp(0, cols - 1);
    final row = (hoverPos.dy / ch).floor().clamp(0, rows - 1);
    final val = data[row][col];

    // Highlight cell
    canvas.drawRect(
      Rect.fromLTWH(col * cw, row * ch, cw, ch),
      Paint()..color = Colors.white..style = PaintingStyle.stroke..strokeWidth = 1.5,
    );

    // Tooltip
    final xMm = (col * _cellSize).toInt();
    final yMm = (row * _cellSize).toInt();
    final label = 'X:${xMm}mm Y:${yMm}mm\n${val >= 0 ? val.toStringAsFixed(1) : '--'}$unit';
    final tp = TextPainter(
      text: TextSpan(text: label, style: const TextStyle(color: Colors.white, fontSize: 9, height: 1.4)),
      textDirection: TextDirection.ltr,
    )..layout();

    final bw = tp.width + 12;
    final bh = tp.height + 8;
    double dx = hoverPos.dx + 14;
    double dy = hoverPos.dy - bh / 2;
    if (dx + bw > size.width) dx = hoverPos.dx - bw - 8;
    if (dy < 0) dy = 4;
    if (dy + bh > size.height) dy = size.height - bh - 4;

    canvas.drawRRect(
      RRect.fromRectAndRadius(Rect.fromLTWH(dx, dy, bw, bh), const Radius.circular(5)),
      Paint()..color = const Color(0xDD000000),
    );
    tp.paint(canvas, Offset(dx + 6, dy + 4));
  }

  @override
  bool shouldRepaint(covariant _HoverPainter old) => hoverPos != old.hoverPos;
}

class _LegendPainter extends CustomPainter {
  final List<_ColorStop> stops;
  _LegendPainter({required this.stops});

  @override
  void paint(Canvas canvas, Size size) {
    final gradient = LinearGradient(
      colors: stops.map((s) => s.color).toList(),
    );
    final paint = Paint()
      ..shader = gradient.createShader(Rect.fromLTWH(0, 0, size.width, size.height));
    canvas.drawRect(Rect.fromLTWH(0, 0, size.width, size.height), paint);
  }

  @override
  bool shouldRepaint(covariant _LegendPainter old) => false;
}
