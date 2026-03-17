import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

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
    _addLog("SYS: Connecting...");
    notifyListeners();

    // Try farmbot.local first, then fallback to IP
    final hosts = [
      "farmbot.local",
      "192.168.214.54",  // fallback IP
    ];

    for (final host in hosts) {
      if (isConnected) break;
      await _connectAndVerify(host);
    }

    if (!isConnected) {
      _addLog("SYS: FarmBot not found.");
    }

    isScanning = false;
    notifyListeners();
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

      // Wait for FARMBOT_ID identity
      final msg = await channel.stream.first
          .timeout(const Duration(seconds: 3));

      if (!msg.toString().startsWith("FARMBOT_ID:")) {
        channel.sink.close();
        _addLog("SYS: Not a FarmBot.");
        return;
      }

      _channel = channel;
      isConnected = true;
      _addLog("SYS: Online → $host");
      notifyListeners();

      _channel!.stream.listen(
        (msg) {
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

  void _addLog(String m) {
    logs.add(m);
    if (logs.length > 50) logs.removeAt(0);
    notifyListeners();
  }
}


