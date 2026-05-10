import 'package:flutter/foundation.dart';

class ESP32Sensors extends ChangeNotifier {
  static final ESP32Sensors instance = ESP32Sensors();

  // Analog/Physical sensors
  double temperature = 0.0;
  double humidity = 0.0;
  double soilMoisture = 0.0;

  // Telemetry sensors
  int gatingFeatureGF = 1; // 1 = Safe, 0 = Gated
  int rainPhysicalRPhys = 0; // 0 = Dry, 1 = Rain
  double farmLat = 0.0;
  double farmLon = 0.0;
  int currentResIdx = 8;
  int espFreeHeap = 0;

  bool isRaining() => gatingFeatureGF == 0 || rainPhysicalRPhys == 1;

  /// Parses incoming JSON from ESP32Service into these variables
  void updateSensorsFromJson(Map<String, dynamic> jsonData) {
    if (jsonData.containsKey('temp')) {
      temperature = (jsonData['temp'] as num).toDouble();
    }
    if (jsonData.containsKey('hum')) {
      humidity = (jsonData['hum'] as num).toDouble();
    }
    if (jsonData.containsKey('moisture')) {
      soilMoisture = (jsonData['moisture'] as num).toDouble();
    }

    // Telemetry parsing
    if (jsonData.containsKey('G_f')) {
      gatingFeatureGF = (jsonData['G_f'] as num).toInt();
    }
    if (jsonData.containsKey('R_phys')) {
      rainPhysicalRPhys = (jsonData['R_phys'] as num).toInt();
    }
    if (jsonData.containsKey('lat')) {
      farmLat = (jsonData['lat'] as num).toDouble();
    }
    if (jsonData.containsKey('lon')) {
      farmLon = (jsonData['lon'] as num).toDouble();
    }
    if (jsonData.containsKey('res_idx')) {
      currentResIdx = (jsonData['res_idx'] as num).toInt();
    }
    if (jsonData.containsKey('free_heap')) {
      espFreeHeap = (jsonData['free_heap'] as num).toInt();
    }

    notifyListeners(); // Tells UI to update the numbers
  }
}