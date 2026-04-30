import 'esp32_service.dart';
import 'esp32_report.dart';

// Renamed from discoverViaMobile. 
// Note: You can eventually replace the hardcoded IP here with an actual 
// UDP listener or Subnet Pinger when you start building the Android APK!
Future<void> sweepMobileSubnets(ESP32Service service) async {
  ESP32Report.instance.addLog("SYS: Mobile mode — sweeping subnets...");
  
  // Updated to the current phone hotspot IP!
  await service.connectAndVerifyHost('10.231.200.181');
}