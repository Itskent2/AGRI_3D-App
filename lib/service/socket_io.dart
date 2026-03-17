import 'dart:io';
import 'farmbot_service.dart';

Future<void> discoverViaMobile(FarmbotService service) async {
  service.addLog("SYS: Scanning for FarmBot...");

  // Scan phone's own subnet
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
          service.addLog("SYS: Scanning $subnet.0/24...");

          final futures = List.generate(254, (i) async {
            if (service.isConnected) return;
            final ip = "$subnet.${i + 1}";
            try {
              final socket = await Socket.connect(
                ip, 80,
                timeout: const Duration(milliseconds: 300),
              );
              socket.destroy();
              service.addLog("SYS: Found → $ip");
              await service.connectAndVerifyHost(ip);
            } catch (_) {}
          });

          await Future.wait(futures);
        }
      }
    }
  } catch (e) {
    service.addLog("SYS: Scan error: $e");
  }

  if (!service.isConnected) {
    service.addLog("SYS: FarmBot not found.");
  }
}