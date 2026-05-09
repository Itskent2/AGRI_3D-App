// lib/screens/plot_map.dart

import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/theme_provider.dart';

import '../models/plot_model.dart';
import '../service/ESP32/esp32_service.dart';

// ─────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────

const double kBedWidthMm = 1000.0;
const double kBedHeightMm = 1000.0;

// The grid visual uses fixed lines just for aesthetic structure,
// but the actual plot dots are placed via mm coordinates.
const int kGridCols = 5;
const int kGridRows = 5;

// ─────────────────────────────────────────────────────────────
// Screen
// ─────────────────────────────────────────────────────────────

class PlotMapScreen extends ConsumerStatefulWidget {
  const PlotMapScreen({super.key});

  @override
  ConsumerState<PlotMapScreen> createState() => _PlotMapScreenState();
}

class _PlotMapScreenState extends ConsumerState<PlotMapScreen> {
  List<Plot> _plots = List.from(initialPlots);
  Plot? _selectedPlot;
  bool _isEditingNpk = false;

  // Map to hold our scanned thumbnails (Key: "x_y" string)
  final Map<String, Uint8List> _stitchedImages = {};

  final _nCtrl = TextEditingController();
  final _pCtrl = TextEditingController();
  final _kCtrl = TextEditingController();

  final ScrollController _consoleScrollController = ScrollController();

  @override
  void initState() {
    super.initState();
    ESP32Service.instance.onFrameCaptured.listen(_handleFrameCaptured);
    ESP32Service.instance.onPlantCandidate.listen(_handlePlantCandidate);
    ESP32Service.instance.addListener(_scrollToBottom);
  }

  void _scrollToBottom() {
    Future.delayed(const Duration(milliseconds: 50), () {
      if (_consoleScrollController.hasClients) {
        _consoleScrollController.animateTo(
          _consoleScrollController.position.maxScrollExtent,
          duration: const Duration(milliseconds: 200),
          curve: Curves.easeOut,
        );
      }
    });
  }

  void _handlePlantCandidate(Map<String, dynamic> data) {
    if (!mounted) return;
    
    final double x = double.parse(data['x'].toString());
    final double y = double.parse(data['y'].toString());
    final double conf = double.parse(data['conf'].toString());
    final Uint8List image = data['image'] as Uint8List;

    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (ctx) {
        return AlertDialog(
          title: const Text("Plant Candidate Detected"),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Image.memory(image, height: 150),
              const SizedBox(height: 10),
              Text("Location: X:${x.toStringAsFixed(1)} Y:${y.toStringAsFixed(1)}"),
              Text("Confidence: ${(conf * 100).toStringAsFixed(0)}%"),
            ],
          ),
          actions: [
            TextButton(
              onPressed: () {
                ESP32Service.instance.rejectPlant(x, y);
                Navigator.pop(ctx);
              },
              child: const Text("Reject", style: TextStyle(color: Colors.red)),
            ),
            ElevatedButton(
              onPressed: () {
                ESP32Service.instance.confirmPlant(x, y, "AI_Weed");
                // Add it to our local plot list temporarily
                setState(() {
                  _plots.add(Plot(
                    id: DateTime.now().millisecondsSinceEpoch,
                    name: "AI_Weed",
                    x: x,
                    y: y,
                    moisture: 0,
                    npk: const NpkLevel(n: 0, p: 0, k: 0),
                    targetNpk: const NpkLevel(n: 0, p: 0, k: 0),
                    aiDetected: true,
                  ));
                });
                Navigator.pop(ctx);
              },
              child: const Text("Confirm"),
            ),
          ],
        );
      }
    );
  }

  void _handleFrameCaptured(Map<String, dynamic> data) {
    if (!mounted) return;
    try {
      final double x = double.parse(data['x'].toString());
      final double y = double.parse(data['y'].toString());
      final Uint8List image = data['image'] as Uint8List;
      
      setState(() {
        _stitchedImages["${x.toStringAsFixed(0)}_${y.toStringAsFixed(0)}"] = image;
      });
    } catch (e) {
      debugPrint("Error handling frame: $e");
    }
  }

  @override
  void dispose() {
    ESP32Service.instance.removeListener(_scrollToBottom);
    _consoleScrollController.dispose();
    _nCtrl.dispose(); _pCtrl.dispose(); _kCtrl.dispose();
    super.dispose();
  }

  void _selectPlot(Plot plot) => setState(() { _selectedPlot = plot; _isEditingNpk = false; });

  void _startEditing() {
    if (_selectedPlot == null) return;
    _nCtrl.text = _selectedPlot!.targetNpk.n.toString();
    _pCtrl.text = _selectedPlot!.targetNpk.p.toString();
    _kCtrl.text = _selectedPlot!.targetNpk.k.toString();
    setState(() => _isEditingNpk = true);
  }

  void _saveNpk() {
    if (_selectedPlot == null) return;
    final updated = _selectedPlot!.copyWith(
      targetNpk: NpkLevel(
        n: double.tryParse(_nCtrl.text) ?? _selectedPlot!.targetNpk.n,
        p: double.tryParse(_pCtrl.text) ?? _selectedPlot!.targetNpk.p,
        k: double.tryParse(_kCtrl.text) ?? _selectedPlot!.targetNpk.k,
      ),
    );
    setState(() {
      _plots = _plots.map((p) => p.id == updated.id ? updated : p).toList();
      _selectedPlot = updated;
      _isEditingNpk = false;
    });
  }

  void _showScanConfigDialog(BuildContext context, Color accentColor, bool isDark) {
    final colsCtrl = TextEditingController(text: "3");
    final rowsCtrl = TextEditingController(text: "3");
    final stepXCtrl = TextEditingController(text: "200");
    final stepYCtrl = TextEditingController(text: "200");
    final zHeightCtrl = TextEditingController(text: "200");

    final textColor = isDark ? Colors.white : Colors.black;
    final inputDecoration = InputDecoration(
      filled: true,
      fillColor: isDark ? const Color(0xFF374151) : Colors.grey.shade100,
      border: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide.none),
      contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
    );

    showDialog(
      context: context,
      builder: (ctx) {
        return AlertDialog(
          backgroundColor: isDark ? const Color(0xFF1F2937) : Colors.white,
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
          title: Row(
            children: [
              Icon(Icons.camera_alt, color: accentColor),
              const SizedBox(width: 8),
              Text("Configure Scan", style: TextStyle(color: textColor, fontWeight: FontWeight.bold)),
            ],
          ),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Row(
                  children: [
                    Expanded(child: _buildConfigField("Columns", colsCtrl, inputDecoration, textColor)),
                    const SizedBox(width: 12),
                    Expanded(child: _buildConfigField("Rows", rowsCtrl, inputDecoration, textColor)),
                  ],
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(child: _buildConfigField("Step X (mm)", stepXCtrl, inputDecoration, textColor)),
                    const SizedBox(width: 12),
                    Expanded(child: _buildConfigField("Step Y (mm)", stepYCtrl, inputDecoration, textColor)),
                  ],
                ),
                const SizedBox(height: 12),
                _buildConfigField("Camera Z-Height (mm)", zHeightCtrl, inputDecoration, textColor),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: Text("Cancel", style: TextStyle(color: isDark ? Colors.grey.shade400 : Colors.grey.shade600)),
            ),
            ElevatedButton(
              onPressed: () {
                final cols = int.tryParse(colsCtrl.text) ?? 3;
                final rows = int.tryParse(rowsCtrl.text) ?? 3;
                final stepX = double.tryParse(stepXCtrl.text) ?? 200.0;
                final stepY = double.tryParse(stepYCtrl.text) ?? 200.0;
                final zHeight = double.tryParse(zHeightCtrl.text) ?? 200.0;
                
                // Clear previous stitched images before starting a new scan
                setState(() {
                  _stitchedImages.clear();
                });

                ESP32Service.instance.startPhotoScan(cols, rows, stepX, stepY, zHeight);
                Navigator.pop(ctx);
              },
              style: ElevatedButton.styleFrom(
                backgroundColor: accentColor,
                foregroundColor: Colors.white,
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
              ),
              child: const Text("Start Scan", style: TextStyle(fontWeight: FontWeight.bold)),
            ),
          ],
        );
      },
    );
  }

  Widget _buildConfigField(String label, TextEditingController ctrl, InputDecoration decoration, Color textColor) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label, style: TextStyle(fontSize: 12, color: textColor, fontWeight: FontWeight.bold)),
        const SizedBox(height: 4),
        TextField(
          controller: ctrl,
          keyboardType: TextInputType.number,
          style: TextStyle(color: textColor, fontSize: 14),
          decoration: decoration,
        ),
      ],
    );
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
          Wrap(
            alignment: WrapAlignment.spaceBetween,
            crossAxisAlignment: WrapCrossAlignment.center,
            spacing: 16,
            runSpacing: 12,
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
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  ElevatedButton.icon(
                    onPressed: () {
                      ESP32Service.instance.startAutoDetect(3, 3, 200.0, 200.0, 200.0);
                    },
                    icon: const Icon(Icons.search, size: 16),
                    label: const Text('Auto-Detect'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.orange,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
                    ),
                  ),
                  ElevatedButton.icon(
                    onPressed: () => _showScanConfigDialog(context, accent, isDark),
                    icon: const Icon(Icons.camera_alt, size: 16),
                    label: const Text('Scan Bed'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: accent,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
                    ),
                  ),
                ],
              ),
            ],
          ),
          const SizedBox(height: 16),
          _buildGrid(accent, isDark, gridBg, borderColor),
          const SizedBox(height: 16),
          _buildDetail(accent, isDark, cardColor, borderColor, textColor, subTextColor),
          const SizedBox(height: 16),
          _buildConsole(isDark, cardColor, borderColor, textColor, subTextColor),
          const SizedBox(height: 32),
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
                // ── Image Stitching Overlay ──
                ..._stitchedImages.entries.map((entry) {
                  final parts = entry.key.split('_');
                  final x = double.parse(parts[0]);
                  final y = double.parse(parts[1]);

                  final double maxX = ESP32Service.instance.maxX > 0 ? ESP32Service.instance.maxX : kBedWidthMm;
                  final double maxY = ESP32Service.instance.maxY > 0 ? ESP32Service.instance.maxY : kBedHeightMm;
                  
                  // Approximate image size on screen based on step size used in startPhotoScan (200mm)
                  // In a real app we'd use groundW/groundH from the FRAME_META
                  final imgSizeW = (200.0 / maxX) * box.maxWidth;
                  final imgSizeH = (200.0 / maxY) * box.maxHeight;
                  
                  final cx = (x / maxX) * box.maxWidth - (imgSizeW / 2);
                  final cy = (y / maxY) * box.maxHeight - (imgSizeH / 2);

                  return Positioned(
                    left: cx,
                    top: cy,
                    width: imgSizeW,
                    height: imgSizeH,
                    child: Container(
                      decoration: BoxDecoration(
                        border: Border.all(color: Colors.white24, width: 0.5),
                      ),
                      child: Image.memory(
                        entry.value,
                        fit: BoxFit.cover,
                        gaplessPlayback: true,
                      ),
                    ),
                  );
                }),
                // ── Plant Dots ──
                ..._plots.map((plot) {
                  final isSelected = _selectedPlot?.id == plot.id;
                  // Map mm coordinates to screen pixels based on max bed dimensions
                  // We flip Y if the physical origin is bottom-left
                  final double maxX = ESP32Service.instance.maxX > 0 ? ESP32Service.instance.maxX : kBedWidthMm;
                  final double maxY = ESP32Service.instance.maxY > 0 ? ESP32Service.instance.maxY : kBedHeightMm;
                  
                  final cx = (plot.x / maxX) * box.maxWidth;
                  final cy = (plot.y / maxY) * box.maxHeight;
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
          _row('X', '${plot.x.toStringAsFixed(1)} mm', subTextColor, textColor),
          _row('Y', '${plot.y.toStringAsFixed(1)} mm', subTextColor, textColor),
          _row('Moisture', '${plot.moisture}%', subTextColor, textColor),
          const SizedBox(height: 16),
          Text('NPK LEVEL', style: TextStyle(fontSize: 10, color: subTextColor, letterSpacing: 2, fontWeight: FontWeight.bold)),
          const SizedBox(height: 4),
          Text('N: ${plot.npk.n}  |  P: ${plot.npk.p}  |  K: ${plot.npk.k}', style: TextStyle(color: textColor, fontWeight: FontWeight.bold)),
          const SizedBox(height: 16),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text('SET TARGET NPK', style: TextStyle(fontSize: 10, color: subTextColor, letterSpacing: 2, fontWeight: FontWeight.bold)),
              IconButton(icon: Icon(_isEditingNpk ? Icons.check_circle : Icons.edit, color: accent, size: 18), onPressed: _isEditingNpk ? _saveNpk : _startEditing),
            ],
          ),
          if (!_isEditingNpk)
            Text('N: ${plot.targetNpk.n}  |  P: ${plot.targetNpk.p}  |  K: ${plot.targetNpk.k}', style: TextStyle(color: textColor, fontWeight: FontWeight.bold))
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

  // ── Console ──────────────────────────────────────────────

  Widget _buildConsole(bool isDark, Color cardColor, Color borderColor, Color textColor, Color subTextColor) {
    final service = ESP32Service.instance;
    final logs = service.logs; // In a full app we might filter by specific tags

    return Container(
      decoration: BoxDecoration(
        color: cardColor,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: borderColor),
      ),
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.terminal, color: Color(0xFF60A5FA), size: 18),
              const SizedBox(width: 8),
              Text(
                "SYSTEM LOG",
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
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                decoration: BoxDecoration(
                  color: isDark ? Colors.white.withOpacity(0.06) : Colors.black.withOpacity(0.04),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Text(
                  "${logs.length} entries",
                  style: TextStyle(color: subTextColor, fontSize: 10, fontWeight: FontWeight.bold),
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          Container(
            height: 200,
            padding: const EdgeInsets.all(10),
            decoration: BoxDecoration(
              color: isDark ? const Color(0xFF030712) : const Color(0xFFF8FAFC),
              borderRadius: BorderRadius.circular(8),
              border: Border.all(color: borderColor),
            ),
            child: logs.isEmpty
                ? Center(
                    child: Text(
                      "Waiting for logs...",
                      style: TextStyle(color: subTextColor, fontSize: 11, fontStyle: FontStyle.italic),
                    ),
                  )
                : ListView.builder(
                    controller: _consoleScrollController,
                    itemCount: logs.length,
                    itemBuilder: (context, i) {
                      final log = logs[i];
                      return Padding(
                        padding: const EdgeInsets.only(bottom: 4),
                        child: Text(
                          log.message,
                          style: TextStyle(
                            color: _getLogColor(log.level, isDark),
                            fontFamily: 'monospace',
                            fontSize: 11,
                          ),
                        ),
                      );
                    },
                  ),
          ),
        ],
      ),
    );
  }

  Color _getLogColor(LogLevel level, bool isDark) {
    switch (level) {
      case LogLevel.error:
        return const Color(0xFFEF4444);
      case LogLevel.warn:
        return const Color(0xFFF59E0B);
      case LogLevel.success:
        return const Color(0xFF10B981);
      default:
        return isDark ? const Color(0xFF9CA3AF) : const Color(0xFF4B5563);
    }
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