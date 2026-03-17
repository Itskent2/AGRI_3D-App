// lib/screens/plot_map.dart

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/theme_provider.dart';

// ─────────────────────────────────────────────────────────────
// Constants & model
// ─────────────────────────────────────────────────────────────

const int kGridCols = 7;
const int kGridRows = 7;

class Plot {
  final String id;
  final String name;
  final int col; 
  final int row; 
  final int moisture;
  final Map<String, int> npk;
  final Map<String, int> targetNpk;

  Plot({
    required this.id,
    required this.name,
    required this.col,
    required this.row,
    required this.moisture,
    required this.npk,
    required this.targetNpk,
  });

  Plot copyWithTarget(Map<String, int> t) => Plot(
        id: id, name: name, col: col, row: row,
        moisture: moisture, npk: npk, targetNpk: t,
      );
}

final List<Plot> _initialPlots = [
  Plot(id: '1', name: 'LETTUCE', col: 1, row: 2, moisture: 60, npk: {'n': 45, 'p': 30, 'k': 40}, targetNpk: {'n': 50, 'p': 35, 'k': 45}),
  Plot(id: '2', name: 'TOMATO', col: 3, row: 2, moisture: 55, npk: {'n': 38, 'p': 22, 'k': 30}, targetNpk: {'n': 45, 'p': 28, 'k': 35}),
  Plot(id: '3', name: 'BASIL', col: 5, row: 4, moisture: 40, npk: {'n': 20, 'p': 15, 'k': 18}, targetNpk: {'n': 25, 'p': 18, 'k': 22}),
];

// ─────────────────────────────────────────────────────────────
// Screen
// ─────────────────────────────────────────────────────────────

class PlotMapScreen extends ConsumerStatefulWidget {
  const PlotMapScreen({super.key});

  @override
  ConsumerState<PlotMapScreen> createState() => _PlotMapScreenState();
}

class _PlotMapScreenState extends ConsumerState<PlotMapScreen> {
  List<Plot> _plots = List.from(_initialPlots);
  Plot? _selectedPlot;
  bool _isEditingNpk = false;

  final _nCtrl = TextEditingController();
  final _pCtrl = TextEditingController();
  final _kCtrl = TextEditingController();

  @override
  void dispose() {
    _nCtrl.dispose(); _pCtrl.dispose(); _kCtrl.dispose();
    super.dispose();
  }

  void _selectPlot(Plot plot) => setState(() { _selectedPlot = plot; _isEditingNpk = false; });

  void _startEditing() {
    if (_selectedPlot == null) return;
    _nCtrl.text = _selectedPlot!.targetNpk['n'].toString();
    _pCtrl.text = _selectedPlot!.targetNpk['p'].toString();
    _kCtrl.text = _selectedPlot!.targetNpk['k'].toString();
    setState(() => _isEditingNpk = true);
  }

  void _saveNpk() {
    if (_selectedPlot == null) return;
    final updated = _selectedPlot!.copyWithTarget({
      'n': int.tryParse(_nCtrl.text) ?? _selectedPlot!.targetNpk['n']!,
      'p': int.tryParse(_pCtrl.text) ?? _selectedPlot!.targetNpk['p']!,
      'k': int.tryParse(_kCtrl.text) ?? _selectedPlot!.targetNpk['k']!,
    });
    setState(() {
      _plots = _plots.map((p) => p.id == updated.id ? updated : p).toList();
      _selectedPlot = updated;
      _isEditingNpk = false;
    });
  }

  @override
  Widget build(BuildContext context) {
    final themeState = ref.watch(themeProvider);
    final accent = themeState.currentAccentColor;
    final isDark = themeState.isDark(context);

    // Adaptive Theme Variables
    final textColor = isDark ? Colors.white : const Color(0xFF111827);
    final subTextColor = isDark ? const Color(0xFF9CA3AF) : const Color(0xFF4B5563);
    final cardColor = isDark ? const Color(0xFF1F2937) : Colors.white;
    final borderColor = isDark ? const Color(0xFF374151) : Colors.grey.shade300;
    final gridBg = isDark ? const Color(0xFF0A0F1E) : const Color(0xFFF8FAFC);

    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          RichText(
            text: TextSpan(
              style: TextStyle(fontSize: 22, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: textColor),
              children: [
                const TextSpan(text: 'PLOT '),
                TextSpan(text: 'GRID MAP', style: TextStyle(color: accent)),
              ],
            ),
          ),
          const SizedBox(height: 16),
          _buildGrid(accent, isDark, gridBg, borderColor),
          const SizedBox(height: 16),
          _buildDetail(accent, isDark, cardColor, borderColor, textColor, subTextColor),
        ],
      ),
    );
  }

  // ── Grid ─────────────────────────────────────────────────

  Widget _buildGrid(Color accent, bool isDark, Color gridBg, Color borderColor) {
    return Container(
      decoration: BoxDecoration(
        color: gridBg,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: borderColor),
      ),
      padding: const EdgeInsets.all(8),
      child: AspectRatio(
        aspectRatio: 1,
        child: LayoutBuilder(
          builder: (context, box) {
            final cellW = box.maxWidth / kGridCols;
            final cellH = box.maxHeight / kGridRows;

            return Stack(
              children: [
                CustomPaint(
                  size: Size(box.maxWidth, box.maxHeight),
                  painter: _GridPainter(cols: kGridCols, rows: kGridRows, color: borderColor.withOpacity(0.5)),
                ),
                ..._plots.map((plot) {
                  final isSelected = _selectedPlot?.id == plot.id;
                  final cx = plot.col * cellW + cellW / 2;
                  final cy = plot.row * cellH + cellH / 2;
                  final dotR = isSelected ? 10.0 : 7.0;

                  return Positioned(
                    left: cx - dotR,
                    top: cy - dotR,
                    child: GestureDetector(
                      onTap: () => _selectPlot(plot),
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          AnimatedContainer(
                            duration: const Duration(milliseconds: 150),
                            width: dotR * 2, height: dotR * 2,
                            decoration: BoxDecoration(
                              shape: BoxShape.circle,
                              color: isSelected ? accent : const Color(0xFF22C55E),
                              border: Border.all(color: isSelected ? Colors.white : const Color(0xFF4ADE80), width: isSelected ? 2 : 1),
                              boxShadow: [
                                BoxShadow(
                                  color: (isSelected ? accent : const Color(0xFF22C55E)).withOpacity(0.5),
                                  blurRadius: isSelected ? 10 : 6,
                                  spreadRadius: isSelected ? 2 : 1,
                                ),
                              ],
                            ),
                          ),
                          const SizedBox(height: 3),
                          Text(
                            plot.name,
                            style: TextStyle(
                              fontSize: 8, fontWeight: FontWeight.bold,
                              color: isSelected ? accent : (isDark ? const Color(0xFF9CA3AF) : const Color(0xFF4B5563)),
                              letterSpacing: 0.5,
                            ),
                          ),
                        ],
                      ),
                    ),
                  );
                }),
              ],
            );
          },
        ),
      ),
    );
  }

  // ── Detail panel ─────────────────────────────────────────

  Widget _buildDetail(Color accent, bool isDark, Color cardColor, Color borderColor, Color textColor, Color subTextColor) {
    if (_selectedPlot == null) {
      return Container(
        width: double.infinity, padding: const EdgeInsets.symmetric(vertical: 32),
        decoration: BoxDecoration(color: cardColor, borderRadius: BorderRadius.circular(16), border: Border.all(color: borderColor)),
        child: Center(child: Text('Select a plot on the grid', style: TextStyle(color: subTextColor))),
      );
    }

    final plot = _selectedPlot!;
    return Container(
      width: double.infinity, padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(color: cardColor, borderRadius: BorderRadius.circular(16), border: Border.all(color: borderColor)),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('${plot.name} DETAILS', style: TextStyle(color: textColor, fontSize: 16, fontWeight: FontWeight.w900, letterSpacing: 1)),
          const SizedBox(height: 12),
          _row('X', '${plot.col * 250} mm', subTextColor, textColor),
          _row('Y', '${plot.row * 250} mm', subTextColor, textColor),
          _row('Moisture', '${plot.moisture}%', subTextColor, textColor),
          const SizedBox(height: 16),
          Text('NPK LEVEL', style: TextStyle(fontSize: 10, color: subTextColor, letterSpacing: 2, fontWeight: FontWeight.bold)),
          const SizedBox(height: 4),
          Text('N: ${plot.npk['n']}  |  P: ${plot.npk['p']}  |  K: ${plot.npk['k']}', style: TextStyle(color: textColor, fontWeight: FontWeight.bold)),
          const SizedBox(height: 16),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text('SET TARGET NPK', style: TextStyle(fontSize: 10, color: subTextColor, letterSpacing: 2, fontWeight: FontWeight.bold)),
              IconButton(icon: Icon(_isEditingNpk ? Icons.check_circle : Icons.edit, color: accent, size: 18), onPressed: _isEditingNpk ? _saveNpk : _startEditing),
            ],
          ),
          if (!_isEditingNpk)
            Text('N: ${plot.targetNpk['n']}  |  P: ${plot.targetNpk['p']}  |  K: ${plot.targetNpk['k']}', style: TextStyle(color: textColor, fontWeight: FontWeight.bold))
          else
            Column(
              children: [
                Row(
                  children: [
                    _npkField('N', _nCtrl, accent, isDark, subTextColor, textColor),
                    const SizedBox(width: 8),
                    _npkField('P', _pCtrl, accent, isDark, subTextColor, textColor),
                    const SizedBox(width: 8),
                    _npkField('K', _kCtrl, accent, isDark, subTextColor, textColor),
                  ],
                ),
                const SizedBox(height: 12),
                SizedBox(
                  width: double.infinity,
                  child: ElevatedButton(
                    onPressed: _saveNpk,
                    style: ElevatedButton.styleFrom(backgroundColor: accent, foregroundColor: Colors.white, shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8))),
                    child: const Text('SAVE TARGETS', style: TextStyle(fontWeight: FontWeight.bold, letterSpacing: 1)),
                  ),
                ),
              ],
            ),
        ],
      ),
    );
  }

  Widget _row(String label, String value, Color sub, Color main) => Padding(
    padding: const EdgeInsets.only(bottom: 4),
    child: Row(children: [
      Text('$label: ', style: TextStyle(color: sub, fontSize: 13)),
      Text(value, style: TextStyle(color: main, fontSize: 13, fontWeight: FontWeight.bold)),
    ]),
  );

  Widget _npkField(String label, TextEditingController ctrl, Color accent, bool isDark, Color sub, Color main) {
    return Expanded(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: TextStyle(fontSize: 10, color: accent, fontWeight: FontWeight.bold)),
          const SizedBox(height: 4),
          TextField(
            controller: ctrl,
            keyboardType: TextInputType.number,
            style: TextStyle(color: main, fontSize: 13),
            decoration: InputDecoration(
              filled: true, fillColor: isDark ? const Color(0xFF111827) : Colors.grey.shade100,
              contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide.none),
              focusedBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: accent)),
            ),
          ),
        ],
      ),
    );
  }
}

class _GridPainter extends CustomPainter {
  final int cols; final int rows; final Color color;
  const _GridPainter({required this.cols, required this.rows, required this.color});

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()..color = color..strokeWidth = 0.8;
    final cellW = size.width / cols;
    final cellH = size.height / rows;
    for (int c = 0; c <= cols; c++) { canvas.drawLine(Offset(c * cellW, 0), Offset(c * cellW, size.height), paint); }
    for (int r = 0; r <= rows; r++) { canvas.drawLine(Offset(0, r * cellH), Offset(size.width, r * cellH), paint); }
  }
  @override
  bool shouldRepaint(covariant _GridPainter old) => old.color != color;
}