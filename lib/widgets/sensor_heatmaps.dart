// lib/widgets/sensor_heatmaps.dart

import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'package:web_socket_channel/status.dart' as status;

import '../providers/theme_provider.dart';

// ── CONFIG ──
const int gridSize = 50;

// ⚠️ CHANGE THIS TO YOUR ESP32 IP
const String wsUrl = "ws://192.168.1.5:81";

// ── STATUS ENUM ──
enum WsStatus { connected, connecting, disconnected, error }

// ── DATA MODEL ──
class SensorGridData {
  final List<List<double>> moisture;

  SensorGridData({required this.moisture});
}

// ── MAIN WIDGET ──
class SensorHeatmaps extends ConsumerStatefulWidget {
  const SensorHeatmaps({super.key});

  @override
  ConsumerState<SensorHeatmaps> createState() => _SensorHeatmapsState();
}

class _SensorHeatmapsState extends ConsumerState<SensorHeatmaps> {
  late SensorGridData sensorData;

  WebSocketChannel? _channel;
  WsStatus _status = WsStatus.disconnected;
  String _message = "Not connected";

  // ── INIT ──
  @override
  void initState() {
    super.initState();
    sensorData = SensorGridData(
      moisture: List.generate(gridSize, (_) => List.filled(gridSize, 0.0)),
    );
    _connect();
  }

  @override
  void dispose() {
    try {
      _channel?.sink.close(status.goingAway);
    } catch (_) {}
    super.dispose();
  }

  // ── CONNECT ──
  void _connect() {
    setState(() {
      _status = WsStatus.connecting;
      _message = "Connecting...";
    });

    try {
      _channel = WebSocketChannel.connect(Uri.parse(wsUrl));

      setState(() {
        _status = WsStatus.connected;
        _message = "Connected";
      });

      _channel!.stream.listen(
        _onData,
        onError: _onError,
        onDone: _onDisconnect,
      );
    } catch (e) {
      _onError(e);
    }
  }

  void _onData(dynamic msg) {
    try {
      final data = jsonDecode(msg);

      int x = (data['x'] as num).toInt().clamp(0, gridSize - 1);
      int y = (data['y'] as num).toInt().clamp(0, gridSize - 1);

      double value = (data['m'] ?? data['val'] ?? 0).toDouble();

      setState(() {
        // ✅ FIXED indexing
        sensorData.moisture[y][x] = value;
        _message = "Receiving data... ($x,$y)";
      });
    } catch (_) {}
  }

  void _onError(error) {
    setState(() {
      _status = WsStatus.error;
      _message = "Error";
    });
  }

  void _onDisconnect() {
    setState(() {
      _status = WsStatus.disconnected;
      _message = "Disconnected";
    });
  }

  // ── COLOR LOGIC ──
  Color getColor(double v) {
    if (v == 0) return const Color(0xFF1C2333);
    if (v < 30) return Colors.red;
    if (v < 60) return Colors.orange;
    return Colors.green;
  }

  // ── BUILD ──
  @override
  Widget build(BuildContext context) {
    final theme = ref.watch(themeProvider);
    final isDark = theme.isDark(context);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          "HEATMAP STATUS: $_message",
          style: TextStyle(
            color: _status == WsStatus.connected
                ? Colors.green
                : Colors.red,
          ),
        ),
        const SizedBox(height: 10),

        SizedBox(
          height: 300,
          child: CustomPaint(
            painter: _HeatmapPainter(
              data: sensorData.moisture,
              getColor: getColor,
            ),
          ),
        ),
      ],
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
    double w = size.width / gridSize;
    double h = size.height / gridSize;

    final paint = Paint();

    for (int r = 0; r < gridSize; r++) {
      for (int c = 0; c < gridSize; c++) {
        paint.color = getColor(data[r][c]);
        canvas.drawRect(
          Rect.fromLTWH(c * w, r * h, w, h),
          paint,
        );
      }
    }
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => true;
}