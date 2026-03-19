import 'package:flutter/foundation.dart';
import 'esp32_service.dart';

class ESP32GCodeControl extends ChangeNotifier {
  static final ESP32GCodeControl instance = ESP32GCodeControl();

  double currentX = 0;
  double currentY = 0;
  double currentZ = 0;

  /// TODO: Build the proper G-Code string and send it via ESP32Service
  void moveToPosition(double x, double y, double z) {
    // Example: String gcode = "G0 X$x Y$y Z$z";
    // ESP32Service.instance.sendCommand(gcode);
  }

  /// TODO: Handle homing sequence
  void homeAllAxes() {
    ESP32Service.instance.sendCommand("G28");
  }
}