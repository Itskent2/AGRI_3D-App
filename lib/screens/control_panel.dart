import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:intl/intl.dart';
import '../service/farmbot_service.dart';
import '../providers/theme_provider.dart';

class ControlPanel extends ConsumerStatefulWidget {
  const ControlPanel({super.key});
  @override
  ConsumerState<ControlPanel> createState() => _ControlPanelState();
}

class _ControlPanelState extends ConsumerState<ControlPanel> {
  final double _speedMode = 1;
  final double _velocity = 5;
  bool _isScanning = false; // Tracks if a scan is currently running
  
  final TextEditingController _terminalController = TextEditingController();
  final ScrollController _terminalScrollController = ScrollController();

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _startScan();
    });
    FarmbotService.instance.addListener(_scrollToBottom);
  }

  @override
  void dispose() {
    FarmbotService.instance.removeListener(_scrollToBottom);
    _terminalController.dispose();
    _terminalScrollController.dispose();
    super.dispose();
  }

  // Handles triggering the auto-discovery and updating the UI state
  Future<void> _startScan() async {
    if (_isScanning) return; // Prevent multiple simultaneous scans

    setState(() => _isScanning = true);
    HapticFeedback.mediumImpact(); // Nice tactile feedback for the user

    await FarmbotService.instance.autoDiscover();

    if (mounted) {
      setState(() => _isScanning = false);
    }
  }

  void _scrollToBottom() {
    if (_terminalScrollController.hasClients) {
      _terminalScrollController.animateTo(
        _terminalScrollController.position.maxScrollExtent,
        duration: const Duration(milliseconds: 300),
        curve: Curves.easeOut,
      );
    }
  }

  void _move(String axis, int direction) {
    final service = FarmbotService.instance;
    double currentVal = (axis == 'x') ? service.x : (axis == 'y' ? service.y : service.z);
    double nextVal = currentVal + (direction * _speedMode);

    if (nextVal >= 0 && nextVal <= 1000) {
      final gcode = "G0 ${axis.toUpperCase()}${nextVal.toStringAsFixed(1)} F${(_velocity * 60).toInt()}";
      service.updatePos(axis, nextVal, gcode);
      HapticFeedback.lightImpact();
    }
  }

  @override
  Widget build(BuildContext context) {
    final isDark = Theme.of(context).brightness == Brightness.dark;
    final accentColor = ref.watch(themeProvider).currentAccentColor;

    return ListenableBuilder(
      listenable: FarmbotService.instance,
      builder: (context, _) {
        final service = FarmbotService.instance;
        
        // Wrapped in RefreshIndicator to allow pull-to-refresh
        return RefreshIndicator(
          onRefresh: _startScan,
          color: accentColor,
          child: SingleChildScrollView(
            // AlwaysScrollable ensures pull-to-refresh works even if content doesn't fill the screen
            physics: const AlwaysScrollableScrollPhysics(), 
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                _buildVisualization(service, accentColor, isDark),
                const SizedBox(height: 16),
                _buildManualControl(accentColor, isDark),
                const SizedBox(height: 24),
                _buildGCodeTerminal(service, accentColor, isDark),
              ],
            ),
          ),
        );
      },
    );
  }

  Widget _buildGCodeTerminal(FarmbotService service, Color accentColor, bool isDark) {
    return Container(
      decoration: BoxDecoration(
        color: isDark ? const Color(0xFF1E2530) : Colors.grey.shade200,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: Colors.white10),
      ),
      child: Column(
        children: [
          Padding(
            padding: const EdgeInsets.all(12),
            child: Row(
              children: [
                const Icon(Icons.terminal, color: Colors.orange, size: 18),
                const SizedBox(width: 10),
                const Text("GCODE\nTERMINAL",
                    style: TextStyle(
                        color: Colors.white,
                        fontWeight: FontWeight.bold,
                        fontSize: 11)),
                const Spacer(),
                
                // Refresh Icon / Loading Spinner
                _isScanning
                    ? const SizedBox(
                        width: 18,
                        height: 18,
                        child: CircularProgressIndicator(
                          strokeWidth: 2,
                          color: Colors.orange,
                        ),
                      )
                    : IconButton(
                        padding: EdgeInsets.zero,
                        constraints: const BoxConstraints(),
                        icon: const Icon(Icons.refresh, size: 20, color: Colors.white70),
                        onPressed: _startScan,
                        tooltip: "Scan for ESP32",
                      ),
                const SizedBox(width: 12),
                
                Icon(Icons.circle,
                    size: 8,
                    color: service.isConnected ? Colors.green : Colors.red),
                const SizedBox(width: 6),
                Text(
                  service.isConnected ? "Online" : "Offline",
                  style: TextStyle(
                    fontSize: 10,
                    color: service.isConnected ? Colors.green : Colors.red,
                  ),
                ),
              ],
            ),
          ),
          Container(
            height: 200,
            margin: const EdgeInsets.symmetric(horizontal: 12),
            padding: const EdgeInsets.all(8),
            decoration: BoxDecoration(
                color: Colors.black, borderRadius: BorderRadius.circular(8)),
            child: ListView.builder(
              controller: _terminalScrollController,
              itemCount: service.logs.length,
              itemBuilder: (context, i) => _buildLogLine(service.logs[i]),
            ),
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            child: TextField(
              controller: _terminalController,
              style: const TextStyle(
                  color: Colors.white, fontSize: 13, fontFamily: 'monospace'),
              decoration: const InputDecoration(
                hintText: "Type GCode...",
                prefixText: "\$ ",
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

  Widget _buildLogLine(String log) {
  final isTx = log.startsWith("TX:");
  final isSys = log.startsWith("SYS:");
  final isRx = log.startsWith("RX:");

  String display;
  if (isTx || isRx) {
    display = log.substring(4); // removes "TX: " or "RX: "
  } else if (isSys) {
    display = log.substring(5); // removes "SYS: "
  } else {
    display = log;
  }

  return Text(
    "${DateFormat('HH:mm:ss').format(DateTime.now())} ${isTx ? '→' : isSys ? '•' : '←'} $display",
    style: TextStyle(
      color: isSys
          ? Colors.white54
          : isTx
              ? Colors.orange
              : Colors.greenAccent,
      fontSize: 11,
      fontFamily: 'monospace',
    ),
  );
}

  Widget _buildVisualization(
      FarmbotService service, Color accentColor, bool isDark) {
    return Container(
      height: 200,
      width: double.infinity,
      decoration: BoxDecoration(
          color: isDark ? Colors.black26 : Colors.white,
          borderRadius: BorderRadius.circular(12)),
      child: Stack(children: [
        Positioned(
          left: (service.x / 1000) * 300,
          top: (service.y / 1000) * 150,
          child: Container(width: 15, height: 15, color: accentColor),
        )
      ]),
    );
  }

  Widget _buildManualControl(Color accentColor, bool isDark) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        IconButton(
            icon: const Icon(Icons.arrow_back),
            onPressed: () => _move('x', -1)),
        IconButton(
            icon: const Icon(Icons.arrow_upward),
            onPressed: () => _move('y', -1)),
        IconButton(
            icon: const Icon(Icons.arrow_downward),
            onPressed: () => _move('y', 1)),
        IconButton(
            icon: const Icon(Icons.arrow_forward),
            onPressed: () => _move('x', 1)),
      ],
    );
  }
}