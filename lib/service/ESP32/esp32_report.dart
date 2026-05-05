import 'package:farmbot_app/service/ESP32/esp32_service.dart';
import 'package:flutter/foundation.dart';

class ESP32Report extends ChangeNotifier {
  static final ESP32Report instance = ESP32Report();

  List<String> logs = [];

  /// Replaces the old _addLog function
  void addLog(String message) {
    // Forward to the main service logger
    String tag = "SYSTEM";
    if (message.startsWith("SYS: ")) {
      message = message.substring(5);
    }

    ESP32Service.instance.addLog(message, tag: tag);

    // Also keep local for any legacy widgets
    logs.add(message);
    if (logs.length > 50) logs.removeAt(0);
    notifyListeners();
  }

  void clearLogs() {
    logs.clear();
    notifyListeners();
  }
}
