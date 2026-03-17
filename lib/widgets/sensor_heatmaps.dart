// lib/widgets/sensor_heatmaps.dart

import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:lucide_icons/lucide_icons.dart';
import '../providers/theme_provider.dart';

// ── Configuration ───────────────────────────────────────────
const int gridSize = 50;
const double areaSize = 1000.0; // mm
const double cellSize = areaSize / gridSize; // 20mm

// ── Data Model ──────────────────────────────────────────────
class SensorGridData {
  final List<List<double>> moisture;
  final List<List<double>> npk;
  final List<List<double>> ec;
  final List<List<double>> ph;
  final List<List<double>> soilTemp;

  SensorGridData({
    required this.moisture,
    required this.npk,
    required this.ec,
    required this.ph,
    required this.soilTemp,
  });
}

class ColorStop {
  final String value;
  final Color color;
  final String label;
  const ColorStop({required this.value, required this.color, required this.label});
}

// ── Main Widget ─────────────────────────────────────────────
class SensorHeatmaps extends ConsumerStatefulWidget {
  const SensorHeatmaps({super.key});

  @override
  ConsumerState<SensorHeatmaps> createState() => _SensorHeatmapsState();
}

class _SensorHeatmapsState extends ConsumerState<SensorHeatmaps> {
  late SensorGridData sensorData;

  @override
  void initState() {
    super.initState();
    sensorData = _generateSensorData();
  }

  // ── Procedural Data Generation ──
  double _smoothNoise(int x, int y, double seed) {
    double nx = x / gridSize;
    double ny = y / gridSize;
    return (math.sin(nx * seed + ny * 3) +
            math.cos(ny * seed + nx * 2) +
            math.sin((nx + ny) * 4)) / 3;
  }

  SensorGridData _generateSensorData() {
    List<List<double>> generate(double seed, double Function(int r, int c, double noise) formula) {
      return List.generate(gridSize, (r) => List.generate(gridSize, (c) => formula(r, c, _smoothNoise(r, c, seed))));
    }
    return SensorGridData(
      moisture: generate(5.2, (r, c, noise) => 50.0 + noise * 25 + (r / gridSize) * 30),
      npk: generate(7.8, (r, c, noise) {
        double dist = math.sqrt(math.pow(r - gridSize / 2, 2) + math.pow(c - gridSize / 2, 2));
        double centerEffect = (1 - dist / (gridSize / 2)) * 20;
        return 60.0 + noise * 20 + centerEffect;
      }),
      ec: generate(3.4, (r, c, noise) => 1.2 + noise * 0.8 + (c / gridSize) * 1.2),
      ph: generate(9.1, (r, c, noise) => 6.2 + noise * 0.5 + ((r + c) / (gridSize * 2)) * 0.8),
      soilTemp: generate(4.6, (r, c, noise) => 22.0 + noise * 4 + (1 - r / gridSize) * 6),
    );
  }

  // ── Color Mapping Functions ──
  Color _clampRGB(num r, num g, num b) => Color.fromARGB(255, r.toInt().clamp(0, 255), g.toInt().clamp(0, 255), b.toInt().clamp(0, 255));

  Color _getMoistureColor(double val) {
    if (val < 35) return _clampRGB(139 + (35 - val) * 2, 0, 0);
    if (val < 50) return _clampRGB(255 - (val - 35) * 5, 100 + (val - 35) * 4, 0);
    if (val < 65) return _clampRGB(234 - (val - 50) * 8, 160 + (val - 50) * 2, 0);
    if (val < 80) return _clampRGB(59 - (val - 65) * 2, 130 + (val - 65) * 3, 246);
    return _clampRGB(30 - (val - 80) * 0.5, 90 + (val - 80), 180 + (val - 80) * 2);
  }

  Color _getNpkColor(double val) {
    if (val < 40) return _clampRGB(139 + (40 - val) * 2, 0, 0);
    if (val < 60) return _clampRGB(255 - (val - 40) * 5, 100 + (val - 40) * 3, 0);
    if (val < 75) return _clampRGB(34 + (val - 60) * 2, 197 - (val - 60) * 2, 94);
    if (val < 90) return _clampRGB(34 - (val - 75) * 0.5, 197 - (val - 75) * 2, 94 - (val - 75));
    return _clampRGB(22 - (val - 90) * 0.5, 163 - (val - 90) * 2, 74 - (val - 90));
  }

  Color _getEcColor(double val) {
    if (val < 1.0) return _clampRGB(30 + (1.0 - val) * 80, 58 + (1.0 - val) * 80, 138 + (1.0 - val) * 80);
    if (val < 1.8) return _clampRGB(34 - (val - 1.0) * 10, 197 - (val - 1.0) * 50, 94 - (val - 1.0) * 20);
    if (val < 2.5) return _clampRGB(234 - (val - 1.8) * 50, 179 - (val - 1.8) * 40, 8 - (val - 1.8) * 8);
    if (val < 3.0) return _clampRGB(249 - (val - 2.5) * 30, 115 - (val - 2.5) * 20, 22 - (val - 2.5) * 22);
    return _clampRGB(220 - (val - 3.0) * 20, 38 - (val - 3.0) * 20, 38 - (val - 3.0) * 20);
  }

  Color _getPhColor(double val) {
    if (val < 5.8) return _clampRGB(139 + (5.8 - val) * 20, 0, 0);
    if (val < 6.2) return _clampRGB(249 - (val - 5.8) * 20, 115 - (val - 5.8) * 15, 22 - (val - 5.8) * 22);
    if (val < 6.8) return _clampRGB(34 - (val - 6.2) * 2, 197 - (val - 6.2) * 10, 94 - (val - 6.2) * 5);
    if (val < 7.2) return _clampRGB(234 - (val - 6.8) * 100, 179 - (val - 6.8) * 20, 8 - (val - 6.8) * 8);
    return _clampRGB(168 - (val - 7.2) * 20, 85 - (val - 7.2) * 20, 247 - (val - 7.2) * 20);
  }

  Color _getSoilTempColor(double val) {
    if (val < 20) return _clampRGB(30 + (20 - val) * 10, 58 + (20 - val) * 10, 138 + (20 - val) * 10);
    if (val < 23) return _clampRGB(59 - (val - 20) * 8, 130 - (val - 20) * 20, 246 - (val - 23) * 40);
    if (val < 26) return _clampRGB(34 - (val - 23) * 5, 197 - (val - 23) * 20, 94 - (val - 23) * 10);
    if (val < 28) return _clampRGB(234 - (val - 26) * 50, 179 - (val - 26) * 30, 8 - (val - 26) * 4);
    if (val < 30) return _clampRGB(249 - (val - 28) * 20, 115 - (val - 28) * 30, 22 - (val - 28) * 11);
    return _clampRGB(239 - (val - 30) * 10, 68 - (val - 30) * 10, 68 - (val - 30) * 10);
  }

  @override
  Widget build(BuildContext context) {
    final themeState = ref.watch(themeProvider);
    final accent = themeState.currentAccentColor;
    final isDark = themeState.isDark(context);

    // Theme Variables
    final bgColor = isDark ? const Color(0xFF0B0F1A) : const Color(0xFFF1F5F9);
    final textColor = isDark ? Colors.white : const Color(0xFF111827);
    final subTextColor = isDark ? const Color(0xFF9CA3AF) : const Color(0xFF4B5563);
    final cardColor = isDark ? const Color(0xFF1F2937) : Colors.white;
    final borderColor = isDark ? const Color(0xFF374151) : Colors.grey.shade300;

    return Container(
      color: bgColor, // 👈 FIX: Solid background during theme switch
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // ── Header ──
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: accent.withOpacity(0.05),
              border: Border.all(color: accent.withOpacity(0.15)),
              borderRadius: BorderRadius.circular(12),
            ),
            child: Row(
              children: [
                Container(width: 8, height: 8, decoration: BoxDecoration(color: accent, shape: BoxShape.circle)),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      RichText(
                        text: TextSpan(
                          style: TextStyle(fontSize: 14, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: textColor, letterSpacing: 0.5),
                          children: [
                            const TextSpan(text: 'SENSOR '),
                            TextSpan(text: 'GRADIENT HEATMAPS', style: TextStyle(color: accent)),
                          ],
                        ),
                      ),
                      Text('Live bed visualization - 1000mm × 1000mm coverage', style: TextStyle(fontSize: 10, color: subTextColor, fontFamily: 'monospace')),
                    ],
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),

          // ── Heatmaps Grid ──
          LayoutBuilder(builder: (context, constraints) {
            final isWide = constraints.maxWidth > 750;
            return GridView.count(
              crossAxisCount: isWide ? 2 : 1,
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              crossAxisSpacing: 16,
              mainAxisSpacing: 16,
              childAspectRatio: 0.85,
              children: [
                GradientHeatmapCard(
                  data: sensorData.moisture, label: 'SOIL MOISTURE', unit: '%', icon: LucideIcons.droplets, getColor: _getMoistureColor, accent: accent, isDark: isDark, cardColor: cardColor, borderColor: borderColor, textColor: textColor, subTextColor: subTextColor,
                  colorStops: const [
                    ColorStop(value: '<35%', color: Color.fromARGB(255, 180, 0, 0), label: 'Critical'),
                    ColorStop(value: '65%', color: Color.fromARGB(255, 234, 179, 8), label: 'Moderate'),
                    ColorStop(value: '>80%', color: Color.fromARGB(255, 30, 90, 200), label: 'Optimal'),
                  ],
                ),
                GradientHeatmapCard(
                  data: sensorData.npk, label: 'FERTILIZER HEALTH', unit: '%', icon: LucideIcons.sprout, getColor: _getNpkColor, accent: accent, isDark: isDark, cardColor: cardColor, borderColor: borderColor, textColor: textColor, subTextColor: subTextColor,
                  colorStops: const [
                    ColorStop(value: '<40%', color: Color.fromARGB(255, 180, 0, 0), label: 'Poor'),
                    ColorStop(value: '75%', color: Color.fromARGB(255, 234, 179, 8), label: 'Good'),
                    ColorStop(value: '>90%', color: Color.fromARGB(255, 22, 163, 74), label: 'Excellent'),
                  ],
                ),
                GradientHeatmapCard(
                  data: sensorData.ec, label: 'ELECTRIC CONDUCTIVITY', unit: 'mS', icon: LucideIcons.zap, getColor: _getEcColor, accent: accent, isDark: isDark, cardColor: cardColor, borderColor: borderColor, textColor: textColor, subTextColor: subTextColor,
                  colorStops: const [
                    ColorStop(value: '<1.0', color: Color.fromARGB(255, 30, 58, 138), label: 'Very Low'),
                    ColorStop(value: '2.5', color: Color.fromARGB(255, 34, 197, 94), label: 'Optimal'),
                    ColorStop(value: '>3.0', color: Color.fromARGB(255, 220, 38, 38), label: 'Very High'),
                  ],
                ),
                GradientHeatmapCard(
                  data: sensorData.ph, label: 'PH LEVEL', unit: 'pH', icon: LucideIcons.beaker, getColor: _getPhColor, accent: accent, isDark: isDark, cardColor: cardColor, borderColor: borderColor, textColor: textColor, subTextColor: subTextColor,
                  colorStops: const [
                    ColorStop(value: '<5.8', color: Color.fromARGB(255, 180, 0, 0), label: 'Too Acidic'),
                    ColorStop(value: '6.5', color: Color.fromARGB(255, 249, 115, 22), label: 'Acidic'),
                    ColorStop(value: '>7.2', color: Color.fromARGB(255, 168, 85, 247), label: 'Alkaline'),
                  ],
                ),
                GradientHeatmapCard(
                  data: sensorData.soilTemp, label: 'SOIL TEMPERATURE', unit: '°C', icon: LucideIcons.thermometer, getColor: _getSoilTempColor, accent: accent, isDark: isDark, cardColor: cardColor, borderColor: borderColor, textColor: textColor, subTextColor: subTextColor,
                  colorStops: const [
                    ColorStop(value: '<20°C', color: Color.fromARGB(255, 30, 58, 138), label: 'Cold'),
                    ColorStop(value: '26°C', color: Color.fromARGB(255, 34, 197, 94), label: 'Optimal'),
                    ColorStop(value: '>30°C', color: Color.fromARGB(255, 239, 68, 68), label: 'Hot'),
                  ],
                ),
              ],
            );
          }),

          const SizedBox(height: 16),
          _HeatmapGuide(accent: accent, isDark: isDark, cardColor: cardColor, borderColor: borderColor, textColor: textColor, subTextColor: subTextColor),
        ],
      ),
    );
  }
}

// ─────────────────────────────────────────────────────────────
// Individual Heatmap Card Widget
// ─────────────────────────────────────────────────────────────
class GradientHeatmapCard extends StatefulWidget {
  final List<List<double>> data;
  final String label, unit;
  final IconData icon;
  final Color Function(double) getColor;
  final List<ColorStop> colorStops;
  final Color accent, cardColor, borderColor, textColor, subTextColor;
  final bool isDark;

  const GradientHeatmapCard({
    super.key, required this.data, required this.label, required this.unit, required this.icon, required this.getColor, required this.colorStops, required this.accent, required this.isDark, required this.cardColor, required this.borderColor, required this.textColor, required this.subTextColor,
  });

  @override
  State<GradientHeatmapCard> createState() => _GradientHeatmapCardState();
}

class _GradientHeatmapCardState extends State<GradientHeatmapCard> {
  Offset? _hoverPos;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(color: widget.cardColor, borderRadius: BorderRadius.circular(12), border: Border.all(color: widget.borderColor)),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(padding: const EdgeInsets.all(8), decoration: BoxDecoration(color: widget.accent.withOpacity(0.1), borderRadius: BorderRadius.circular(8)), child: Icon(widget.icon, color: widget.accent, size: 20)),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    RichText(text: TextSpan(style: TextStyle(fontSize: 14, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: widget.textColor, letterSpacing: 0.5), children: [TextSpan(text: '${widget.label} '), TextSpan(text: 'HEATMAP', style: TextStyle(color: widget.accent))])),
                    Text('1000mm × 1000mm bed area', style: TextStyle(fontSize: 9, color: widget.subTextColor, fontFamily: 'monospace')),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 16),
          Expanded(
            child: LayoutBuilder(
              builder: (context, constraints) {
                double side = constraints.maxWidth;
                return Center(
                  child: SizedBox(
                    width: side, height: side,
                    child: MouseRegion(
                      onHover: (event) => setState(() => _hoverPos = event.localPosition),
                      onExit: (_) => setState(() => _hoverPos = null),
                      child: ClipRRect(
                        borderRadius: BorderRadius.circular(8),
                        child: CustomPaint(
                          size: Size(side, side),
                          painter: _HeatmapPainter(data: widget.data, getColor: widget.getColor),
                          foregroundPainter: _HoverPainter(hoverPos: _hoverPos, data: widget.data, unit: widget.unit, isDark: widget.isDark),
                        ),
                      ),
                    ),
                  ),
                );
              },
            ),
          ),
          const SizedBox(height: 16),
          _buildLegend(),
        ],
      ),
    );
  }

  Widget _buildLegend() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('SCALE', style: TextStyle(fontSize: 9, fontWeight: FontWeight.bold, color: widget.subTextColor, letterSpacing: 1.5)),
        const SizedBox(height: 6),
        Container(
          height: 24,
          decoration: BoxDecoration(borderRadius: BorderRadius.circular(6), gradient: LinearGradient(colors: widget.colorStops.map((s) => s.color).toList())),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 8),
            child: Row(mainAxisAlignment: MainAxisAlignment.spaceBetween, children: widget.colorStops.map((s) => Text(s.value, style: const TextStyle(fontSize: 10, fontWeight: FontWeight.bold, color: Colors.white, shadows: [Shadow(color: Colors.black54, blurRadius: 2)]))).toList()),
          ),
        ),
        const SizedBox(height: 6),
        Row(mainAxisAlignment: MainAxisAlignment.spaceBetween, children: [
          Text(widget.colorStops.first.label, style: TextStyle(fontSize: 9, color: widget.subTextColor)),
          Text(widget.colorStops.last.label, style: TextStyle(fontSize: 9, color: widget.subTextColor)),
        ]),
      ],
    );
  }
}

class _HeatmapPainter extends CustomPainter {
  final List<List<double>> data;
  final Color Function(double) getColor;
  _HeatmapPainter({required this.data, required this.getColor});
  @override
  void paint(Canvas canvas, Size size) {
    double w = size.width / gridSize; double h = size.height / gridSize;
    final paint = Paint()..style = PaintingStyle.fill;
    for (int r = 0; r < gridSize; r++) {
      for (int c = 0; c < gridSize; c++) {
        paint.color = getColor(data[r][c]);
        canvas.drawRect(Rect.fromLTWH(c * w, r * h, w + 0.5, h + 0.5), paint);
      }
    }
  }
  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}

class _HoverPainter extends CustomPainter {
  final Offset? hoverPos; final List<List<double>> data; final String unit; final bool isDark;
  _HoverPainter({this.hoverPos, required this.data, required this.unit, required this.isDark});
  @override
  void paint(Canvas canvas, Size size) {
    if (hoverPos == null) return;
    double cellW = size.width / gridSize; double cellH = size.height / gridSize;
    int col = (hoverPos!.dx / cellW).floor().clamp(0, gridSize - 1);
    int row = (hoverPos!.dy / cellH).floor().clamp(0, gridSize - 1);
    double val = data[row][col];
    
    // Grid highlight
    canvas.drawRect(Rect.fromLTWH(col * cellW, row * cellH, cellW, cellH), Paint()..color = isDark ? Colors.white : Colors.black..style = PaintingStyle.stroke..strokeWidth = 2);
    
    final textPainter = TextPainter(text: TextSpan(text: 'X:${(col * cellSize).toInt()} Y:${(row * cellSize).toInt()}\n${val.toStringAsFixed(1)}$unit', style: const TextStyle(color: Colors.white, fontSize: 10, height: 1.4)), textDirection: TextDirection.ltr)..layout();
    
    double dx = (hoverPos!.dx + 15 + textPainter.width + 12 > size.width) ? hoverPos!.dx - textPainter.width - 25 : hoverPos!.dx + 15;
    double dy = (hoverPos!.dy - 15 < 0) ? 10 : hoverPos!.dy - 15;
    final bgRect = Rect.fromLTWH(dx, dy, textPainter.width + 12, textPainter.height + 8);
    
    canvas.drawRRect(RRect.fromRectAndRadius(bgRect, const Radius.circular(6)), Paint()..color = const Color(0xDD000000));
    textPainter.paint(canvas, Offset(dx + 6, dy + 4));
  }
  @override
  bool shouldRepaint(covariant _HoverPainter oldDelegate) => hoverPos != oldDelegate.hoverPos;
}

class _HeatmapGuide extends StatelessWidget {
  final Color accent, cardColor, borderColor, textColor, subTextColor; final bool isDark;
  const _HeatmapGuide({required this.accent, required this.isDark, required this.cardColor, required this.borderColor, required this.textColor, required this.subTextColor});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(color: cardColor, borderRadius: BorderRadius.circular(12), border: Border.all(color: borderColor)),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(padding: const EdgeInsets.all(12), decoration: BoxDecoration(color: accent.withOpacity(0.1), borderRadius: BorderRadius.circular(10)), child: Icon(LucideIcons.sprout, color: accent, size: 24)),
          const SizedBox(height: 16),
          RichText(text: TextSpan(style: TextStyle(fontSize: 16, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: textColor, letterSpacing: 0.5), children: [const TextSpan(text: 'HEATMAP '), TextSpan(text: 'GUIDE', style: TextStyle(color: accent))])),
          const SizedBox(height: 8),
          Text('Each heatmap displays a continuous gradient across the 1000mm × 1000mm bed area. Hover over any point to view exact sensor readings and coordinates.', style: TextStyle(fontSize: 12, color: subTextColor, height: 1.5)),
          const SizedBox(height: 20),
          _buildRow(LucideIcons.droplets, 'COVERAGE AREA', '1000×1000mm', accent),
          const SizedBox(height: 12),
          _buildRow(LucideIcons.zap, 'GRID RESOLUTION', '50×50', const Color(0xFF34D399)),
          const SizedBox(height: 12),
          _buildRow(LucideIcons.thermometer, 'CELL SIZE', '20mm²', const Color(0xFF60A5FA)),
        ],
      ),
    );
  }

  Widget _buildRow(IconData icon, String label, String value, Color color) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(color: color.withOpacity(0.1), borderRadius: BorderRadius.circular(8), border: Border.all(color: color.withOpacity(0.2))),
      child: Row(children: [
        Icon(icon, color: color, size: 20),
        const SizedBox(width: 16),
        Expanded(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(label, style: TextStyle(fontSize: 9, fontWeight: FontWeight.bold, color: subTextColor, letterSpacing: 1.2)),
          Text(value, style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold, color: textColor)),
        ])),
      ]),
    );
  }
}