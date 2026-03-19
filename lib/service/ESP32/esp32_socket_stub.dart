import 'esp32_service.dart';
import 'esp32_report.dart';

Future<void> discoverViaMobile(ESP32Service service) async {
  ESP32Report.instance.addLog("SYS: Subnet scan not supported on web.");
}