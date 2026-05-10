// lib/screens/plot_map.dart

import 'dart:io';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:permission_handler/permission_handler.dart';
import '../providers/theme_provider.dart';

import '../models/plot_model.dart';
import '../service/ESP32/esp32_service.dart';

// ─────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────

const int kGridCols = 5;
const int kGridRows = 5;

const double kGridOverhangMm = 100.0; // Extra space around the physical bed

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
    ESP32Service.instance.addListener(_onServiceChange);

    // Fetch initial plant map — only if already authenticated.
    // If not yet connected, esp32_service sends GET_PLANT_MAP after AUTH_SUCCESS.
    if (ESP32Service.instance.isConnected) {
      ESP32Service.instance.sendCommand("GET_PLANT_MAP");
    }
  }

  void _onServiceChange() {
    _scrollToBottom();
    // Sync plots from service
    if (mounted) {
      setState(() {
        _plots = ESP32Service.instance.registeredPlots;
        // If selected plot was deleted, clear selection
        if (_selectedPlot != null &&
            !_plots.any((p) => p.id == _selectedPlot!.id)) {
          _selectedPlot = null;
        }
      });
    }
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
              Text(
                "Location: X:${x.toStringAsFixed(1)} Y:${y.toStringAsFixed(1)}",
              ),
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
                  _plots.add(
                    Plot(
                      id: DateTime.now().millisecondsSinceEpoch,
                      name: "AI_Weed",
                      x: x,
                      y: y,
                      moisture: 0,
                      npk: const NpkLevel(n: 0, p: 0, k: 0),
                      targetNpk: const NpkLevel(n: 0, p: 0, k: 0),
                      aiDetected: true,
                    ),
                  );
                });
                Navigator.pop(ctx);
              },
              child: const Text("Confirm"),
            ),
          ],
        );
      },
    );
  }

  void _handleFrameCaptured(Map<String, dynamic> data) {
    if (!mounted) return;
    try {
      final double x = double.parse(data['x'].toString());
      final double y = double.parse(data['y'].toString());
      final Uint8List image = data['image'] as Uint8List;

      setState(() {
        _stitchedImages["${x.toStringAsFixed(0)}_${y.toStringAsFixed(0)}"] =
            image;
      });
    } catch (e) {
      debugPrint("Error handling frame: $e");
    }
  }

  Future<void> _downloadAllImages() async {
    if (_stitchedImages.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('No scan images to download yet.')),
      );
      return;
    }

    // Request the correct storage permission depending on Android version.
    // Android 13+ (API 33): MANAGE_EXTERNAL_STORAGE
    // Android 12 and below: READ/WRITE_EXTERNAL_STORAGE (legacy)
    if (Platform.isAndroid) {
      bool granted = false;

      // Try the broad all-files permission first (Android 11+)
      final manageStatus = await Permission.manageExternalStorage.status;
      if (manageStatus.isGranted) {
        granted = true;
      } else {
        final requested = await Permission.manageExternalStorage.request();
        if (requested.isGranted) {
          granted = true;
        } else {
          // Fall back to legacy storage permission (Android ≤ 10)
          final legacyStatus = await Permission.storage.request();
          granted = legacyStatus.isGranted;
        }
      }

      if (!granted) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: const Text(
                'Storage permission denied.\n'
                'Go to App Settings → Permissions → Storage → Allow all.',
              ),
              backgroundColor: Colors.red,
              action: SnackBarAction(
                label: 'Settings',
                textColor: Colors.white,
                onPressed: () => openAppSettings(),
              ),
              duration: const Duration(seconds: 6),
            ),
          );
        }
        return;
      }
    }

    // Android: save directly to the public Downloads folder (no path_provider needed).
    // Non-Android: not supported in this build.
    if (!Platform.isAndroid) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Download is only supported on Android.'),
          ),
        );
      }
      return;
    }
    final saveDir = Directory('/storage/emulated/0/Download/Agri3D_Scan');

    await saveDir.create(recursive: true);

    int saved = 0;
    final ts = DateTime.now();
    final folderName =
        'scan_${ts.year}${ts.month.toString().padLeft(2, '0')}${ts.day.toString().padLeft(2, '0')}'
        '_${ts.hour.toString().padLeft(2, '0')}${ts.minute.toString().padLeft(2, '0')}';
    final sessionDir = Directory('${saveDir.path}/$folderName');
    await sessionDir.create(recursive: true);

    for (final entry in _stitchedImages.entries) {
      final file = File('${sessionDir.path}/frame_${entry.key}.jpg');
      await file.writeAsBytes(entry.value);
      saved++;
    }

    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            '✅ $saved images saved to Downloads/Agri3D_Scan/$folderName',
          ),
          duration: const Duration(seconds: 4),
          backgroundColor: const Color(0xFF22C55E),
        ),
      );
    }
  }

  @override
  void dispose() {
    ESP32Service.instance.removeListener(_onServiceChange);
    _consoleScrollController.dispose();
    _nCtrl.dispose();
    _pCtrl.dispose();
    _kCtrl.dispose();
    super.dispose();
  }

  void _selectPlot(Plot plot) => setState(() {
    _selectedPlot = plot;
    _isEditingNpk = false;
  });

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

  void _showScanConfigDialog(
    BuildContext context,
    Color accentColor,
    bool isDark,
  ) {
    final colsCtrl = TextEditingController(text: "3");
    final rowsCtrl = TextEditingController(text: "3");
    final stepXCtrl = TextEditingController(text: "200");
    final stepYCtrl = TextEditingController(text: "200");
    final zHeightCtrl = TextEditingController(text: "200");
    final offsetCtrl = TextEditingController(
      text: ESP32Service.instance.cameraOffset.toStringAsFixed(0),
    );

    final textColor = isDark ? Colors.white : Colors.black;
    final inputDecoration = InputDecoration(
      filled: true,
      fillColor: isDark ? const Color(0xFF374151) : Colors.grey.shade100,
      border: OutlineInputBorder(
        borderRadius: BorderRadius.circular(8),
        borderSide: BorderSide.none,
      ),
      contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
    );

    showDialog(
      context: context,
      builder: (ctx) {
        return AlertDialog(
          backgroundColor: isDark ? const Color(0xFF1F2937) : Colors.white,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(16),
          ),
          title: Row(
            children: [
              Icon(Icons.camera_alt, color: accentColor),
              const SizedBox(width: 8),
              Text(
                "Configure Scan",
                style: TextStyle(color: textColor, fontWeight: FontWeight.bold),
              ),
            ],
          ),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Row(
                  children: [
                    Expanded(
                      child: _buildConfigField(
                        "Columns",
                        colsCtrl,
                        inputDecoration,
                        textColor,
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: _buildConfigField(
                        "Rows",
                        rowsCtrl,
                        inputDecoration,
                        textColor,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(
                      child: _buildConfigField(
                        "Step X (mm)",
                        stepXCtrl,
                        inputDecoration,
                        textColor,
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: _buildConfigField(
                        "Step Y (mm)",
                        stepYCtrl,
                        inputDecoration,
                        textColor,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                _buildConfigField(
                  "Camera Z-Height (mm)",
                  zHeightCtrl,
                  inputDecoration,
                  textColor,
                ),
                const SizedBox(height: 12),
                _buildConfigField(
                  "Camera Offset (mm)",
                  offsetCtrl,
                  inputDecoration,
                  textColor,
                ),
                const SizedBox(height: 6),
                Text(
                  "Physical offset from gantry centre to camera lens centre.",
                  style: TextStyle(
                    fontSize: 11,
                    color: textColor.withOpacity(0.5),
                  ),
                ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: Text(
                "Cancel",
                style: TextStyle(
                  color: isDark ? Colors.grey.shade400 : Colors.grey.shade600,
                ),
              ),
            ),
            ElevatedButton(
              onPressed: () {
                final cols = int.tryParse(colsCtrl.text) ?? 3;
                final rows = int.tryParse(rowsCtrl.text) ?? 3;
                final stepX = double.tryParse(stepXCtrl.text) ?? 200.0;
                final stepY = double.tryParse(stepYCtrl.text) ?? 200.0;
                final zHeight = double.tryParse(zHeightCtrl.text) ?? 200.0;
                final offset = double.tryParse(offsetCtrl.text) ?? 100.0;

                // Send offset to ESP32 before starting scan so it takes effect
                if (offset != ESP32Service.instance.cameraOffset) {
                  ESP32Service.instance.setCamOffset(offset);
                }

                // Clear previous stitched images before starting a new scan
                setState(() {
                  _stitchedImages.clear();
                });

                ESP32Service.instance.startPhotoScan(
                  cols,
                  rows,
                  stepX,
                  stepY,
                  zHeight,
                );
                Navigator.pop(ctx);
              },
              style: ElevatedButton.styleFrom(
                backgroundColor: accentColor,
                foregroundColor: Colors.white,
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(8),
                ),
              ),
              child: const Text(
                "Start Scan",
                style: TextStyle(fontWeight: FontWeight.bold),
              ),
            ),
          ],
        );
      },
    );
  }

  void _showAddPlantDialog(
    BuildContext context,
    Color accentColor,
    bool isDark,
  ) {
    final nameCtrl = TextEditingController(text: "New Plant");
    final xCtrl = TextEditingController();
    final yCtrl = TextEditingController();
    final rosetteCtrl = TextEditingController(text: "150");
    final dxCtrl = TextEditingController();
    final dyCtrl = TextEditingController();
    CropType selectedCrop = CropType.none;

    final textColor = isDark ? Colors.white : Colors.black;
    final inputDecoration = InputDecoration(
      filled: true,
      fillColor: isDark ? const Color(0xFF374151) : Colors.grey.shade100,
      border: OutlineInputBorder(
        borderRadius: BorderRadius.circular(8),
        borderSide: BorderSide.none,
      ),
      contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
    );

    showDialog(
      context: context,
      builder: (ctx) {
        return StatefulBuilder(
          builder: (context, setState) {
            return AlertDialog(
              backgroundColor: isDark ? const Color(0xFF1F2937) : Colors.white,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(16),
          ),
          title: Row(
            children: [
              Icon(Icons.add_location_alt, color: accentColor),
              const SizedBox(width: 8),
              Text(
                "Add Plant",
                style: TextStyle(color: textColor, fontWeight: FontWeight.bold),
              ),
            ],
          ),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                _buildConfigField("Name", nameCtrl, inputDecoration, textColor),
                const SizedBox(height: 12),
                Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      "Crop Type",
                      style: TextStyle(
                        fontSize: 12,
                        color: textColor,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 4),
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 12),
                      decoration: BoxDecoration(
                        color: isDark ? const Color(0xFF374151) : Colors.grey.shade100,
                        borderRadius: BorderRadius.circular(8),
                      ),
                      child: DropdownButtonHideUnderline(
                        child: DropdownButton<CropType>(
                          value: selectedCrop,
                          isExpanded: true,
                          dropdownColor: isDark ? const Color(0xFF374151) : Colors.white,
                          style: TextStyle(color: textColor),
                          items: CropType.values.map((CropType type) {
                            return DropdownMenuItem<CropType>(
                              value: type,
                              child: Text(type.label),
                            );
                          }).toList(),
                          onChanged: (CropType? newValue) {
                            if (newValue != null) {
                              setState(() {
                                selectedCrop = newValue;
                              });
                            }
                          },
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(
                      child: _buildConfigField(
                        "X (mm)",
                        xCtrl,
                        inputDecoration,
                        textColor,
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: _buildConfigField(
                        "Y (mm)",
                        yCtrl,
                        inputDecoration,
                        textColor,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                _buildConfigField(
                  "Rosette Ø (mm)",
                  rosetteCtrl,
                  inputDecoration,
                  textColor,
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(
                      child: _buildConfigField(
                        "Dip X (mm)",
                        dxCtrl,
                        inputDecoration,
                        textColor,
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: _buildConfigField(
                        "Dip Y (mm)",
                        dyCtrl,
                        inputDecoration,
                        textColor,
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: Text(
                "Cancel",
                style: TextStyle(
                  color: isDark ? Colors.grey.shade400 : Colors.grey.shade600,
                ),
              ),
            ),
            ElevatedButton(
              onPressed: () {
                final x = double.tryParse(xCtrl.text) ?? 0.0;
                final y = double.tryParse(yCtrl.text) ?? 0.0;
                final dx = double.tryParse(dxCtrl.text) ?? 0.0;
                final dy = double.tryParse(dyCtrl.text) ?? 0.0;
                final r = double.tryParse(rosetteCtrl.text) ?? 150.0;
                final name = nameCtrl.text.isNotEmpty ? nameCtrl.text : "Plant";

                final newPlot = Plot(
                  id: 0, // Assigned by ESP32 or not used initially
                  name: name,
                  x: x,
                  y: y,
                  dx: dx,
                  dy: dy,
                  cropType: selectedCrop,
                  rosetteDiameter: r,
                  moisture: 0,
                  npk: const NpkLevel(n: 0, p: 0, k: 0),
                  targetNpk: const NpkLevel(n: 0, p: 0, k: 0),
                );

                ESP32Service.instance.registerPlant(newPlot);

                Navigator.pop(ctx);
              },
              style: ElevatedButton.styleFrom(
                backgroundColor: accentColor,
                foregroundColor: Colors.white,
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(8),
                ),
              ),
              child: const Text(
                "Add",
                style: TextStyle(fontWeight: FontWeight.bold),
              ),
            ),
          ],
        );
        });
      },
    );
  }

  Widget _buildConfigField(
    String label,
    TextEditingController ctrl,
    InputDecoration decoration,
    Color textColor,
  ) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label,
          style: TextStyle(
            fontSize: 12,
            color: textColor,
            fontWeight: FontWeight.bold,
          ),
        ),
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
    final service = ESP32Service.instance;

    // Adaptive Theme Variables
    final textColor = isDark ? Colors.white : const Color(0xFF111827);
    final subTextColor = isDark
        ? const Color(0xFF9CA3AF)
        : const Color(0xFF4B5563);
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
                  style: TextStyle(
                    fontSize: 22,
                    fontWeight: FontWeight.w900,
                    fontStyle: FontStyle.italic,
                    color: textColor,
                  ),
                  children: [
                    const TextSpan(text: 'PLOT '),
                    TextSpan(
                      text: 'GRID MAP',
                      style: TextStyle(color: accent),
                    ),
                  ],
                ),
              ),
              ElevatedButton.icon(
                onPressed: () => _showAddPlantDialog(context, accent, isDark),
                icon: const Icon(Icons.add_location_alt, size: 16),
                label: const Text('Add Plant'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: const Color(0xFF10B981),
                  foregroundColor: Colors.white,
                  padding: const EdgeInsets.symmetric(
                    horizontal: 12,
                    vertical: 8,
                  ),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(20),
                  ),
                ),
              ),
              ElevatedButton.icon(
                onPressed: service.isScanning || service.isUploadingScan
                    ? null
                    : () {
                        ESP32Service.instance.startAutoDetect(
                          3,
                          3,
                          200.0,
                          200.0,
                          200.0,
                        );
                      },
                icon: const Icon(Icons.search, size: 16),
                label: const Text('Auto-Detect'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.orange,
                  foregroundColor: Colors.white,
                  disabledBackgroundColor: Colors.orange.withOpacity(0.35),
                  padding: const EdgeInsets.symmetric(
                    horizontal: 12,
                    vertical: 8,
                  ),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(20),
                  ),
                ),
              ),
              ElevatedButton.icon(
                onPressed: service.isScanning || service.isUploadingScan
                    ? null
                    : () {
                        ESP32Service.instance.sendCommand("DIP_ALL_PLANTS");
                      },
                icon: const Icon(Icons.water_drop, size: 16),
                label: const Text('Dip NPK'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.blue,
                  foregroundColor: Colors.white,
                  disabledBackgroundColor: Colors.blue.withOpacity(0.35),
                  padding: const EdgeInsets.symmetric(
                    horizontal: 12,
                    vertical: 8,
                  ),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(20),
                  ),
                ),
              ),
              ElevatedButton.icon(
                onPressed: service.isScanning || service.isUploadingScan
                    ? null
                    : () => _showScanConfigDialog(context, accent, isDark),
                icon: const Icon(Icons.camera_alt, size: 16),
                label: const Text('Scan Bed'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: accent,
                  foregroundColor: Colors.white,
                  disabledBackgroundColor: accent.withOpacity(0.35),
                  padding: const EdgeInsets.symmetric(
                    horizontal: 12,
                    vertical: 8,
                  ),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(20),
                  ),
                ),
              ),
              ElevatedButton.icon(
                onPressed: _stitchedImages.isEmpty ? null : _downloadAllImages,
                icon: const Icon(Icons.download, size: 16),
                label: Text(
                  _stitchedImages.isEmpty
                      ? 'Download'
                      : 'Download (${_stitchedImages.length})',
                ),
                style: ElevatedButton.styleFrom(
                  backgroundColor: const Color(0xFF6366F1),
                  foregroundColor: Colors.white,
                  disabledBackgroundColor: const Color(
                    0xFF6366F1,
                  ).withOpacity(0.35),
                  padding: const EdgeInsets.symmetric(
                    horizontal: 12,
                    vertical: 8,
                  ),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(20),
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          // ── 3-phase scan status bar ──
          _buildScanStatus(
            service,
            accent,
            isDark,
            cardColor,
            borderColor,
            textColor,
            subTextColor,
          ),
          const SizedBox(height: 12),
          _buildGrid(accent, isDark, gridBg, borderColor),
          const SizedBox(height: 16),
          _buildDetail(
            accent,
            isDark,
            cardColor,
            borderColor,
            textColor,
            subTextColor,
          ),
          const SizedBox(height: 16),
          _buildConsole(
            isDark,
            cardColor,
            borderColor,
            textColor,
            subTextColor,
          ),
          const SizedBox(height: 32),
        ],
      ),
    );
  }

  // ── 3-Phase Scan Status Bar ──────────────────────────────────────────────

  Widget _buildScanStatus(
    ESP32Service service,
    Color accent,
    bool isDark,
    Color cardColor,
    Color borderColor,
    Color textColor,
    Color subTextColor,
  ) {
    // Phase 1: actively scanning (saving to SD)
    if (service.scanProgress > 0 &&
        service.scanProgress < 1.0 &&
        !service.isScanReady) {
      return _buildProgressCard(
        icon: Icons.camera_alt,
        iconColor: accent,
        title: 'SCANNING BED…',
        subtitle:
            'Frame ${service.scanFrameIdx} of ${service.scanFrameTotal} — saving to SD card',
        progress: service.scanProgress,
        progressColor: accent,
        isDark: isDark,
        cardColor: cardColor,
        borderColor: borderColor,
        textColor: textColor,
        subTextColor: subTextColor,
      );
    }

    // Phase 2a: scan complete (or interrupted), waiting for user to tap Upload/Resume
    if (service.isScanReady && !service.isUploadingScan) {
      final bool isResume = _stitchedImages.isNotEmpty;
      return Container(
        width: double.infinity,
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        decoration: BoxDecoration(
          color: isResume
              ? const Color(0xFF1E3A5F).withOpacity(isDark ? 0.4 : 0.12)
              : const Color(0xFF14532D).withOpacity(isDark ? 0.4 : 0.12),
          borderRadius: BorderRadius.circular(12),
          border: Border.all(
            color: isResume
                ? const Color(0xFF60A5FA).withOpacity(0.5)
                : const Color(0xFF22C55E).withOpacity(0.5),
          ),
        ),
        child: Row(
          children: [
            Icon(
              isResume ? Icons.warning_amber_rounded : Icons.check_circle,
              color: isResume
                  ? const Color(0xFF60A5FA)
                  : const Color(0xFF22C55E),
              size: 20,
            ),
            const SizedBox(width: 10),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    isResume ? 'UPLOAD INTERRUPTED' : 'SCAN COMPLETE',
                    style: TextStyle(
                      color: isResume
                          ? const Color(0xFF60A5FA)
                          : const Color(0xFF22C55E),
                      fontSize: 11,
                      fontWeight: FontWeight.w900,
                      letterSpacing: 1,
                    ),
                  ),
                  Text(
                    isResume
                        ? '${_stitchedImages.length}/${service.scanFrameTotal} frames received — tap to resume'
                        : '${service.scanFrameTotal} frames saved on SD card',
                    style: TextStyle(color: subTextColor, fontSize: 12),
                  ),
                ],
              ),
            ),
            ElevatedButton.icon(
              onPressed: () {
                // On resume, keep partially received images; on fresh start clear them.
                if (!isResume) setState(() => _stitchedImages.clear());
                ESP32Service.instance.startScanUpload();
              },
              icon: Icon(isResume ? Icons.replay : Icons.upload, size: 14),
              label: Text(
                isResume ? 'Resume Upload' : 'Upload Plant Map',
                style: const TextStyle(fontSize: 12),
              ),
              style: ElevatedButton.styleFrom(
                backgroundColor: isResume
                    ? const Color(0xFF60A5FA)
                    : const Color(0xFF22C55E),
                foregroundColor: Colors.white,
                padding: const EdgeInsets.symmetric(
                  horizontal: 12,
                  vertical: 8,
                ),
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(20),
                ),
              ),
            ),
          ],
        ),
      );
    }

    // Phase 2b: uploading frames to Flutter
    if (service.isUploadingScan) {
      return _buildProgressCard(
        icon: Icons.upload,
        iconColor: const Color(0xFF60A5FA),
        title: 'UPLOADING PLANT MAP…',
        subtitle: 'Receiving frames from SD card…',
        progress: service.uploadScanProgress,
        progressColor: const Color(0xFF60A5FA),
        isDark: isDark,
        cardColor: cardColor,
        borderColor: borderColor,
        textColor: textColor,
        subTextColor: subTextColor,
      );
    }

    return const SizedBox.shrink();
  }

  Widget _buildProgressCard({
    required IconData icon,
    required Color iconColor,
    required String title,
    required String subtitle,
    required double progress,
    required Color progressColor,
    required bool isDark,
    required Color cardColor,
    required Color borderColor,
    required Color textColor,
    required Color subTextColor,
  }) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: cardColor,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: borderColor),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, color: iconColor, size: 16),
              const SizedBox(width: 8),
              Text(
                title,
                style: TextStyle(
                  color: textColor,
                  fontSize: 11,
                  fontWeight: FontWeight.w900,
                  letterSpacing: 1,
                ),
              ),
              const Spacer(),
              Text(
                '${(progress * 100).toStringAsFixed(0)}%',
                style: TextStyle(
                  color: iconColor,
                  fontSize: 12,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          ClipRRect(
            borderRadius: BorderRadius.circular(4),
            child: LinearProgressIndicator(
              value: progress,
              backgroundColor: isDark
                  ? const Color(0xFF374151)
                  : Colors.grey.shade200,
              valueColor: AlwaysStoppedAnimation<Color>(progressColor),
              minHeight: 6,
            ),
          ),
          const SizedBox(height: 6),
          Text(subtitle, style: TextStyle(color: subTextColor, fontSize: 11)),
        ],
      ),
    );
  }

  // ── Grid ─────────────────────────────────────────────────

  Widget _buildGrid(
    Color accent,
    bool isDark,
    Color gridBg,
    Color borderColor,
  ) {
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
            final service = ESP32Service.instance;
            final double maxX = service.maxX > 0 ? service.maxX : 1000.0;
            final double maxY = service.maxY > 0 ? service.maxY : 1000.0;

            // Total logical space including overhang
            final totalW = maxX + (2 * kGridOverhangMm);
            final totalH = maxY + (2 * kGridOverhangMm);

            // Coordinate mapping:
            // X-positive is LEFT (0 is right, MaxX is left)
            // Y-positive is DOWN (0 is top, MaxY is bottom)
            Offset mapToScreen(double px, double py) {
              // uiX = ((MaxX + Overhang) - px) / TotalW * boxWidth
              final uiX =
                  ((maxX + kGridOverhangMm) - px) / totalW * box.maxWidth;
              // uiY = (py + Overhang) / TotalH * boxHeight
              final uiY = (py + kGridOverhangMm) / totalH * box.maxHeight;
              return Offset(uiX, uiY);
            }

            return Stack(
              clipBehavior: Clip.none,
              children: [
                // ── Physical Bed Boundary ──
                Positioned(
                  left: mapToScreen(maxX, 0).dx,
                  top: mapToScreen(maxX, 0).dy,
                  width: (maxX / totalW) * box.maxWidth,
                  height: (maxY / totalH) * box.maxHeight,
                  child: Container(
                    decoration: BoxDecoration(
                      border: Border.all(
                        color: accent.withOpacity(0.3),
                        width: 2,
                      ),
                      color: isDark
                          ? Colors.white.withOpacity(0.02)
                          : Colors.black.withOpacity(0.02),
                    ),
                  ),
                ),
                CustomPaint(
                  size: Size(box.maxWidth, box.maxHeight),
                  painter: _GridPainter(
                    cols: kGridCols,
                    rows: kGridRows,
                    color: borderColor.withOpacity(0.3),
                  ),
                ),
                // ── Image Stitching Overlay ──
                ..._stitchedImages.entries.map((entry) {
                  final parts = entry.key.split('_');
                  final px = double.parse(parts[0]);
                  final py = double.parse(parts[1]);

                  // Image size (approx 200mm)
                  final imgSizeW = (200.0 / totalW) * box.maxWidth;
                  final imgSizeH = (200.0 / totalH) * box.maxHeight;

                  final pos = mapToScreen(px, py);

                  return Positioned(
                    left: pos.dx - (imgSizeW / 2),
                    top: pos.dy - (imgSizeH / 2),
                    width: imgSizeW,
                    height: imgSizeH,
                    child: Container(
                      decoration: BoxDecoration(
                        border: Border.all(color: Colors.white24, width: 0.5),
                      ),
                      child: Transform.rotate(
                        angle:
                            3.141592653589793, // 180° — camera is mounted upside-down
                        child: Image.memory(
                          entry.value,
                          fit: BoxFit.cover,
                          gaplessPlayback: true,
                        ),
                      ),
                    ),
                  );
                }),
                // ── Plant Dots & Exclusion Zones ──
                ..._plots.map((plot) {
                  final isSelected = _selectedPlot?.id == plot.id;
                  final pos = mapToScreen(plot.x, plot.y);
                  final dotR = isSelected ? 10.0 : 7.0;

                  // Exclusion zone radius in pixels
                  final exclusionR =
                      (plot.rosetteDiameter / 2.0 / totalW) * box.maxWidth;

                  return Stack(
                    clipBehavior: Clip.none,
                    children: [
                      // Rosette Diameter Exclusion Zone
                      if (plot.rosetteDiameter > 0)
                        Positioned(
                          left: pos.dx - exclusionR,
                          top: pos.dy - exclusionR,
                          child: IgnorePointer(
                            child: Container(
                              width: exclusionR * 2,
                              height: exclusionR * 2,
                              decoration: BoxDecoration(
                                shape: BoxShape.circle,
                                color: const Color(0xFF22C55E).withOpacity(0.1),
                                border: Border.all(
                                  color: const Color(
                                    0xFF22C55E,
                                  ).withOpacity(0.3),
                                  width: 1,
                                ),
                              ),
                            ),
                          ),
                        ),
                      // Dip Coordinates Indicator
                      if (plot.dx != 0 || plot.dy != 0)
                        Positioned(
                          left: mapToScreen(plot.dx, plot.dy).dx - 4,
                          top: mapToScreen(plot.dx, plot.dy).dy - 4,
                          child: IgnorePointer(
                            child: Container(
                              width: 8,
                              height: 8,
                              decoration: BoxDecoration(
                                shape: BoxShape.circle,
                                color: Colors.blueAccent,
                                border: Border.all(color: Colors.white, width: 1),
                              ),
                            ),
                          ),
                        ),
                      // The Dot
                      Positioned(
                        left: pos.dx - dotR,
                        top: pos.dy - dotR,
                        child: GestureDetector(
                          onTap: () => _selectPlot(plot),
                          child: Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              AnimatedContainer(
                                duration: const Duration(milliseconds: 150),
                                width: dotR * 2,
                                height: dotR * 2,
                                decoration: BoxDecoration(
                                  shape: BoxShape.circle,
                                  color: isSelected
                                      ? accent
                                      : const Color(0xFF22C55E),
                                  border: Border.all(
                                    color: isSelected
                                        ? Colors.white
                                        : const Color(0xFF4ADE80),
                                    width: isSelected ? 2 : 1,
                                  ),
                                  boxShadow: [
                                    BoxShadow(
                                      color:
                                          (isSelected
                                                  ? accent
                                                  : const Color(0xFF22C55E))
                                              .withOpacity(0.5),
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
                                  fontSize: 8,
                                  fontWeight: FontWeight.bold,
                                  color: isSelected
                                      ? accent
                                      : (isDark
                                            ? const Color(0xFF9CA3AF)
                                            : const Color(0xFF4B5563)),
                                  letterSpacing: 0.5,
                                  backgroundColor: isDark
                                      ? Colors.black54
                                      : Colors.white70,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ],
                  );
                }),
                // ── Live Gantry Dot ──
                Builder(
                  builder: (ctx) {
                    final gantryPos = mapToScreen(service.x, service.y);
                    return Positioned(
                      left: gantryPos.dx - 5,
                      top: gantryPos.dy - 5,
                      child: Container(
                        width: 10,
                        height: 10,
                        decoration: BoxDecoration(
                          color: Colors.red,
                          shape: BoxShape.circle,
                          boxShadow: [
                            BoxShadow(
                              color: Colors.red.withOpacity(0.6),
                              blurRadius: 8,
                              spreadRadius: 2,
                            ),
                          ],
                          border: Border.all(color: Colors.white, width: 1.5),
                        ),
                      ),
                    );
                  },
                ),
              ],
            );
          },
        ),
      ),
    );
  }

  // ── Detail panel ─────────────────────────────────────────

  Widget _buildDetail(
    Color accent,
    bool isDark,
    Color cardColor,
    Color borderColor,
    Color textColor,
    Color subTextColor,
  ) {
    if (_selectedPlot == null) {
      return Container(
        width: double.infinity,
        padding: const EdgeInsets.symmetric(vertical: 32),
        decoration: BoxDecoration(
          color: cardColor,
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: borderColor),
        ),
        child: Center(
          child: Text(
            'Select a plot on the grid',
            style: TextStyle(color: subTextColor),
          ),
        ),
      );
    }

    final plot = _selectedPlot!;
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: cardColor,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: borderColor),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            '${plot.name} DETAILS',
            style: TextStyle(
              color: textColor,
              fontSize: 16,
              fontWeight: FontWeight.w900,
              letterSpacing: 1,
            ),
          ),
          const SizedBox(height: 12),
          _row('X', '${plot.x.toStringAsFixed(1)} mm', subTextColor, textColor),
          _row('Y', '${plot.y.toStringAsFixed(1)} mm', subTextColor, textColor),
          _row(
            'Rosette Ø',
            '${plot.rosetteDiameter.toStringAsFixed(1)} mm',
            subTextColor,
            textColor,
          ),
          const SizedBox(height: 16),
          Row(
            children: [
              Text(
                'NPK LEVEL',
                style: TextStyle(
                  fontSize: 10,
                  color: subTextColor,
                  letterSpacing: 2,
                  fontWeight: FontWeight.bold,
                ),
              ),
              const Spacer(),
              TextButton.icon(
                onPressed: () => ESP32Service.instance.deletePlant(plot.id),
                icon: const Icon(
                  Icons.delete_outline,
                  size: 14,
                  color: Colors.red,
                ),
                label: const Text(
                  'DELETE',
                  style: TextStyle(
                    fontSize: 10,
                    color: Colors.red,
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 4),
          Text(
            'N: ${plot.npk.n}  |  P: ${plot.npk.p}  |  K: ${plot.npk.k}',
            style: TextStyle(color: textColor, fontWeight: FontWeight.bold),
          ),
          const SizedBox(height: 16),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                'SET TARGET NPK',
                style: TextStyle(
                  fontSize: 10,
                  color: subTextColor,
                  letterSpacing: 2,
                  fontWeight: FontWeight.bold,
                ),
              ),
              IconButton(
                icon: Icon(
                  _isEditingNpk ? Icons.check_circle : Icons.edit,
                  color: accent,
                  size: 18,
                ),
                onPressed: _isEditingNpk ? _saveNpk : _startEditing,
              ),
            ],
          ),
          if (!_isEditingNpk)
            Text(
              'N: ${plot.targetNpk.n}  |  P: ${plot.targetNpk.p}  |  K: ${plot.targetNpk.k}',
              style: TextStyle(color: textColor, fontWeight: FontWeight.bold),
            )
          else
            Column(
              children: [
                Row(
                  children: [
                    _npkField(
                      'N',
                      _nCtrl,
                      accent,
                      isDark,
                      subTextColor,
                      textColor,
                    ),
                    const SizedBox(width: 8),
                    _npkField(
                      'P',
                      _pCtrl,
                      accent,
                      isDark,
                      subTextColor,
                      textColor,
                    ),
                    const SizedBox(width: 8),
                    _npkField(
                      'K',
                      _kCtrl,
                      accent,
                      isDark,
                      subTextColor,
                      textColor,
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                SizedBox(
                  width: double.infinity,
                  child: ElevatedButton(
                    onPressed: _saveNpk,
                    style: ElevatedButton.styleFrom(
                      backgroundColor: accent,
                      foregroundColor: Colors.white,
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(8),
                      ),
                    ),
                    child: const Text(
                      'SAVE TARGETS',
                      style: TextStyle(
                        fontWeight: FontWeight.bold,
                        letterSpacing: 1,
                      ),
                    ),
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
    child: Row(
      children: [
        Text('$label: ', style: TextStyle(color: sub, fontSize: 13)),
        Text(
          value,
          style: TextStyle(
            color: main,
            fontSize: 13,
            fontWeight: FontWeight.bold,
          ),
        ),
      ],
    ),
  );

  Widget _npkField(
    String label,
    TextEditingController ctrl,
    Color accent,
    bool isDark,
    Color sub,
    Color main,
  ) {
    return Expanded(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            label,
            style: TextStyle(
              fontSize: 10,
              color: accent,
              fontWeight: FontWeight.bold,
            ),
          ),
          const SizedBox(height: 4),
          TextField(
            controller: ctrl,
            keyboardType: TextInputType.number,
            style: TextStyle(color: main, fontSize: 13),
            decoration: InputDecoration(
              filled: true,
              fillColor: isDark
                  ? const Color(0xFF111827)
                  : Colors.grey.shade100,
              contentPadding: const EdgeInsets.symmetric(
                horizontal: 10,
                vertical: 8,
              ),
              border: OutlineInputBorder(
                borderRadius: BorderRadius.circular(8),
                borderSide: BorderSide.none,
              ),
              focusedBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(8),
                borderSide: BorderSide(color: accent),
              ),
            ),
          ),
        ],
      ),
    );
  }

  // ── Console ──────────────────────────────────────────────

  Widget _buildConsole(
    bool isDark,
    Color cardColor,
    Color borderColor,
    Color textColor,
    Color subTextColor,
  ) {
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
                  color: isDark
                      ? Colors.white.withOpacity(0.06)
                      : Colors.black.withOpacity(0.04),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Text(
                  "${logs.length} entries",
                  style: TextStyle(
                    color: subTextColor,
                    fontSize: 10,
                    fontWeight: FontWeight.bold,
                  ),
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
                      style: TextStyle(
                        color: subTextColor,
                        fontSize: 11,
                        fontStyle: FontStyle.italic,
                      ),
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
  final int cols;
  final int rows;
  final Color color;
  const _GridPainter({
    required this.cols,
    required this.rows,
    required this.color,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = color
      ..strokeWidth = 0.8;
    final cellW = size.width / cols;
    final cellH = size.height / rows;
    for (int c = 0; c <= cols; c++) {
      canvas.drawLine(
        Offset(c * cellW, 0),
        Offset(c * cellW, size.height),
        paint,
      );
    }
    for (int r = 0; r <= rows; r++) {
      canvas.drawLine(
        Offset(0, r * cellH),
        Offset(size.width, r * cellH),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(covariant _GridPainter old) => old.color != color;
}
