import 'package:flutter/foundation.dart';

class ESP32Sensors extends ChangeNotifier {
  static final ESP32Sensors instance = ESP32Sensors();

  double temperature = 0.0;
  double humidity = 0.0;
  double soilMoisture = 0.0;

  /// TODO: Parse incoming JSON from ESP32Service into these variables
  void updateSensorsFromJson(Map<String, dynamic> jsonData) {
    // temperature = jsonData['temp'] ?? temperature;
    // humidity = jsonData['hum'] ?? humidity;
    notifyListeners(); // Tells Clark's UI to update the numbers
  }
}