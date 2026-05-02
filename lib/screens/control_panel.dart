// lib/screens/control_panel.dart

import 'dart:async';
import 'dart:io'; 
import 'package:flutter/material.dart';
import 'package:flutter/services.dart'; 
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:intl/intl.dart'; 
import 'package:file_picker/file_picker.dart'; 

import '../providers/theme_provider.dart';
import '../service/ESP32/esp32_service.dart'; 
import 'dart:io';
import 'package:file_picker/file_picker.dart';

class ControlPanel extends ConsumerStatefulWidget {
  const ControlPanel({super.key});

  @override
  ConsumerState<ControlPanel> createState() => _ControlPanelState();
}

class _ControlPanelState extends ConsumerState<ControlPanel> {
  double _speedMode = 1; 
  double _feedrate  = 1000; 
  bool _isIrrigating = false;
  double _fertilizeAmount = 50; 
  bool   _isFertilizing   = false;
  Timer? _fertilizeTimer;
  bool _isWeederOn = false;

  DateTime? _lastMoveTime; 
  bool _isScanning = false;
  final TextEditingController _terminalController = TextEditingController();
  final ScrollController _terminalScrollController = ScrollController();

  bool _isBusy = false;

  final List<String> _commandHistory = [];
  int _historyIndex = -1;
  final FocusNode _terminalFocusNode = FocusNode(); 

  double _targetX = 0;
  double _targetY = 0;
  double _targetZ = 0;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) { _startScan(); });
    ESP32Service.instance.addListener(_scrollToBottom);
    ESP32Service.instance.addListener(_onServiceUpdate); 
  }

  @override
  void dispose() {
    ESP32Service.instance.removeListener(_scrollToBottom);
    ESP32Service.instance.removeListener(_onServiceUpdate); 
    _terminalController.dispose();
    _terminalScrollController.dispose();
    _fertilizeTimer?.cancel();
    _terminalFocusNode.dispose(); 
    super.dispose();
  }

  // ── CNC G-CODE FILE UPLOAD LOGIC ──
  Future<void> _pickAndRunGcode() async {
    try {
      FilePickerResult? result = await FilePicker.platform.pickFiles(type: FileType.any);
      if (result != null && result.files.single.path != null) {
        File file = File(result.files.single.path!);
        String gcodeText = await file.readAsString();
        
        ESP32Service.instance.startGcodeJob(gcodeText);
        
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text("G-Code Job Started!"), backgroundColor: Colors.green),
          );
        }
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text("Error reading file: $e"), backgroundColor: Colors.red),
        );
      }
    }
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

  void _handleHistoryKey(RawKeyEvent event) {
    if (event is! RawKeyDownEvent) return;

    if (event.logicalKey == LogicalKeyboardKey.arrowUp) {
      if (_commandHistory.isNotEmpty && _historyIndex < _commandHistory.length - 1) {
        setState(() {
          _historyIndex++;
          _terminalController.text = _commandHistory[_commandHistory.length - 1 - _historyIndex];
          _terminalController.selection = TextSelection.fromPosition(TextPosition(offset: _terminalController.text.length));
        });
      }
    } else if (event.logicalKey == LogicalKeyboardKey.arrowDown) {
      if (_historyIndex > 0) {
        setState(() {
          _historyIndex--;
          _terminalController.text = _commandHistory[_commandHistory.length - 1 - _historyIndex];
          _terminalController.selection = TextSelection.fromPosition(TextPosition(offset: _terminalController.text.length));
        });
      } else if (_historyIndex == 0) {
        setState(() {
          _historyIndex = -1;
          _terminalController.clear();
        });
      }
    }
  }

  Future<void> _startScan() async {
    if (_isScanning) return;
    setState(() => _isScanning = true);
    HapticFeedback.mediumImpact();
    await ESP32Service.instance.autoDiscover();
    if (mounted) setState(() => _isScanning = false);
  }

  void _scrollToBottom() {
    Future.delayed(const Duration(milliseconds: 50), () {
      if (_terminalScrollController.hasClients) {
        _terminalScrollController.animateTo(
          _terminalScrollController.position.maxScrollExtent,
          duration: const Duration(milliseconds: 300),
          curve: Curves.easeOut,
        );
      }
    });
  }

  void _move(String axis, int direction) {
    if (_isBusy) return; 

    final now = DateTime.now();
    if (_lastMoveTime != null && now.difference(_lastMoveTime!).inMilliseconds < 200) return;
    _lastMoveTime = now;

    final service = ESP32Service.instance;

    if (service.machineState == 'Alarm') {
      service.addLog("SYS: Move rejected. Machine not homed.");
      HapticFeedback.heavyImpact(); 
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Motors locked! Press "HOME ALL" to unlock.'), backgroundColor: Color(0xFFEF4444), duration: Duration(seconds: 2)));
      return; 
    }

    final delta = direction * _speedMode;
    double nextVal = 0;
    double currentMax = 1000.0;

    switch (axis) {
      case 'x': nextVal = service.x + delta; currentMax = service.maxX; break;
      case 'y': nextVal = service.y + delta; currentMax = service.maxY; break;
      case 'z': nextVal = service.z + delta; currentMax = service.maxZ; break;
    }

    if (nextVal < 0 || nextVal > currentMax) {
      _showLimitWarning(axis, nextVal < 0 ? "Minimum" : "Maximum", currentMax);
      service.addLog("SYS: Boundary limit reached on $axis");
      HapticFeedback.heavyImpact(); 
      return; 
    }
    
    setState(() => _isBusy = true);

    final gcode = "G1 ${axis.toUpperCase()}${nextVal.toStringAsFixed(1)} F${_feedrate.toInt()}";
    service.updatePos(axis, nextVal, gcode);
    HapticFeedback.lightImpact();
  }

  void _handleHome({String? axis}) {
    if (_isBusy) return; 
    
    setState(() => _isBusy = true);

    final service = ESP32Service.instance;
    if (axis == null) {
      service.updatePos('x', 0, ''); 
      service.updatePos('y', 0, ''); 
      service.updatePos('z', 0, '\$H'); 
    } else {
      service.updatePos(axis, 0, "\$H${axis.toUpperCase()}");
    }
    HapticFeedback.mediumImpact();
  }

  void _showLimitWarning(String axis, String limitType, double maxLimit) {
    final isDark = Theme.of(context).brightness == Brightness.dark;
    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        backgroundColor: isDark ? const Color(0xFF1F2937) : Colors.white,
        shape: RoundedRectangleBorder(side: const BorderSide(color: Color(0xFFF59E0B), width: 2), borderRadius: BorderRadius.circular(16)),
        title: Row(
          children: [
            const Icon(Icons.warning_amber_rounded, color: Color(0xFFF59E0B), size: 28),
            const SizedBox(width: 12),
            Text('${axis.toUpperCase()}-Axis Limit', style: TextStyle(color: isDark ? Colors.white : Colors.black, fontSize: 18, fontWeight: FontWeight.bold)),
          ],
        ),
        content: Text('Movement blocked. You have reached the $limitType physical limit for the ${axis.toUpperCase()} axis (0mm - ${maxLimit.toStringAsFixed(1)}mm).', style: TextStyle(color: isDark ? const Color(0xFFD1D5DB) : Colors.grey.shade700, height: 1.5)),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            style: TextButton.styleFrom(backgroundColor: isDark ? const Color(0xFF374151) : Colors.grey.shade200, foregroundColor: isDark ? Colors.white : Colors.black, shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8))),
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }

  void _handleIrrigateToggle() {
    setState(() { _isIrrigating = !_isIrrigating; ESP32Service.instance.sendCommand(_isIrrigating ? "M8" : "M9"); });
    HapticFeedback.lightImpact();
  }

  void _handleFertilize() {
    setState(() => _isFertilizing = true);
    final String gcode = "M7 ml${_fertilizeAmount.toInt()}";
    ESP32Service.instance.sendCommand(gcode); 
    _fertilizeTimer = Timer(const Duration(seconds: 2), () {
      if (!mounted) return;
      setState(() => _isFertilizing = false);
    });
  }

  void _handleStopFertilize() {
    _fertilizeTimer?.cancel();
    setState(() => _isFertilizing = false);
    ESP32Service.instance.sendCommand("M9"); 
  }

  void _handleWeederToggle() {
    if (_isWeederOn) {
      setState(() => _isWeederOn = false); ESP32Service.instance.sendCommand("M5"); HapticFeedback.lightImpact();
    } else {
      _showWeederWarning();
    }
  }

  void _showWeederWarning() {
    final isDark = Theme.of(context).brightness == Brightness.dark;
    showDialog(
      context: context, barrierDismissible: false,
      builder: (_) => AlertDialog(
        backgroundColor: isDark ? const Color(0xFF1F2937) : Colors.white,
        shape: RoundedRectangleBorder(side: const BorderSide(color: Color(0xFFEF4444), width: 2), borderRadius: BorderRadius.circular(16)),
        title: Row(
          children: [
            Container(width: 48, height: 48, decoration: BoxDecoration(color: const Color(0xFFEF4444).withValues(alpha: 0.2), shape: BoxShape.circle), child: const Icon(Icons.warning_amber_rounded, color: Color(0xFFEF4444), size: 24)),
            const SizedBox(width: 12),
            Expanded(child: Text('Activate Weeder?', style: TextStyle(color: isDark ? Colors.white : Colors.black, fontSize: 18, fontWeight: FontWeight.w900))),
          ],
        ),
        content: Text('You are about to turn ON the cutting mechanism.\nMake sure the area is clear and safe to proceed.', style: TextStyle(color: isDark ? const Color(0xFFD1D5DB) : Colors.grey.shade700, height: 1.6)),
        actions: [
          TextButton(onPressed: () => Navigator.pop(context), style: TextButton.styleFrom(backgroundColor: isDark ? const Color(0xFF374151) : Colors.grey.shade200, foregroundColor: isDark ? Colors.white : Colors.black, padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12), shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8))), child: const Text('Cancel', style: TextStyle(fontWeight: FontWeight.bold))),
          TextButton(onPressed: () { Navigator.pop(context); setState(() => _isWeederOn = true); ESP32Service.instance.sendCommand("M3"); }, style: TextButton.styleFrom(backgroundColor: const Color(0xFFEF4444), foregroundColor: Colors.white, padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12), shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8))), child: const Text('Activate Weeder', style: TextStyle(fontWeight: FontWeight.bold))),
        ],
      ),
    );
  }

  Future<void> _uploadGCode() async {
    try {
      FilePickerResult? result = await FilePicker.platform.pickFiles(
        type: FileType.custom,
        allowedExtensions: ['gcode', 'txt', 'nc'],
      );

      if (result != null && result.files.single.path != null) {
        final file = File(result.files.single.path!);
        final lines = await file.readAsLines();
        ESP32Service.instance.addLog("SYS: Uploaded ${result.files.single.name} (${lines.length} lines)");
        await ESP32Service.instance.executeGCode(lines);
      }
    } catch (e) {
      ESP32Service.instance.addLog("SYS: Error uploading G-Code: $e");
    }
  }

  @override
  Widget build(BuildContext context) {
    final themeState = ref.watch(themeProvider);
    final isDark = Theme.of(context).brightness == Brightness.dark;
    final accentColor = themeState.currentAccentColor;
    
    return ListenableBuilder(
      listenable: ESP32Service.instance,
      builder: (context, _) {
        final service = ESP32Service.instance;
        return RefreshIndicator(
          onRefresh: _startScan, color: accentColor,
          child: SingleChildScrollView(
            physics: const AlwaysScrollableScrollPhysics(), padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                _buildVisualization(service, accentColor, isDark), const SizedBox(height: 16),
                _buildWorkCoordinates(service, accentColor, isDark), const SizedBox(height: 16), // ── THE NEW WORK COORDINATES PANEL ──
                _buildCncAutomation(service, accentColor, isDark), const SizedBox(height: 16),   // ── THE NEW CNC AUTOMATION FILE PICKER ──
                _buildManualControl(service, accentColor, isDark), const SizedBox(height: 16),
                _buildGCodeTerminal(service, accentColor, isDark), const SizedBox(height: 32),
              ],
            ),
          ),
        );
      }
    );
  }

  // ── THE WORK COORDINATES WIDGET ──
  Widget _buildWorkCoordinates(ESP32Service service, Color accentColor, bool isDark) {
    final containerBg = isDark ? const Color(0xFF111111) : Colors.white;
    final containerBorder = isDark ? const Color(0xFF374151) : Colors.grey.shade300;
    final textColor = isDark ? Colors.white : Colors.black87;

    return Container(
      decoration: BoxDecoration(
        color: containerBg, 
        borderRadius: BorderRadius.circular(12), 
        border: Border.all(color: containerBorder)
      ),
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text("Work Coordinates", style: TextStyle(color: textColor, fontSize: 16, fontWeight: FontWeight.w500)),
              const SizedBox(width: 8),
              Icon(Icons.keyboard_arrow_down, color: isDark ? Colors.white70 : Colors.black54, size: 20),
            ],
          ),
          const SizedBox(height: 16),
          Divider(color: isDark ? Colors.white12 : Colors.black12, height: 1),
          const SizedBox(height: 24),

          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Column(
                children: [
                  _coordRowOriginal('X', service.x, isDark, () => _handleHome(axis: 'x')),
                  const SizedBox(height: 16),
                  _coordRowOriginal('Y', service.y, isDark, () => _handleHome(axis: 'y')),
                  const SizedBox(height: 16),
                  _coordRowOriginal('Z', service.z, isDark, () => _handleHome(axis: 'z')),
                ],
              ),
              Container(
                width: 75, height: 75,
                decoration: BoxDecoration(
                  color: isDark ? const Color(0xFF222222) : Colors.grey.shade900,
                  shape: BoxShape.circle,
                ),
                child: IconButton(
                  icon: const Icon(Icons.share_location, color: Colors.white, size: 32),
                  onPressed: () => _handleHome(), 
                ),
              ),
            ],
          ),
          const SizedBox(height: 32),
          GestureDetector(
            onTap: () {
              ESP32Service.instance.addLog("SYS: Running Boundary Routine...");
            },
            child: Text(
              "Run Boundary",
              style: TextStyle(
                color: Colors.blueAccent.shade400,
                fontSize: 16,
                fontWeight: FontWeight.w500,
                decoration: TextDecoration.underline,
                decorationColor: Colors.blueAccent.shade400,
              ),
            ),
          )
        ],
      ),
    );
  }

  Widget _coordRowOriginal(String axis, double value, bool isDark, VoidCallback onTargetPressed) {
    return Row(
      children: [
        SizedBox(width: 30, child: Text(axis, style: const TextStyle(color: Colors.grey, fontSize: 22, fontWeight: FontWeight.bold))),
        SizedBox(
          width: 100,
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.baseline,
            textBaseline: TextBaseline.alphabetic,
            children: [
              Text(value.toStringAsFixed(2), style: TextStyle(color: isDark ? Colors.white : Colors.black, fontSize: 22, fontWeight: FontWeight.bold)),
              const SizedBox(width: 4),
              const Text("mm", style: TextStyle(color: Colors.grey, fontSize: 14)),
            ],
          ),
        ),
        const SizedBox(width: 16),
        GestureDetector(
          onTap: onTargetPressed,
          child: Container(
            width: 36, height: 36,
            decoration: BoxDecoration(color: isDark ? const Color(0xFF222222) : Colors.grey.shade900, shape: BoxShape.circle),
            child: const Icon(Icons.my_location, color: Colors.white, size: 18),
          ),
        )
      ],
    );
  }

  // ── CNC AUTOMATION WIDGET ──
  Widget _buildCncAutomation(ESP32Service service, Color accentColor, bool isDark) {
    final containerBg = isDark ? const Color(0xFF1F2937) : Colors.white;
    final containerBorder = isDark ? const Color(0xFF374151) : Colors.grey.shade300;
    final textColor = isDark ? Colors.white : Colors.black87;

    return Container(
      decoration: BoxDecoration(color: containerBg, borderRadius: BorderRadius.circular(12), border: Border.all(color: containerBorder)), padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          RichText(text: TextSpan(style: TextStyle(fontSize: 18, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: textColor), children: [const TextSpan(text: 'CNC '), TextSpan(text: 'Automation', style: TextStyle(color: accentColor))])), 
          const SizedBox(height: 16),
          
          ValueListenableBuilder<double>(
            valueListenable: service.jobProgress,
            builder: (context, progress, child) {
              if (progress <= 0.0 || progress >= 1.0) {
                return SizedBox(
                  width: double.infinity, height: 50,
                  child: ElevatedButton.icon(
                    onPressed: _pickAndRunGcode,
                    icon: const Icon(Icons.upload_file),
                    label: const Text("UPLOAD G-CODE FILE", style: TextStyle(fontWeight: FontWeight.bold, letterSpacing: 1)),
                    style: ElevatedButton.styleFrom(backgroundColor: Colors.blueAccent.shade700, foregroundColor: Colors.white, shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10))),
                  ),
                );
              }

              return Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      const Text("Running Job...", style: TextStyle(color: Colors.blueAccent, fontWeight: FontWeight.bold)),
                      Text("${(progress * 100).toInt()}%", style: TextStyle(color: textColor, fontWeight: FontWeight.bold)),
                    ],
                  ),
                  const SizedBox(height: 8),
                  ClipRRect(borderRadius: BorderRadius.circular(8), child: LinearProgressIndicator(value: progress, minHeight: 12, backgroundColor: isDark ? Colors.grey.shade800 : Colors.grey.shade300, color: accentColor)),
                  const SizedBox(height: 16),
                  SizedBox(
                    width: double.infinity, height: 50,
                    child: ElevatedButton.icon(
                      onPressed: () => service.cancelGcodeJob(),
                      icon: const Icon(Icons.warning_amber_rounded),
                      label: const Text("EMERGENCY STOP", style: TextStyle(fontWeight: FontWeight.bold, letterSpacing: 1)),
                      style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFFEF4444), foregroundColor: Colors.white, shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10))),
                    ),
                  ),
                ],
              );
            },
          ),
        ],
      ),
    );
  }

  Widget _buildGCodeTerminal(ESP32Service service, Color accentColor, bool isDark) {
    final containerBg = isDark ? const Color(0xFF1F2937) : Colors.white;
    final containerBorder = isDark ? const Color(0xFF374151) : Colors.grey.shade300;
    
    final bool hasLogs = service.logs.isNotEmpty;
    final Color enabledIconColor = isDark ? Colors.white70 : Colors.grey.shade800;
    final Color disabledIconColor = isDark ? Colors.white12 : Colors.grey.shade200;

    return Container(
      decoration: BoxDecoration(color: containerBg, borderRadius: BorderRadius.circular(12), border: Border.all(color: containerBorder)),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 16, 16, 4),
            child: Row(
              children: [
                const Icon(Icons.terminal, color: Color(0xFFF59E0B), size: 20), const SizedBox(width: 10),
                Text("GCODE TERMINAL", style: TextStyle(color: isDark ? Colors.white : Colors.black87, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, fontSize: 14)), 
                const Spacer(),
                Icon(Icons.circle, size: 10, color: service.isConnected ? const Color(0xFF10B981) : const Color(0xFFEF4444)), const SizedBox(width: 6),
                Text(service.isConnected ? "Online" : "Offline", style: TextStyle(fontSize: 12, fontWeight: FontWeight.bold, color: service.isConnected ? const Color(0xFF10B981) : const Color(0xFFEF4444))),
              ],
            ),
          ),
          
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.end,
              children: [
                IconButton(
                  padding: EdgeInsets.zero, constraints: const BoxConstraints(minWidth: 40, minHeight: 36), 
                  icon: Icon(Icons.content_copy, size: 18, color: hasLogs ? enabledIconColor : disabledIconColor), 
                  onPressed: !hasLogs ? null : () { 
                    final linesToCopy = service.logs.length > 100 ? service.logs.sublist(service.logs.length - 100) : service.logs;
                    Clipboard.setData(ClipboardData(text: linesToCopy.join('\n'))); 
                    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Last ${linesToCopy.length} logs copied!'), backgroundColor: accentColor, duration: const Duration(seconds: 2))); 
                    HapticFeedback.lightImpact(); 
                  }, 
                  tooltip: "Copy Last 100 Logs"
                ), 
                const SizedBox(width: 8),
                IconButton(
                  padding: EdgeInsets.zero, constraints: const BoxConstraints(minWidth: 40, minHeight: 36), 
                  icon: Icon(Icons.copy_all, size: 22, color: hasLogs ? enabledIconColor : disabledIconColor), 
                  onPressed: !hasLogs ? null : () { 
                    Clipboard.setData(ClipboardData(text: service.logs.join('\n'))); 
                    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('All ${service.logs.length} logs copied!'), backgroundColor: accentColor, duration: const Duration(seconds: 2))); 
                    HapticFeedback.lightImpact(); 
                  }, 
                  tooltip: "Copy All Logs"
                ), 
                const SizedBox(width: 8),
                IconButton(
                  padding: EdgeInsets.zero, constraints: const BoxConstraints(minWidth: 40, minHeight: 36), 
                  icon: Icon(Icons.delete_outline, size: 20, color: hasLogs ? enabledIconColor : disabledIconColor), 
                  onPressed: !hasLogs ? null : () { 
                    service.clearLogs(); 
                    HapticFeedback.lightImpact(); 
                  }, 
                  tooltip: "Clear Terminal"
                ), 
                const SizedBox(width: 8),
                _isScanning 
                  ? SizedBox(width: 40, height: 36, child: Center(child: SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2, color: accentColor)))) 
                  : IconButton(
                      padding: EdgeInsets.zero, constraints: const BoxConstraints(minWidth: 40, minHeight: 36), 
                      icon: Icon(Icons.refresh, size: 20, color: enabledIconColor), 
                      onPressed: _startScan, 
                      tooltip: "Scan for ESP32"
                    ),
                const SizedBox(width: 8),
                ElevatedButton.icon(
                  onPressed: service.isConnected ? _uploadGCode : null,
                  icon: const Icon(Icons.upload_file, size: 16),
                  label: const Text('Upload'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: accentColor,
                    foregroundColor: Colors.white,
                    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                    textStyle: const TextStyle(fontSize: 12, fontWeight: FontWeight.bold),
                  ),
                ),
              ],
            ),
          ),

          // ── Nano Disconnected Warning Banner ──
          if (service.isConnected && !service.nanoConnected)
            Container(
              margin: const EdgeInsets.fromLTRB(16, 0, 16, 8),
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              decoration: BoxDecoration(
                color: const Color(0xFFF59E0B).withValues(alpha: 0.12),
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: const Color(0xFFF59E0B)),
              ),
              child: Row(
                children: [
                  const Icon(Icons.cable_rounded, color: Color(0xFFF59E0B), size: 16),
                  const SizedBox(width: 8),
                  const Expanded(
                    child: Text(
                      'No Nano connected — GRBL not responding on Serial1. Check TX/RX wiring (pins 43/44).',
                      style: TextStyle(color: Color(0xFFF59E0B), fontSize: 11, fontWeight: FontWeight.bold),
                    ),
                  ),
                ],
              ),
            ),

          Container(
            height: 200, margin: const EdgeInsets.symmetric(horizontal: 16), padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(color: isDark ? const Color(0xFF030712) : const Color(0xFFF8FAFC), borderRadius: BorderRadius.circular(8), border: Border.all(color: containerBorder)),
            child: ListView.builder(controller: _terminalScrollController, itemCount: service.logs.length, itemBuilder: (context, i) => _buildLogLine(service.logs[i], isDark)),
          ),
          
          Padding(
            padding: const EdgeInsets.all(16),
            child: RawKeyboardListener(
              focusNode: _terminalFocusNode,
              onKey: _handleHistoryKey,
              child: TextField(
                controller: _terminalController, style: TextStyle(color: isDark ? Colors.white : Colors.black, fontSize: 13, fontFamily: 'monospace'),
                decoration: InputDecoration(hintText: "Type GCode...", hintStyle: TextStyle(color: isDark ? Colors.white30 : Colors.black38), prefixText: "\$ ", filled: true, fillColor: isDark ? const Color(0xFF374151) : Colors.grey.shade100, contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10), border: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: containerBorder)), enabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: containerBorder)), focusedBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: accentColor))),
                onSubmitted: (val) { 
                  if (val.trim().isNotEmpty) {
                    service.sendCommand(val); 
                    _commandHistory.add(val);
                    _historyIndex = -1;
                    _terminalController.clear(); 
                  }
                },
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildLogLine(String log, bool isDark) {
    final isTx = log.startsWith("TX:"); final isSys = log.startsWith("SYS"); final display = log.length > 3 ? log.substring(3) : log;
    return Text("${DateFormat('HH:mm:ss').format(DateTime.now())} ${isTx ? '→' : isSys ? '•' : '←'} $display", style: TextStyle(color: isSys ? (isDark ? Colors.white54 : Colors.grey.shade600) : isTx ? const Color(0xFFF59E0B) : const Color(0xFF10B981), fontSize: 12, fontFamily: 'monospace'));
  }

  Widget _buildVisualization(ESP32Service service, Color accentColor, bool isDark) {
    final containerBg = isDark ? const Color(0xFF1F2937) : Colors.white;
    final containerBorder = isDark ? const Color(0xFF374151) : Colors.grey.shade300;
    final innerBg = isDark ? const Color(0xFF030712) : const Color(0xFFF8FAFC);
    final textColor = isDark ? Colors.white : Colors.black87;
    
    return Container(
      decoration: BoxDecoration(color: containerBg, borderRadius: BorderRadius.circular(12), border: Border.all(color: containerBorder)), padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          RichText(text: TextSpan(style: TextStyle(fontSize: 18, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: textColor), children: [const TextSpan(text: 'Gantry '), TextSpan(text: 'Visualization', style: TextStyle(color: accentColor))])), const SizedBox(height: 16),
          SizedBox(
            height: 280,
            child: Container(
              decoration: BoxDecoration(color: innerBg, borderRadius: BorderRadius.circular(8), border: Border.all(color: containerBorder)),
              child: Stack(
                children: [
                  Positioned(left: 12, top: 12, child: Container(padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8), decoration: BoxDecoration(color: isDark ? Colors.black.withValues(alpha: 0.4) : Colors.white.withValues(alpha: 0.8), borderRadius: BorderRadius.circular(8)), child: Text('COORDINATES (LIVE)', style: TextStyle(fontSize: 8, color: isDark ? const Color(0xFF9CA3AF) : Colors.grey.shade600, fontFamily: 'monospace', letterSpacing: 2)))),
                  Positioned(left: 12, bottom: 12, child: Container(padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8), decoration: BoxDecoration(color: isDark ? Colors.black.withValues(alpha: 0.4) : Colors.white.withValues(alpha: 0.8), borderRadius: BorderRadius.circular(8)), child: Row(mainAxisSize: MainAxisSize.min, children: [_coordLine('x', service.x, accentColor, textColor), const SizedBox(width: 14), _coordLine('y', service.y, accentColor, textColor), const SizedBox(width: 14), _coordLine('z', service.z, accentColor, textColor)]))),
                  Center(
                    child: SizedBox(
                      width: 200, height: 200,
                      child: DecoratedBox(
                        decoration: BoxDecoration(border: Border.all(color: containerBorder, width: 2), borderRadius: BorderRadius.circular(8)),
                        child: Stack(
                          children: [
                            Positioned.fill(child: CustomPaint(painter: _DashedBorderPainter(color: containerBorder))),
                            AnimatedPositioned(
                              left: (service.x / (service.maxX > 0 ? service.maxX : 1000)) * 182,
                              bottom: (service.y / (service.maxY > 0 ? service.maxY : 1000)) * 182,
                              duration: const Duration(milliseconds: 250), curve: Curves.linear, 
                              child: Transform.scale(
                                scale: 1.0 + service.z / 500, 
                                child: Container(
                                  width: 18, height: 18,
                                  decoration: BoxDecoration(
                                    color: accentColor, borderRadius: BorderRadius.circular(4),
                                    boxShadow: [BoxShadow(color: accentColor.withValues(alpha: 0.6), blurRadius: 12, spreadRadius: 2)],
                                  ),
                                ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _coordLine(String axis, double val, Color accentColor, Color textColor) {
    return RichText(text: TextSpan(style: TextStyle(fontFamily: 'monospace', fontSize: 13, color: textColor), children: [TextSpan(text: '$axis: ', style: TextStyle(color: accentColor)), TextSpan(text: val.toStringAsFixed(1))]));
  }

  Widget _buildManualControl(ESP32Service service, Color accentColor, bool isDark) {
    final isWide = MediaQuery.of(context).size.width >= 700;
    final containerBg = isDark ? const Color(0xFF1F2937) : Colors.white;
    final containerBorder = isDark ? const Color(0xFF374151) : Colors.grey.shade300;
    final textColor = isDark ? Colors.white : Colors.black87;

    final isOffline = !service.isConnected;
    final shouldDisable = _isBusy || isOffline;
    final effectiveAccent = shouldDisable ? Colors.grey.shade600 : accentColor;

    return Container(
      decoration: BoxDecoration(color: containerBg, borderRadius: BorderRadius.circular(12), border: Border.all(color: containerBorder)), padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          RichText(text: TextSpan(style: TextStyle(fontSize: 18, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: textColor), children: [const TextSpan(text: 'Manual '), TextSpan(text: 'Control', style: TextStyle(color: accentColor))])), 
          
          if (isOffline)
            Container(
              margin: const EdgeInsets.only(top: 16), padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(color: Colors.grey.withValues(alpha: 0.15), borderRadius: BorderRadius.circular(8), border: Border.all(color: Colors.grey)),
              child: Row(children: [const Icon(Icons.wifi_off_rounded, color: Colors.grey), const SizedBox(width: 12), Expanded(child: Text('SYSTEM OFFLINE. Please connect to the ESP32 to enable manual controls.', style: TextStyle(color: isDark ? Colors.grey.shade400 : Colors.grey.shade700, fontSize: 12, fontWeight: FontWeight.bold)))]),
            )
          else if (service.machineState == 'Alarm')
            Container(
              margin: const EdgeInsets.only(top: 16), padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(color: const Color(0xFFEF4444).withValues(alpha: 0.15), borderRadius: BorderRadius.circular(8), border: Border.all(color: const Color(0xFFEF4444))),
              child: Row(children: [const Icon(Icons.warning_amber_rounded, color: Color(0xFFEF4444)), const SizedBox(width: 12), Expanded(child: Text('MACHINE NOT HOMED. Motors are locked for safety.\nPlease press "HOME ALL" to unlock the gantry.', style: TextStyle(color: isDark ? const Color(0xFFFCA5A5) : const Color(0xFFB91C1C), fontSize: 12, fontWeight: FontWeight.bold)))]),
            ),

          const SizedBox(height: 20),
          
          AnimatedOpacity(
            opacity: shouldDisable ? 0.25 : 1.0,
            duration: const Duration(milliseconds: 200),
            child: IgnorePointer(
              ignoring: shouldDisable,
              child: Column(
                children: [
                  _buildHomingRow(effectiveAccent, isDark), const SizedBox(height: 24),
                  if (isWide) Row(crossAxisAlignment: CrossAxisAlignment.start, children: [Expanded(child: _buildDPad(effectiveAccent, isDark)), const SizedBox(width: 32), Expanded(child: _buildRightColumn(effectiveAccent, isDark, shouldDisable))])
                  else Column(children: [_buildDPad(effectiveAccent, isDark), const SizedBox(height: 24), _buildRightColumn(effectiveAccent, isDark, shouldDisable)]),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildHomingRow(Color accentColor, bool isDark) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _label('HOMING CYCLES', isDark), const SizedBox(height: 8),
        Row(children: [_homingBtn('HOME X', () => _handleHome(axis: 'x'), accentColor, isDark), const SizedBox(width: 8), _homingBtn('HOME Y', () => _handleHome(axis: 'y'), accentColor, isDark), const SizedBox(width: 8), _homingBtn('HOME Z', () => _handleHome(axis: 'z'), accentColor, isDark), const SizedBox(width: 8), _homingBtn('HOME ALL', () => _handleHome(), accentColor, isDark, isPrimary: true)]),
      ],
    );
  }

  Widget _homingBtn(String text, VoidCallback onTap, Color accentColor, bool isDark, {bool isPrimary = false}) {
    final bg = isPrimary ? accentColor.withOpacity(0.15) : (isDark ? const Color(0xFF374151) : Colors.grey.shade100);
    final border = isPrimary ? accentColor : (isDark ? const Color(0xFF4B5563) : Colors.grey.shade300);
    final textColor = isPrimary ? accentColor : (isDark ? Colors.white : Colors.black87);
    return Expanded(child: GestureDetector(onTap: onTap, child: Container(padding: const EdgeInsets.symmetric(vertical: 12), decoration: BoxDecoration(color: bg, borderRadius: BorderRadius.circular(8), border: Border.all(color: border)), child: Center(child: Text(text, style: TextStyle(color: textColor, fontSize: 10, fontWeight: FontWeight.bold, letterSpacing: 1))))));
  }

  Widget _buildDPad(Color accentColor, bool isDark) {
    return Column(
      children: [
        Text('AXIS CONTROL (X/Y)', style: TextStyle(fontSize: 10, color: isDark ? const Color(0xFF9CA3AF) : Colors.grey.shade600, letterSpacing: 2)), const SizedBox(height: 16),
        SizedBox(
          width: 200, 
          child: Column(
            children: [
              Row(mainAxisAlignment: MainAxisAlignment.center, children: [const SizedBox(width: 60), _ControlBtn(icon: Icons.keyboard_arrow_up, onTap: () => _move('y', -1), accentColor: accentColor, isDark: isDark), const SizedBox(width: 60)]), const SizedBox(height: 8),
              Row(mainAxisAlignment: MainAxisAlignment.center, children: [_ControlBtn(icon: Icons.keyboard_arrow_left, onTap: () => _move('x', -1), accentColor: accentColor, isDark: isDark), const SizedBox(width: 8), GestureDetector(onTap: _handleHome, child: Container(width: 56, height: 56, decoration: BoxDecoration(color: isDark ? const Color(0xFF374151) : Colors.grey.shade100, borderRadius: BorderRadius.circular(12), border: Border.all(color: accentColor)), child: Icon(Icons.my_location, color: accentColor, size: 24))), const SizedBox(width: 8), _ControlBtn(icon: Icons.keyboard_arrow_right, onTap: () => _move('x', 1), accentColor: accentColor, isDark: isDark)]), const SizedBox(height: 8),
              Row(mainAxisAlignment: MainAxisAlignment.center, children: [const SizedBox(width: 60), _ControlBtn(icon: Icons.keyboard_arrow_down, onTap: () => _move('y', 1), accentColor: accentColor, isDark: isDark), const SizedBox(width: 60)]),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildRightColumn(Color accentColor, bool isDark, bool shouldDisable) {
    final idleBtnColor = isDark ? const Color(0xFF374151) : Colors.grey.shade100;
    final idleBorderColor = isDark ? const Color(0xFF4B5563) : Colors.grey.shade300;
    final textColor = isDark ? Colors.white : Colors.black87;

    final irrigateBg = shouldDisable ? Colors.grey.shade600 : const Color(0xFF2563EB);
    final irrigateBorder = shouldDisable ? Colors.grey.shade600 : const Color(0xFF60A5FA);
    final fertilizeColor = shouldDisable ? Colors.grey.shade600 : const Color(0xFF16A34A);
    final weederBg = shouldDisable ? Colors.grey.shade600 : const Color(0xFFDC2626);
    final weederBorder = shouldDisable ? Colors.grey.shade600 : const Color(0xFFEF4444);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(mainAxisAlignment: MainAxisAlignment.center, children: [_ControlBtn(icon: Icons.keyboard_arrow_up, onTap: () => _move('z', 1), accentColor: accentColor, isDark: isDark), const SizedBox(width: 12), _ControlBtn(icon: Icons.keyboard_arrow_down, onTap: () => _move('z', -1), accentColor: accentColor, isDark: isDark)]), const SizedBox(height: 20),
        _label('DISTANCE', isDark), const SizedBox(height: 8),
        Row(children: [0.1, 1.0, 10.0, 100.0].map((v) { final selected = _speedMode == v; return Expanded(child: Padding(padding: const EdgeInsets.only(right: 6), child: GestureDetector(onTap: () => setState(() => _speedMode = v), child: Container(padding: const EdgeInsets.symmetric(vertical: 8), decoration: BoxDecoration(color: selected ? accentColor : idleBtnColor, borderRadius: BorderRadius.circular(8), border: Border.all(color: selected ? accentColor : idleBorderColor)), child: Text('${v % 1 == 0 ? v.toInt() : v}mm', textAlign: TextAlign.center, style: TextStyle(fontSize: 11, fontWeight: FontWeight.bold, color: selected ? Colors.white : (isDark ? const Color(0xFF9CA3AF) : Colors.grey.shade700))))))); }).toList()), const SizedBox(height: 16),
        _label('FEEDRATE (mm/min)', isDark), const SizedBox(height: 6),
        _numberField(value: _feedrate, onChanged: (v) => setState(() => _feedrate = v), accentColor: accentColor, isDark: isDark, textColor: textColor), const SizedBox(height: 20),
        _label('ABSOLUTE MOVEMENT', isDark), const SizedBox(height: 6),
        Row(children: [
          Expanded(child: _coordField(label: 'X', value: _targetX, onChanged: (v) => _targetX = v, accentColor: accentColor, isDark: isDark, textColor: textColor)), const SizedBox(width: 8),
          Expanded(child: _coordField(label: 'Y', value: _targetY, onChanged: (v) => _targetY = v, accentColor: accentColor, isDark: isDark, textColor: textColor)), const SizedBox(width: 8),
          Expanded(child: _coordField(label: 'Z', value: _targetZ, onChanged: (v) => _targetZ = v, accentColor: accentColor, isDark: isDark, textColor: textColor)), const SizedBox(width: 8),
          ElevatedButton(onPressed: () { final gcode = "G0 X${_targetX.toStringAsFixed(1)} Y${_targetY.toStringAsFixed(1)} Z${_targetZ.toStringAsFixed(1)} F${_feedrate.toInt()}"; ESP32Service.instance.sendCommand(gcode); HapticFeedback.lightImpact(); }, style: ElevatedButton.styleFrom(backgroundColor: accentColor, foregroundColor: Colors.white, shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)), padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 14)), child: const Text('Move', style: TextStyle(fontWeight: FontWeight.bold))),
        ]), const SizedBox(height: 20),
        GestureDetector(onTap: _handleIrrigateToggle, child: Container(padding: const EdgeInsets.symmetric(vertical: 14), decoration: BoxDecoration(color: irrigateBg, borderRadius: BorderRadius.circular(10), border: Border.all(color: irrigateBorder)), child: Row(mainAxisAlignment: MainAxisAlignment.center, children: [const Icon(Icons.water_drop_outlined, color: Colors.white, size: 16), const SizedBox(width: 8), Text('Irrigate: ${_isIrrigating ? "ON" : "OFF"}', style: const TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold))]))), const SizedBox(height: 10),
        _ActionRow(amount: _fertilizeAmount, onAmountChanged: (v) => setState(() => _fertilizeAmount = v), amountEnabled: !_isFertilizing, onAction: _handleFertilize, onStop: _handleStopFertilize, isActive: _isFertilizing, activeLabel: 'Fertilizing...', idleLabel: 'Fertilize', hintText: 'mL', activeColor: fertilizeColor, textColor: textColor, icon: Icons.eco_outlined, accentColor: accentColor, isDark: isDark), const SizedBox(height: 10),
        GestureDetector(onTap: _handleWeederToggle, child: Container(padding: const EdgeInsets.symmetric(vertical: 14), decoration: BoxDecoration(color: weederBg, borderRadius: BorderRadius.circular(10), border: Border.all(color: weederBorder)), child: Row(mainAxisAlignment: MainAxisAlignment.center, children: [const Icon(Icons.content_cut, color: Colors.white, size: 16), const SizedBox(width: 8), Text('Weeder: ${_isWeederOn ? "ON" : "OFF"}', style: const TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold))]))),
      ],
    );
  }

  Widget _label(String text, bool isDark) => Text(text, style: TextStyle(fontSize: 10, color: isDark ? const Color(0xFF9CA3AF) : Colors.grey.shade600, letterSpacing: 2));
  
  Widget _numberField({required double value, required ValueChanged<double> onChanged, required Color accentColor, required bool isDark, required Color textColor}) {
    final fieldBg = isDark ? const Color(0xFF374151) : Colors.grey.shade100;
    final fieldBorder = isDark ? const Color(0xFF4B5563) : Colors.grey.shade300;
    return TextFormField(initialValue: value.toString(), keyboardType: TextInputType.number, style: TextStyle(color: textColor, fontSize: 14), decoration: InputDecoration(filled: true, fillColor: fieldBg, contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10), border: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBorder)), enabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBorder)), focusedBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: accentColor))), onChanged: (s) { final parsed = double.tryParse(s); if (parsed != null) onChanged(parsed); });
  }
  Widget _coordField({required String label, required double value, required ValueChanged<double> onChanged, required Color accentColor, required bool isDark, required Color textColor}) {
    final fieldBg = isDark ? const Color(0xFF374151) : Colors.grey.shade100;
    final fieldBorder = isDark ? const Color(0xFF4B5563) : Colors.grey.shade300;
    return TextFormField(initialValue: value.toString(), keyboardType: TextInputType.number, style: TextStyle(color: textColor, fontSize: 14), decoration: InputDecoration(labelText: label, labelStyle: TextStyle(color: accentColor, fontWeight: FontWeight.bold), filled: true, fillColor: fieldBg, contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8), border: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBorder)), enabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBorder)), focusedBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: accentColor))), onChanged: (s) { final parsed = double.tryParse(s); if (parsed != null) onChanged(parsed); });
  }
}

class _ControlBtn extends StatelessWidget {
  const _ControlBtn({required this.icon, required this.onTap, required this.accentColor, required this.isDark});
  final IconData icon; final VoidCallback onTap; final Color accentColor; final bool isDark;
  @override Widget build(BuildContext context) {
    return GestureDetector(onTap: onTap, child: AnimatedContainer(duration: const Duration(milliseconds: 150), width: 56, height: 56, decoration: BoxDecoration(color: isDark ? const Color(0xFF374151) : Colors.grey.shade100, borderRadius: BorderRadius.circular(12), border: Border.all(color: accentColor, width: 1.5)), child: Icon(icon, size: 26, color: accentColor)));
  }
}

class _ActionRow extends StatelessWidget {
  const _ActionRow({required this.amount, required this.onAmountChanged, required this.amountEnabled, required this.onAction, required this.onStop, required this.isActive, required this.activeLabel, required this.idleLabel, required this.hintText, required this.activeColor, required this.textColor, required this.icon, required this.accentColor, required this.isDark});
  final double amount; final ValueChanged<double> onAmountChanged; final bool amountEnabled; final VoidCallback onAction, onStop; final bool isActive; final String activeLabel, idleLabel, hintText; final Color activeColor, textColor; final IconData icon; final Color accentColor; final bool isDark;
  
  @override 
  Widget build(BuildContext context) {
    final fieldBg = isDark ? const Color(0xFF374151) : Colors.grey.shade100;
    final fieldBorder = isDark ? const Color(0xFF4B5563) : Colors.grey.shade300;
    final btnBg = isActive ? (isDark ? const Color(0xFF374151) : Colors.grey.shade300) : activeColor;
    final btnBorderColor = isActive ? Colors.transparent : activeColor.withValues(alpha: 0.6);
    final btnTextColor = isActive ? (isDark ? Colors.white30 : Colors.grey.shade500) : Colors.white;

    return Row(
      children: [
        SizedBox(width: 72, child: TextFormField(initialValue: amount.toInt().toString(), keyboardType: TextInputType.number, enabled: amountEnabled, style: TextStyle(color: textColor, fontSize: 13), decoration: InputDecoration(hintText: hintText, hintStyle: TextStyle(color: isDark ? const Color(0xFF6B7280) : Colors.grey, fontSize: 13), filled: true, fillColor: fieldBg, contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 10), border: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBorder)), enabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBorder)), disabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBg)), focusedBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: accentColor))), onChanged: (s) { final v = double.tryParse(s); if (v != null) onAmountChanged(v); })), const SizedBox(width: 8),
        Expanded(child: GestureDetector(onTap: isActive ? null : onAction, child: AnimatedContainer(duration: const Duration(milliseconds: 200), padding: const EdgeInsets.symmetric(vertical: 12), decoration: BoxDecoration(color: btnBg, borderRadius: BorderRadius.circular(10), border: Border.all(color: btnBorderColor)), child: Row(mainAxisAlignment: MainAxisAlignment.center, children: [Icon(icon, color: btnTextColor, size: 16), const SizedBox(width: 6), Text(isActive ? activeLabel : idleLabel, style: TextStyle(color: btnTextColor, fontSize: 12, fontWeight: FontWeight.bold))])))),
        if (isActive) ...[const SizedBox(width: 8), GestureDetector(onTap: onStop, child: Container(padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12), decoration: BoxDecoration(color: const Color(0xFFDC2626), borderRadius: BorderRadius.circular(10), border: Border.all(color: const Color(0xFFEF4444))), child: const Text('Stop', style: TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold))))],
      ],
    );
  }
}

class _DashedBorderPainter extends CustomPainter {
  final Color color; _DashedBorderPainter({required this.color});
  @override void paint(Canvas canvas, Size size) {
    final paint = Paint()..color = color..strokeWidth = 1.5..style = PaintingStyle.stroke;
    final rrect = RRect.fromRectAndRadius(Rect.fromLTWH(0, 0, size.width, size.height), const Radius.circular(8));
    final path = Path()..addRRect(rrect);
    for (final m in path.computeMetrics()) {
      double dist = 0; while (dist < m.length) { canvas.drawPath(m.extractPath(dist, dist + 6.0), paint); dist += 10.0; }
    }
  }
  @override bool shouldRepaint(covariant CustomPainter old) => true;
}