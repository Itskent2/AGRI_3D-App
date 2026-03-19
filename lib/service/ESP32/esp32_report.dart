import 'package:flutter/foundation.dart';

class ESP32Report extends ChangeNotifier {
  static final ESP32Report instance = ESP32Report();

  List<String> logs = [];

  /// Replaces the old _addLog function
  void addLog(String message) {
    logs.add(message);
    if (logs.length > 50) {
      logs.removeAt(0); // Keep memory usage light
    }
    notifyListeners(); // Updates the log_panel.dart UI
  }

  void clearLogs() {
    logs.clear();
    notifyListeners();
  }
}