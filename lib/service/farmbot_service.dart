import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

// Only import dart:io on non-web platforms
import 'socket_stub.dart'
    if (dart.library.io) 'socket_io.dart';

class FarmbotService extends ChangeNotifier {
  WebSocketChannel? _channel;
  bool isConnected = false;
  bool isScanning = false;
  List<String> logs = [];

  double x = 0, y = 0, z = 0;

  static final FarmbotService instance = FarmbotService();

  Future<void> autoDiscover() async {
    if (isConnected || isScanning) return;
    isScanning = true;
    _addLog("SYS: Searching for FarmBot...");
    notifyListeners();

    if (kIsWeb) {
      await _discoverWeb();
    } else {
      await _discoverMobile();
    }

    if (!isConnected) {
      _addLog("SYS: FarmBot not found.");
    }

    isScanning = false;
    notifyListeners();
  }

  // --- WEB: try farmbot.local only ---
  Future<void> _discoverWeb() async {
    await _connectAndVerify("farmbot.local");
  }

  // --- MOBILE: UDP broadcast discovery ---
  Future<void> _discoverMobile() async {
    await discoverViaMobile(this);
  }

  Future<void> _connectAndVerify(String host) async {
    if (isConnected) return;
    try {
      _addLog("SYS: Trying $host...");
      notifyListeners();

      final url = "ws://$host/ws";
      final channel = WebSocketChannel.connect(Uri.parse(url));
      await channel.ready.timeout(const Duration(seconds: 3));

      if (isConnected) {
        channel.sink.close();
        return;
      }

      bool identified = false;
      _channel = channel;

      _channel!.stream.listen(
        (msg) {
          if (!identified) {
            if (msg.toString().startsWith("FARMBOT_ID:")) {
              identified = true;
              isConnected = true;
              _addLog("SYS: Online → $host");
              notifyListeners();
            } else {
              _channel!.sink.close();
              _channel = null;
            }
            return;
          }
          _addLog("RX: $msg");
          notifyListeners();
        },
        onDone: () => _handleDisconnect(),
        onError: (_) => _handleDisconnect(),
      );
    } catch (_) {
      _addLog("SYS: $host unreachable.");
    }
  }

  void connectAndVerifyHost(String host) => _connectAndVerify(host);

  void _handleDisconnect() {
    isConnected = false;
    _channel = null;
    _addLog("SYS: Offline.");
    notifyListeners();
  }

  void sendCommand(String cmd) {
    if (_channel != null && isConnected) {
      _channel!.sink.add(cmd);
      _addLog("TX: $cmd");
      notifyListeners();
    }
  }

  void updatePos(String axis, double val, String gcode) {
    if (axis == 'x') x = val;
    if (axis == 'y') y = val;
    if (axis == 'z') z = val;
    sendCommand(gcode);
    notifyListeners();
  }

  void addLog(String m) => _addLog(m);

  void _addLog(String m) {
    logs.add(m);
    if (logs.length > 50) logs.removeAt(0);
    notifyListeners();
  }
}