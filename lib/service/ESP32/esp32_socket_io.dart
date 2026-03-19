import 'dart:io';
import 'esp32_service.dart';
import 'esp32_report.dart';

Future<void> discoverViaMobile(ESP32Service service) async {
  ESP32Report.instance.addLog("SYS: Scanning for ESP32...");

  try {
    final interfaces = await NetworkInterface.list(
      includeLoopback: false,
      type: InternetAddressType.IPv4,
    );

    for (var interface in interfaces) {
      for (var addr in interface.addresses) {
        if (!addr.address.startsWith("127")) {
          final parts = addr.address.split(".");
          final subnet = "${parts[0]}.${parts[1]}.${parts[2]}";
          ESP32Report.instance.addLog("SYS: Scanning $subnet.0/24...");

          final futures = List.generate(254, (i) async {
            if (service.isConnected) return;
            final ip = "$subnet.${i + 1}";
            try {
              final socket = await Socket.connect(
                ip, 80,
                timeout: const Duration(milliseconds: 300),
              );
              socket.destroy();
              ESP32Report.instance.addLog("SYS: Found → $ip");
              await service.connectAndVerifyHost(ip);
            } catch (_) {}
          });

          await Future.wait(futures);
        }
      }
    }
  } catch (e) {
    ESP32Report.instance.addLog("SYS: Scan error: $e");
  }

  if (!service.isConnected) {
    ESP32Report.instance.addLog("SYS: ESP32 not found.");
  }
}