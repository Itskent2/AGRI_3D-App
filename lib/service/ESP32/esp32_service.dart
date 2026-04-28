import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'esp32_sensors.dart';

import 'esp32_socket_stub.dart' if (dart.library.io) 'esp32_socket_io.dart';

class ESP32Service extends ChangeNotifier {
  WebSocketChannel? _channel;
  bool isConnected = false;
  bool isScanning = false;
  String? _lastKnownIP;
  String? get currentIP => _lastKnownIP;
  List<String> logs = [];

  // Binary frame listener for the Live Feed
  final ValueNotifier<Uint8List?> cameraFrame = ValueNotifier(null);

  double x = 0, y = 0, z = 0;

  static final ESP32Service instance = ESP32Service();

  Future<void> autoDiscover() async {
    if (isConnected || isScanning) return;
    isScanning = true;
    notifyListeners();

    // Load the saved IP from storage before scanning
    final prefs = await SharedPreferences.getInstance();
    _lastKnownIP = prefs.getString('lastKnownIP');

    while (!isConnected) {
      _addLog("SYS: Searching for ESP32...");

      // Try last known IP first (fast)
      if (_lastKnownIP != null) {
        _addLog("SYS: Trying last known → $_lastKnownIP");
        await _connectAndVerify(_lastKnownIP!);
      }

      // 2. Try mDNS Name natively
      if (!isConnected) {
        _addLog("SYS: Trying mDNS → farmbot.local");
        await _connectAndVerify("farmbot.local");
      }

      // 3. Try Default Hotspot IP
      if (!isConnected) {
        _addLog("SYS: Trying Hotspot → 192.168.4.1");
        await _connectAndVerify("192.168.4.1");
      }

      // 4. Try legacy subnet sweep if still not connected
      if (!isConnected && !kIsWeb) {
        _addLog("SYS: Sweeping IP subnets...");
        await _discoverMobile();
      }

      if (!isConnected) {
        _addLog("SYS: Retrying in 3s...");
        await Future.delayed(const Duration(seconds: 3));
      }
    }

    isScanning = false;
    notifyListeners();
  }

  Future<void> _discoverMobile() async {
    // Assuming discoverViaMobile is defined elsewhere and uses connectAndVerifyHost
    await discoverViaMobile(this);
  }

  Future<void> _connectAndVerify(String host) async {
    if (isConnected) return;

    // --- NEW: Kill any ghost connections before opening a new one ---
    if (_channel != null) {
      _channel!.sink.close();
      _channel = null;
    }
    // --------------------------------------------------------------

    try {
      _addLog("SYS: Trying $host...");

      // Standard WebSocket mapping: Connect exactly to port 80
      final url = "ws://$host:80/";
      final channel = WebSocketChannel.connect(Uri.parse(url));
      await channel.ready.timeout(const Duration(seconds: 3));

      final completer = Completer<bool>();
      bool identified = false;
      _channel = channel;

      _channel!.stream.listen(
        (msg) async {
          // 1. Binary frames are pure JPEG images from the camera
          if (msg is List<int>) {
            // Covers both Uint8List and generic List<int>
            if (identified) {
              int size = msg.length;
              cameraFrame.value = msg is Uint8List
                  ? msg
                  : Uint8List.fromList(msg);

              // Lightly log frame arrival to terminal every ~50 frames to confirm receipt
              if (DateTime.now().millisecond < 20) {
                _addLog("SYS: [Video] Rx Frame $size bytes");
              }
            }
            return;
          }

          // 2. String messages are commands/logs
          String textMsg = msg.toString();

          if (!identified) {
            if (textMsg.startsWith("FARMBOT_ID:")) {
              identified = true;
              isConnected = true;

              // Save the IP to storage on successful connection
              _lastKnownIP = host;
              final prefs = await SharedPreferences.getInstance();
              await prefs.setString('lastKnownIP', host);

              _addLog("SYS: Online → $host");
              if (!completer.isCompleted) completer.complete(true);
            } else {
              _channel?.sink.close();
              _channel = null;
              if (!completer.isCompleted) completer.complete(false);
            }
            return;
          }
          
          // Try to decode as JSON first (Telemetry)
          try {
            final decoded = jsonDecode(textMsg);
            if (decoded is Map<String, dynamic>) {
              ESP32Sensors.instance.updateSensorsFromJson(decoded);
              // Only log json if it's not the frequent telemetry to avoid spam
              if (decoded['type'] != 'telemetry') {
                _addLog("RX JSON: $textMsg");
              }
              return;
            }
          } catch (e) {
            // Not a JSON string, just a regular string message
          }

          _addLog("RX: $msg");
        },
        onDone: () {
          if (!completer.isCompleted) completer.complete(false);
          _handleDisconnect();
        },
        onError: (_) {
          if (!completer.isCompleted) completer.complete(false);
          _handleDisconnect();
        },
      );

      // Await confirmation payload before returning to scanner loop!
      await completer.future.timeout(const Duration(seconds: 2));
    } catch (_) {
      _addLog("SYS: $host unreachable.");
      _channel?.sink.close();
      _channel = null;
    }
  }

  Future<void> connectAndVerifyHost(String host) => _connectAndVerify(host);

  void _handleDisconnect() {
    isConnected = false;
    _channel = null;
    cameraFrame.value = null; // Clear frame on disconnect
    _addLog("SYS: Offline.");
  }

  void sendCommand(String cmd) {
    if (_channel != null && isConnected) {
      _channel!.sink.add(cmd);
      _addLog("TX: $cmd");
    }
  }

  void updatePos(String axis, double val, String gcode) {
    if (axis == 'x') x = val;
    if (axis == 'y') y = val;
    if (axis == 'z') z = val;
    sendCommand(gcode);
  }

  void addLog(String m) => _addLog(m);

  void _addLog(String m) {
    logs.add(m);
    if (logs.length > 50) logs.removeAt(0);
    notifyListeners();
  }
}
