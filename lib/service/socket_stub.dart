import 'farmbot_service.dart';

Future<void> discoverViaMobile(FarmbotService service) async {
  // Web doesn't support UDP
  service.addLog("SYS: UDP not supported on web.");
}