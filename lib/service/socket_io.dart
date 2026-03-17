import 'dart:io';
import 'farmbot_service.dart';

Future<void> discoverViaMobile(FarmbotService service) async {
  try {
    final udpSocket = await RawDatagramSocket.bind(
      InternetAddress.anyIPv4,
      4210,
    );

    service.addLog("SYS: Waiting for FarmBot broadcast...");

    await for (final event in udpSocket.timeout(
      const Duration(seconds: 15),
    )) {
      if (service.isConnected) break;
      if (event == RawSocketEvent.read) {
        final datagram = udpSocket.receive();
        if (datagram != null) {
          final msg = String.fromCharCodes(datagram.data);
          if (msg.startsWith("FARMBOT_HERE:")) {
            final ip = msg.split(":")[1];
            service.addLog("SYS: Found FarmBot at $ip");
            udpSocket.close();
            service.connectAndVerifyHost(ip);
            break;
          }
        }
      }
    }
  } catch (_) {
    service.addLog("SYS: Discovery timeout.");
  }
}