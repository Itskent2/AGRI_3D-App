import 'package:flutter/foundation.dart';
import 'esp32_service.dart';

class ESP32GCodeControl extends ChangeNotifier {
  static final ESP32GCodeControl instance = ESP32GCodeControl();

  double currentX = 0;
  double currentY = 0;
  double currentZ = 0;

  /// Builds the proper G-Code string and sends it via ESP32Service
  void moveToPosition(double x, double y, double z) {
    // Standard GRBL fast-move command
    String gcode = "G0 X${x.toStringAsFixed(2)} Y${y.toStringAsFixed(2)} Z${z.toStringAsFixed(2)}";
    ESP32Service.instance.sendCommand(gcode);
  }

  /// Handle homing sequence using standard GRBL home command
  void homeAllAxes() {
    // $H is the standard GRBL homing cycle command
    ESP32Service.instance.sendCommand("\$H");
  }
}