// lib/screens/control_panel.dart

import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart'; // Added for haptics
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:intl/intl.dart'; // Added for terminal timestamps

import '../providers/theme_provider.dart'; 
import '../service/ESP32/esp32_service.dart'; // Added to connect to ESP32

// ─────────────────────────────────────────────────────────────
// Data model
// ─────────────────────────────────────────────────────────────

class GantryPosition {
  final double x, y, z;
  const GantryPosition({this.x = 0, this.y = 0, this.z = 0});

  GantryPosition copyWith({double? x, double? y, double? z}) =>
      GantryPosition(x: x ?? this.x, y: y ?? this.y, z: z ?? this.z);
}

// ─────────────────────────────────────────────────────────────
// ControlPanel screen
// ─────────────────────────────────────────────────────────────

class ControlPanel extends ConsumerStatefulWidget {
  const ControlPanel({super.key});

  @override
  ConsumerState<ControlPanel> createState() => _ControlPanelState();
}

class _ControlPanelState extends ConsumerState<ControlPanel> {
  // ── Gantry state ─────────────────────────────────────────
  GantryPosition _position = const GantryPosition();

  // ── Motion settings ──────────────────────────────────────
  double _speedMode    = 1;   // mm per button press
  double _velocity     = 5;   // mm/s
  double _acceleration = 1;   // mm²/s

  // ── Irrigation (ON/OFF) ──────────────────────────────────
  bool _isIrrigating = false;

  // ── Fertilize (Amount based) ─────────────────────────────
  double _fertilizeAmount = 50;
  bool   _isFertilizing   = false;

  // ── Weeder ───────────────────────────────────────────────
  bool _isWeederOn = false;

  // ── Timer & Hardware handles ─────────────────────────────
  Timer? _fertilizeTimer;
  DateTime? _lastMoveTime; // Used to prevent motor command spam
  bool _isScanning = false;
  final TextEditingController _terminalController = TextEditingController();
  final ScrollController _terminalScrollController = ScrollController();

  // ── Lifecycle ────────────────────────────────────────────

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _startScan();
    });
    ESP32Service.instance.addListener(_scrollToBottom);
  }

  @override
  void dispose() {
    ESP32Service.instance.removeListener(_scrollToBottom);
    _terminalController.dispose();
    _terminalScrollController.dispose();
    _fertilizeTimer?.cancel();
    super.dispose();
  }

  // ── Hardware Communication ───────────────────────────────

  Future<void> _startScan() async {
    if (_isScanning) return;
    setState(() => _isScanning = true);
    HapticFeedback.mediumImpact();

    await ESP32Service.instance.autoDiscover();

    if (mounted) setState(() => _isScanning = false);
  }

  void _scrollToBottom() {
    // 50ms delay gives UI time to paint the new G-code before calculating scroll limit
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
    // 1. Throttle logic to prevent ESP32 Queue Spam
    final now = DateTime.now();
    if (_lastMoveTime != null && now.difference(_lastMoveTime!).inMilliseconds < 200) {
      return; 
    }
    _lastMoveTime = now;

    // 2. Calculate next position
    final delta = direction * _speedMode;
    double nextVal = 0;

    switch (axis) {
      case 'x': nextVal = _position.x + delta; break;
      case 'y': nextVal = _position.y + delta; break;
      case 'z': nextVal = _position.z + delta; break;
    }

    // 3. Prevent crashing into physical boundaries
    if (nextVal < 0 || nextVal > 1000) {
      _showLimitWarning(axis, nextVal < 0 ? "Minimum" : "Maximum");
      ESP32Service.instance.addLog("SYS: Boundary limit reached on $axis");
      HapticFeedback.heavyImpact(); 
      return; 
    }

    // 4. Update UI Visualization
    setState(() {
      switch (axis) {
        case 'x': _position = _position.copyWith(x: nextVal); break;
        case 'y': _position = _position.copyWith(y: nextVal); break;
        case 'z': _position = _position.copyWith(z: nextVal); break;
      }
    });

    // 5. Send actual G-Code to hardware
    final gcode = "G0 ${axis.toUpperCase()}${nextVal.toStringAsFixed(1)} F${(_velocity * 60).toInt()}";
    ESP32Service.instance.updatePos(axis, nextVal, gcode);
    HapticFeedback.lightImpact();
  }

  void _handleHome() {
    setState(() => _position = const GantryPosition(x: 0, y: 0, z: 0));
    
    // Update internal tracking
    ESP32Service.instance.x = 0;
    ESP32Service.instance.y = 0;
    ESP32Service.instance.z = 0;
    
    // Send G-Code to physically home
    ESP32Service.instance.sendCommand("G0 X0 Y0 Z0 F${(_velocity * 60).toInt()}");
    HapticFeedback.mediumImpact();
  }


  // ── Limit Warning Dialog ─────────────────────────────────

  void _showLimitWarning(String axis, String limitType) {
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
            const Icon(Icons.warning_amber_rounded, color: Color(0xFFF59E0B), size: 28),
            const SizedBox(width: 12),
            Text(
              '${axis.toUpperCase()}-Axis Limit',
              style: TextStyle(color: isDark ? Colors.white : Colors.black, fontSize: 18, fontWeight: FontWeight.bold),
            ),
          ],
        ),
        content: Text(
          'Movement blocked. You have reached the $limitType physical limit for the ${axis.toUpperCase()} axis (0mm - 1000mm).',
          style: TextStyle(color: isDark ? const Color(0xFFD1D5DB) : Colors.grey.shade700, height: 1.5),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            style: TextButton.styleFrom(
              backgroundColor: isDark ? const Color(0xFF374151) : Colors.grey.shade200,
              foregroundColor: isDark ? Colors.white : Colors.black,
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
            ),
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }

  // ── Fertilize & Weeder Logic ─────────────────────────────

  void _handleFertilize() {
    setState(() => _isFertilizing = true);
    _fertilizeTimer = Timer(
      Duration(milliseconds: (_fertilizeAmount * 10).toInt()),
      () {
        setState(() => _isFertilizing = false);
      },
    );
  }

  void _handleStopFertilize() {
    _fertilizeTimer?.cancel();
    setState(() => _isFertilizing = false);
  }

  void _handleWeederToggle() {
    if (_isWeederOn) {
      setState(() => _isWeederOn = false);
    } else {
      _showWeederWarning();
    }
  }

  void _showWeederWarning() {
    final isDark = Theme.of(context).brightness == Brightness.dark;
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (_) => AlertDialog(
        backgroundColor: isDark ? const Color(0xFF1F2937) : Colors.white,
        shape: RoundedRectangleBorder(
          side: const BorderSide(color: Color(0xFFEF4444), width: 2), // Always red danger border
          borderRadius: BorderRadius.circular(16),
        ),
        title: Row(
          children: [
            Container(
              width: 48, height: 48,
              decoration: BoxDecoration(
                color: const Color(0xFFEF4444).withValues(alpha: 0.2),
                shape: BoxShape.circle,
              ),
              child: const Icon(Icons.warning_amber_rounded, color: Color(0xFFEF4444), size: 24),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Text(
                'Warning: Activate Weeder?',
                style: TextStyle(color: isDark ? Colors.white : Colors.black, fontSize: 18, fontWeight: FontWeight.w900),
              ),
            ),
          ],
        ),
        content: Text(
          'You are about to activate the weeder system. This will start the cutting mechanism. Make sure the area is clear and safe to proceed.',
          style: TextStyle(color: isDark ? const Color(0xFFD1D5DB) : Colors.grey.shade700, height: 1.6),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            style: TextButton.styleFrom(
              backgroundColor: isDark ? const Color(0xFF374151) : Colors.grey.shade200,
              foregroundColor: isDark ? Colors.white : Colors.black,
              padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
            ),
            child: const Text('Cancel', style: TextStyle(fontWeight: FontWeight.bold)),
          ),
          TextButton(
            onPressed: () {
              setState(() => _isWeederOn = true);
              Navigator.pop(context);
            },
            style: TextButton.styleFrom(
              backgroundColor: const Color(0xFFEF4444),
              foregroundColor: Colors.white,
              padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
            ),
            child: const Text('Activate Weeder', style: TextStyle(fontWeight: FontWeight.bold)),
          ),
        ],
      ),
    );
  }

  // ── Build ────────────────────────────────────────────────

  @override
  Widget build(BuildContext context) {
    final themeState = ref.watch(themeProvider);
    final isDark = Theme.of(context).brightness == Brightness.dark;
    final accentColor = themeState.currentAccentColor; 

    return ListenableBuilder(
      listenable: ESP32Service.instance,
      builder: (context, _) {
        final service = ESP32Service.instance;

        // Wrap with RefreshIndicator for pull-to-refresh hardware scanning
        return RefreshIndicator(
          onRefresh: _startScan,
          color: accentColor,
          child: SingleChildScrollView(
            physics: const AlwaysScrollableScrollPhysics(),
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                _buildVisualization(accentColor, isDark),
                const SizedBox(height: 16),
                _buildManualControl(accentColor, isDark),
                const SizedBox(height: 24),
                // Terminal placed exactly below UI
                _buildGCodeTerminal(service, accentColor, isDark),
                const SizedBox(height: 32),
              ],
            ),
          ),
        );
      }
    );
  }

  // ── GCode Terminal Widget ────────────────────────────────
  
  Widget _buildGCodeTerminal(ESP32Service service, Color accentColor, bool isDark) {
    final containerBg = isDark ? const Color(0xFF1F2937) : Colors.white;
    final containerBorder = isDark ? const Color(0xFF374151) : Colors.grey.shade300;

    return Container(
      decoration: BoxDecoration(
        color: containerBg,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: containerBorder),
      ),
      child: Column(
        children: [
          Padding(
            padding: const EdgeInsets.all(16),
            child: Row(
              children: [
                const Icon(Icons.terminal, color: Color(0xFFF59E0B), size: 20),
                const SizedBox(width: 10),
                Text("GCODE TERMINAL",
                    style: TextStyle(
                        color: isDark ? Colors.white : Colors.black87,
                        fontWeight: FontWeight.w900,
                        fontStyle: FontStyle.italic,
                        fontSize: 14)),
                const Spacer(),
                
                // Refresh Icon / Loading Spinner
                _isScanning
                    ? SizedBox(
                        width: 18, height: 18,
                        child: CircularProgressIndicator(strokeWidth: 2, color: accentColor),
                      )
                    : IconButton(
                        padding: EdgeInsets.zero,
                        constraints: const BoxConstraints(),
                        icon: Icon(Icons.refresh, size: 20, color: isDark ? Colors.white54 : Colors.grey),
                        onPressed: _startScan,
                        tooltip: "Scan for ESP32",
                      ),
                const SizedBox(width: 12),
                
                Icon(Icons.circle,
                    size: 10,
                    color: service.isConnected ? const Color(0xFF10B981) : const Color(0xFFEF4444)),
                const SizedBox(width: 6),
                Text(
                  service.isConnected ? "Online" : "Offline",
                  style: TextStyle(
                    fontSize: 12, fontWeight: FontWeight.bold,
                    color: service.isConnected ? const Color(0xFF10B981) : const Color(0xFFEF4444),
                  ),
                ),
              ],
            ),
          ),
          Container(
            height: 200,
            margin: const EdgeInsets.symmetric(horizontal: 16),
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
                color: isDark ? const Color(0xFF030712) : const Color(0xFFF8FAFC), 
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: containerBorder)),
            child: ListView.builder(
              controller: _terminalScrollController,
              itemCount: service.logs.length,
              itemBuilder: (context, i) => _buildLogLine(service.logs[i], isDark),
            ),
          ),
          Padding(
            padding: const EdgeInsets.all(16),
            child: TextField(
              controller: _terminalController,
              style: TextStyle(color: isDark ? Colors.white : Colors.black, fontSize: 13, fontFamily: 'monospace'),
              decoration: InputDecoration(
                hintText: "Type GCode...",
                hintStyle: TextStyle(color: isDark ? Colors.white30 : Colors.black38),
                prefixText: "\$ ",
                filled: true,
                fillColor: isDark ? const Color(0xFF374151) : Colors.grey.shade100,
                contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: containerBorder)),
                enabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: containerBorder)),
                focusedBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: accentColor)),
              ),
              onSubmitted: (val) {
                service.sendCommand(val);
                _terminalController.clear();
              },
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildLogLine(String log, bool isDark) {
    final isTx = log.startsWith("TX:");
    final isSys = log.startsWith("SYS");
    final display = log.length > 3 ? log.substring(3) : log;
    
    return Text(
      "${DateFormat('HH:mm:ss').format(DateTime.now())} ${isTx ? '→' : isSys ? '•' : '←'} $display",
      style: TextStyle(
        color: isSys
            ? (isDark ? Colors.white54 : Colors.grey.shade600)
            : isTx
                ? const Color(0xFFF59E0B)
                : const Color(0xFF10B981),
        fontSize: 12,
        fontFamily: 'monospace',
      ),
    );
  }

  // ── Gantry Visualization ─────────────────────────────────

  Widget _buildVisualization(Color accentColor, bool isDark) {
    final containerBg = isDark ? const Color(0xFF1F2937) : Colors.white;
    final containerBorder = isDark ? const Color(0xFF374151) : Colors.grey.shade300;
    final innerBg = isDark ? const Color(0xFF030712) : const Color(0xFFF8FAFC);
    final textColor = isDark ? Colors.white : Colors.black87;

    return Container(
      decoration: BoxDecoration(
        color: containerBg,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: containerBorder),
      ),
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          RichText(
            text: TextSpan(
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: textColor),
              children: [
                const TextSpan(text: 'Gantry '),
                TextSpan(text: 'Visualization', style: TextStyle(color: accentColor)),
              ],
            ),
          ),
          const SizedBox(height: 16),
          SizedBox(
            height: 280,
            child: Container(
              decoration: BoxDecoration(
                color: innerBg,
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: containerBorder),
              ),
              child: Stack(
                children: [
                  Positioned(
                    left: 12, top: 12,
                    child: Container(
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                      decoration: BoxDecoration(
                        color: isDark ? Colors.black.withValues(alpha: 0.4) : Colors.white.withValues(alpha: 0.8),
                        borderRadius: BorderRadius.circular(8),
                      ),
                      child: Text(
                        'COORDINATES',
                        style: TextStyle(fontSize: 8, color: isDark ? const Color(0xFF9CA3AF) : Colors.grey.shade600, fontFamily: 'monospace', letterSpacing: 2),
                      ),
                    ),
                  ),

                  Positioned(
                    left: 12, bottom: 12,
                    child: Container(
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                      decoration: BoxDecoration(
                        color: isDark ? Colors.black.withValues(alpha: 0.4) : Colors.white.withValues(alpha: 0.8),
                        borderRadius: BorderRadius.circular(8),
                      ),
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          _coordLine('x', _position.x, accentColor, textColor),
                          const SizedBox(width: 14), 
                          _coordLine('y', _position.y, accentColor, textColor),
                          const SizedBox(width: 14),
                          _coordLine('z', _position.z, accentColor, textColor),
                        ],
                      ),
                    ),
                  ),

                  Center(
                    child: SizedBox(
                      width: 200, height: 200,
                      child: DecoratedBox(
                        decoration: BoxDecoration(
                          border: Border.all(color: containerBorder, width: 2),
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: Stack(
                          children: [
                            Positioned.fill(child: CustomPaint(painter: _DashedBorderPainter(color: containerBorder))),
                            TweenAnimationBuilder<Offset>(
                              tween: Tween<Offset>(
                                begin: Offset.zero,
                                end: Offset((_position.x / 1000) * 176 - 88, (_position.y / 1000) * 176 - 88),
                              ),
                              duration: const Duration(milliseconds: 400),
                              curve: Curves.easeOutBack,
                              builder: (_, offset, __) {
                                final scale = 1.0 + _position.z / 500;
                                return Transform.translate(
                                  offset: offset,
                                  child: Center(
                                    child: Transform.scale(
                                      scale: scale,
                                      child: Container(
                                        width: 18, height: 18,
                                        decoration: BoxDecoration(
                                          color: accentColor,
                                          borderRadius: BorderRadius.circular(4),
                                          boxShadow: [
                                            BoxShadow(color: accentColor.withValues(alpha: 0.6), blurRadius: 12, spreadRadius: 2),
                                          ],
                                        ),
                                      ),
                                    ),
                                  ),
                                );
                              },
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
    final valStr = val.toStringAsFixed(0);
    return RichText(
      text: TextSpan(
        style: TextStyle(fontFamily: 'monospace', fontSize: 13, color: textColor),
        children: [
          TextSpan(text: '$axis: ', style: TextStyle(color: accentColor)),
          TextSpan(text: valStr),
        ],
      ),
    );
  }

  // ── Manual Control ───────────────────────────────────────

  Widget _buildManualControl(Color accentColor, bool isDark) {
    final isWide = MediaQuery.of(context).size.width >= 700;
    final containerBg = isDark ? const Color(0xFF1F2937) : Colors.white;
    final containerBorder = isDark ? const Color(0xFF374151) : Colors.grey.shade300;
    final textColor = isDark ? Colors.white : Colors.black87;

    return Container(
      decoration: BoxDecoration(
        color: containerBg,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: containerBorder),
      ),
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          RichText(
            text: TextSpan(
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: textColor),
              children: [
                const TextSpan(text: 'Manual '),
                TextSpan(text: 'Control', style: TextStyle(color: accentColor)),
              ],
            ),
          ),
          const SizedBox(height: 24),
          if (isWide)
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Expanded(child: _buildDPad(accentColor, isDark)),
                const SizedBox(width: 32),
                Expanded(child: _buildRightColumn(accentColor, isDark)),
              ],
            )
          else
            Column(
              children: [
                _buildDPad(accentColor, isDark),
                const SizedBox(height: 24),
                _buildRightColumn(accentColor, isDark),
              ],
            ),
        ],
      ),
    );
  }

  Widget _buildDPad(Color accentColor, bool isDark) {
    return Column(
      children: [
        Text('AXIS CONTROL (X/Y)', style: TextStyle(fontSize: 10, color: isDark ? const Color(0xFF9CA3AF) : Colors.grey.shade600, letterSpacing: 2)),
        const SizedBox(height: 16),
        SizedBox(
          width: 200, 
          child: Column(
            children: [
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const SizedBox(width: 60),
                  _ControlBtn(icon: Icons.keyboard_arrow_up, onTap: () => _move('y', -1), accentColor: accentColor, isDark: isDark),
                  const SizedBox(width: 60),
                ],
              ),
              const SizedBox(height: 8),
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  _ControlBtn(icon: Icons.keyboard_arrow_left, onTap: () => _move('x', -1), accentColor: accentColor, isDark: isDark),
                  const SizedBox(width: 8),
                  GestureDetector(
                    onTap: _handleHome,
                    child: Container(
                      width: 56, height: 56,
                      decoration: BoxDecoration(
                        color: isDark ? const Color(0xFF374151) : Colors.grey.shade100, // Background matches unhovered buttons
                        borderRadius: BorderRadius.circular(12),
                        border: Border.all(color: accentColor), // Always shows accent color border
                      ),
                      child: Icon(Icons.my_location, color: accentColor, size: 24), // Icon is accent color
                    ),
                  ),
                  const SizedBox(width: 8),
                  _ControlBtn(icon: Icons.keyboard_arrow_right, onTap: () => _move('x', 1), accentColor: accentColor, isDark: isDark),
                ],
              ),
              const SizedBox(height: 8),
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const SizedBox(width: 60),
                  _ControlBtn(icon: Icons.keyboard_arrow_down, onTap: () => _move('y', 1), accentColor: accentColor, isDark: isDark),
                  const SizedBox(width: 60),
                ],
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildRightColumn(Color accentColor, bool isDark) {
    final idleBtnColor = isDark ? const Color(0xFF374151) : Colors.grey.shade100;
    final idleBorderColor = isDark ? const Color(0xFF4B5563) : Colors.grey.shade300;
    final textColor = isDark ? Colors.white : Colors.black87;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            _ControlBtn(icon: Icons.keyboard_arrow_up, onTap: () => _move('z', 1), accentColor: accentColor, isDark: isDark),
            const SizedBox(width: 12),
            _ControlBtn(icon: Icons.keyboard_arrow_down, onTap: () => _move('z', -1), accentColor: accentColor, isDark: isDark),
          ],
        ),
        const SizedBox(height: 20),
        _label('DISTANCE', isDark),
        const SizedBox(height: 8),
        Row(
          children: [0.1, 1.0, 10.0, 100.0].map((v) {
            final selected = _speedMode == v;
            return Expanded(
              child: Padding(
                padding: const EdgeInsets.only(right: 6),
                child: GestureDetector(
                  onTap: () => setState(() => _speedMode = v),
                  child: Container(
                    padding: const EdgeInsets.symmetric(vertical: 8),
                    decoration: BoxDecoration(
                      color: selected ? accentColor : idleBtnColor,
                      borderRadius: BorderRadius.circular(8),
                      border: Border.all(color: selected ? accentColor : idleBorderColor),
                    ),
                    child: Text(
                      '${v % 1 == 0 ? v.toInt() : v}mm',
                      textAlign: TextAlign.center,
                      style: TextStyle(
                        fontSize: 11, fontWeight: FontWeight.bold,
                        color: selected ? Colors.white : (isDark ? const Color(0xFF9CA3AF) : Colors.grey.shade700),
                      ),
                    ),
                  ),
                ),
              ),
            );
          }).toList(),
        ),
        const SizedBox(height: 16),
        _label('VELOCITY (mm/s)', isDark),
        const SizedBox(height: 6),
        _numberField(value: _velocity, onChanged: (v) => setState(() => _velocity = v), accentColor: accentColor, isDark: isDark, textColor: textColor),
        const SizedBox(height: 16),
        _label('ACCELERATION (mm²/s)', isDark),
        const SizedBox(height: 6),
        _numberField(value: _acceleration, onChanged: (v) => setState(() => _acceleration = v), accentColor: accentColor, isDark: isDark, textColor: textColor),
        const SizedBox(height: 20),

        // ── ACTION BUTTONS (ALWAYS SPECIFIC COLORS) ──

        // Irrigate: Always Blue (0xFF2563EB)
        GestureDetector(
          onTap: () => setState(() => _isIrrigating = !_isIrrigating),
          child: Container(
            padding: const EdgeInsets.symmetric(vertical: 14),
            decoration: BoxDecoration(
              color: const Color(0xFF2563EB), 
              borderRadius: BorderRadius.circular(10),
              border: Border.all(color: const Color(0xFF60A5FA)),
            ),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const Icon(Icons.water_drop_outlined, color: Colors.white, size: 16),
                const SizedBox(width: 8),
                Text('Irrigate: ${_isIrrigating ? "ON" : "OFF"}',
                  style: const TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 10),

        // Fertilize: Always Green (0xFF16A34A)
        _ActionRow(
          amount: _fertilizeAmount,
          onAmountChanged: (v) => setState(() => _fertilizeAmount = v),
          amountEnabled: !_isFertilizing,
          onAction: _handleFertilize,
          onStop: _handleStopFertilize,
          isActive: _isFertilizing,
          activeLabel: 'Fertilizing...',
          idleLabel: 'Fertilize',
          activeColor: const Color(0xFF16A34A), 
          textColor: textColor,
          icon: Icons.eco_outlined,
          accentColor: accentColor, 
          isDark: isDark,
        ),
        const SizedBox(height: 10),

        // Weeder: Always Red (0xFFDC2626)
        GestureDetector(
          onTap: _handleWeederToggle,
          child: Container(
            padding: const EdgeInsets.symmetric(vertical: 14),
            decoration: BoxDecoration(
              color: const Color(0xFFDC2626),
              borderRadius: BorderRadius.circular(10),
              border: Border.all(color: const Color(0xFFEF4444)),
            ),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const Icon(Icons.content_cut, color: Colors.white, size: 16),
                const SizedBox(width: 8),
                Text('Weeder: ${_isWeederOn ? "ON" : "OFF"}',
                  style: const TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }

  Widget _label(String text, bool isDark) => Text(text,
        style: TextStyle(fontSize: 10, color: isDark ? const Color(0xFF9CA3AF) : Colors.grey.shade600, letterSpacing: 2),
      );

  Widget _numberField({
    required double value,
    required ValueChanged<double> onChanged,
    required Color accentColor,
    required bool isDark,
    required Color textColor,
  }) {
    final fieldBg = isDark ? const Color(0xFF374151) : Colors.grey.shade100;
    final fieldBorder = isDark ? const Color(0xFF4B5563) : Colors.grey.shade300;

    return TextFormField(
      initialValue: value.toString(),
      keyboardType: TextInputType.number,
      style: TextStyle(color: textColor, fontSize: 14),
      decoration: InputDecoration(
        filled: true, fillColor: fieldBg,
        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        border: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBorder)),
        enabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBorder)),
        focusedBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: accentColor)), 
      ),
      onChanged: (s) {
        final parsed = double.tryParse(s);
        if (parsed != null) onChanged(parsed);
      },
    );
  }
}

// ─────────────────────────────────────────────────────────────
// _ControlBtn  (XYZ Arrow buttons)
// ─────────────────────────────────────────────────────────────

class _ControlBtn extends StatelessWidget {
  const _ControlBtn({
    required this.icon, 
    required this.onTap, 
    required this.accentColor, 
    required this.isDark
  });

  final IconData icon;
  final VoidCallback onTap;
  final Color accentColor;
  final bool isDark;

  @override
  Widget build(BuildContext context) {
    // Background adapts to Light/Dark mode
    final btnBg = isDark ? const Color(0xFF374151) : Colors.grey.shade100;

    return GestureDetector(
      onTap: onTap,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 150),
        width: 56,
        height: 56,
        decoration: BoxDecoration(
          color: btnBg,
          borderRadius: BorderRadius.circular(12),
          // Border is permanently the Accent Color
          border: Border.all(color: accentColor, width: 1.5),
        ),
        // Icon is permanently the Accent Color
        child: Icon(
          icon, 
          size: 26, 
          color: accentColor,
        ),
      ),
    );
  }
}

// ─────────────────────────────────────────────────────────────
// _ActionRow (Fertilize)
// ─────────────────────────────────────────────────────────────

class _ActionRow extends StatelessWidget {
  const _ActionRow({
    required this.amount, required this.onAmountChanged, required this.amountEnabled,
    required this.onAction, required this.onStop, required this.isActive,
    required this.activeLabel, required this.idleLabel,
    required this.activeColor, required this.textColor, required this.icon, required this.accentColor, required this.isDark
  });

  final double amount;
  final ValueChanged<double> onAmountChanged;
  final bool amountEnabled;
  final VoidCallback onAction, onStop;
  final bool isActive;
  final String activeLabel, idleLabel;
  final Color activeColor, textColor;
  final IconData icon;
  final Color accentColor;
  final bool isDark;

  @override
  Widget build(BuildContext context) {
    final label = isActive ? activeLabel : idleLabel;
    final fieldBg = isDark ? const Color(0xFF374151) : Colors.grey.shade100;
    final fieldBorder = isDark ? const Color(0xFF4B5563) : Colors.grey.shade300;

    return Row(
      children: [
        SizedBox(
          width: 72,
          child: TextFormField(
            initialValue: amount.toInt().toString(),
            keyboardType: TextInputType.number,
            enabled: amountEnabled,
            style: TextStyle(color: textColor, fontSize: 13),
            decoration: InputDecoration(
              hintText: 'ml', hintStyle: TextStyle(color: isDark ? const Color(0xFF6B7280) : Colors.grey, fontSize: 13),
              filled: true, fillColor: fieldBg,
              contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 10),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBorder)),
              enabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBorder)),
              disabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: fieldBg)),
              focusedBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(8), borderSide: BorderSide(color: accentColor)), 
            ),
            onChanged: (s) {
              final v = double.tryParse(s);
              if (v != null) onAmountChanged(v);
            },
          ),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: GestureDetector(
            onTap: isActive ? null : onAction,
            child: Container(
              padding: const EdgeInsets.symmetric(vertical: 12),
              decoration: BoxDecoration(
                color: activeColor, // Always Green
                borderRadius: BorderRadius.circular(10),
                border: Border.all(color: const Color(0xFF4ADE80)), // Light green border
              ),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(icon, color: Colors.white, size: 16),
                  const SizedBox(width: 6),
                  Text(label, style: const TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold)),
                ],
              ),
            ),
          ),
        ),
        if (isActive) ...[
          const SizedBox(width: 8),
          GestureDetector(
            onTap: onStop,
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
              decoration: BoxDecoration(
                color: const Color(0xFFDC2626), 
                borderRadius: BorderRadius.circular(10),
                border: Border.all(color: const Color(0xFFEF4444)),
              ),
              child: const Text('Stop', style: TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold)),
            ),
          ),
        ],
      ],
    );
  }
}

// ─────────────────────────────────────────────────────────────
// Dashed border painter
// ─────────────────────────────────────────────────────────────

class _DashedBorderPainter extends CustomPainter {
  final Color color;
  _DashedBorderPainter({required this.color});

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()..color = color..strokeWidth = 1.5..style = PaintingStyle.stroke;
    const dashLen = 6.0; const gapLen = 4.0;
    final rrect = RRect.fromRectAndRadius(Rect.fromLTWH(0, 0, size.width, size.height), const Radius.circular(8));
    final path = Path()..addRRect(rrect);
    for (final m in path.computeMetrics()) {
      double dist = 0;
      while (dist < m.length) {
        canvas.drawPath(m.extractPath(dist, dist + dashLen), paint);
        dist += dashLen + gapLen;
      }
    }
  }

  @override
  bool shouldRepaint(covariant CustomPainter old) => true;
}