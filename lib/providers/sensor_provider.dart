import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../service/ESP32/esp32_sensors.dart';

final sensorProvider = Provider<ESP32Sensors>((ref) {
  return ESP32Sensors.instance;
});
