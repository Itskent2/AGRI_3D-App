// lib/service/ESP32/esp32_service.dart

import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'package:shared_preferences/shared_preferences.dart';

class ESP32Service extends ChangeNotifier {
  WebSocketChannel? _channel;
  bool isConnected = false;
  bool isScanning = false;
  String? _lastKnownIP;
  String? get currentIP => _lastKnownIP;
  List<String> logs = [];

  final ValueNotifier<Uint8List?> cameraFrame = ValueNotifier(null);

  double x = 0, y = 0, z = 0;
  double maxX = 1000.0, maxY = 1000.0, maxZ = 1000.0;

  // ── NEW: Track if the machine is Homed, Idle, or Running ──
  String machineState = "Unknown";

  static final ESP32Service instance = ESP32Service();

  Future<void> autoDiscover() async {
    if (isConnected || isScanning) return;
    isScanning = true;
    notifyListeners();

    final prefs = await SharedPreferences.getInstance();
    _lastKnownIP = prefs.getString('lastKnownIP');

    while (!isConnected) {
      if (kIsWeb) await _connectAndVerify("192.168.0.134");
      if (!isConnected && _lastKnownIP != null) await _connectAndVerify(_lastKnownIP!);
      if (!isConnected && !kIsWeb) await _connectAndVerify("farmbot.local");
      if (!isConnected) await _connectAndVerify("192.168.4.1");
      if (!isConnected && !kIsWeb) await sweepMobileSubnets(this); 
      if (!isConnected) await Future.delayed(const Duration(seconds: 3));
    }

    isScanning = false;
    notifyListeners();
  }

  Future<void> _connectAndVerify(String host) async {
    if (isConnected) return;
    if (_channel != null) { _channel!.sink.close(); _channel = null; }

    try {
      final url = "ws://$host/ws";
      final channel = WebSocketChannel.connect(Uri.parse(url));
      await channel.ready.timeout(const Duration(seconds: 3));

      final completer = Completer<bool>();
      bool identified = false;
      _channel = channel;

      _channel!.stream.listen(
        (msg) async {
          if (msg is List<int>) {
            if (identified) cameraFrame.value = msg is Uint8List ? msg : Uint8List.fromList(msg);
            return;
          }

          String textMsg = msg.toString();

          if (!identified) {
            if (textMsg.contains('"system":"AGRI_3D"') || textMsg.startsWith("FARMBOT_ID:")) {
              identified = true;
              isConnected = true;
              _lastKnownIP = host;
              SharedPreferences.getInstance().then((prefs) => prefs.setString('lastKnownIP', host));
              _addLog("SYS: Online → $host");

              try {
                final parsed = jsonDecode(textMsg);
                if (parsed['x'] != null) x = (parsed['x'] as num).toDouble();
                if (parsed['y'] != null) y = (parsed['y'] as num).toDouble();
                if (parsed['z'] != null) z = (parsed['z'] as num).toDouble();
                if (parsed['maxX'] != null) maxX = (parsed['maxX'] as num).toDouble();
                if (parsed['maxY'] != null) maxY = (parsed['maxY'] as num).toDouble();
                notifyListeners();
              } catch (_) {}
              if (!completer.isCompleted) completer.complete(true);
            }
            return;
          }
          
          // FIXED: Filter out continuous status reports from terminal logs
          //if (!textMsg.contains("<") && !textMsg.contains(">")) {
            _addLog("RX: $textMsg");
          //}

          try {
            final parsed = jsonDecode(textMsg);
            if (parsed['nano_raw'] != null) {
              String raw = parsed['nano_raw'].toString();
              _parseGrblStatus(raw);

              if (raw.startsWith("\$130=")) { maxX = double.tryParse(raw.substring(5)) ?? maxX; notifyListeners(); }
              if (raw.startsWith("\$131=")) { maxY = double.tryParse(raw.substring(5)) ?? maxY; notifyListeners(); }
              if (raw.startsWith("\$132=")) { maxZ = double.tryParse(raw.substring(5)) ?? maxZ; notifyListeners(); }
            }
          } catch (_) {}
        },
        onDone: () { if (!completer.isCompleted) completer.complete(false); _handleDisconnect(); },
        onError: (_) { if (!completer.isCompleted) completer.complete(false); _handleDisconnect(); },
      );
      await completer.future.timeout(const Duration(seconds: 2));
    } catch (_) {
      _channel?.sink.close(); _channel = null;
    }
  }

  void _parseGrblStatus(String raw) {
    // ── NEW: Extract Machine State (Idle, Run, Alarm, etc.) ──
    if (raw.startsWith("<")) {
      int firstPipe = raw.indexOf("|");
      if (firstPipe > 1) {
        machineState = raw.substring(1, firstPipe);
      }
    }

    int posStart = raw.indexOf("MPos:");
    if (posStart == -1) posStart = raw.indexOf("WPos:");
    
    if (posStart != -1) {
      posStart += 5;
      int posEnd = raw.indexOf("|", posStart);
      if (posEnd == -1) posEnd = raw.indexOf(">", posStart);
      
      if (posEnd != -1) {
        String posStr = raw.substring(posStart, posEnd);
        List<String> parts = posStr.split(",");
        if (parts.length >= 3) {
          x = double.tryParse(parts[0]) ?? x;
          y = double.tryParse(parts[1]) ?? y;
          z = double.tryParse(parts[2]) ?? z;
          notifyListeners(); 
        }
      }
    }
  }

  Future<void> connectAndVerifyHost(String host) => _connectAndVerify(host);

  void _handleDisconnect() {
    isConnected = false; _channel = null; cameraFrame.value = null; 
    _addLog("SYS: Offline."); notifyListeners(); 
  }

  void sendCommand(String cmd) {
    if (_channel != null && isConnected) { _channel!.sink.add(cmd); _addLog("TX: $cmd"); }
  }

  void updatePos(String axis, double val, String gcode) {
    if (axis == 'x') x = val; if (axis == 'y') y = val; if (axis == 'z') z = val;
    sendCommand(gcode); notifyListeners(); 
  }

  void addLog(String m) => _addLog(m);
  void _addLog(String m) { logs.add(m); if (logs.length > 50) logs.removeAt(0); notifyListeners(); }
  void clearLogs() { logs.clear(); notifyListeners(); }
}

Future<void> sweepMobileSubnets(ESP32Service service) async {
  await Future.delayed(const Duration(seconds: 2));
}