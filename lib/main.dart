import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter/foundation.dart' show kIsWeb;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:permission_handler/permission_handler.dart';

import 'package:farmbot_app/app.dart';
import 'package:farmbot_app/screens/loading_screen.dart';
import 'package:farmbot_app/providers/theme_provider.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  if (!kIsWeb) {
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
    SystemChrome.setSystemUIOverlayStyle(const SystemUiOverlayStyle(
      statusBarColor: Colors.transparent,
      systemNavigationBarColor: Color(0xFF19222B),
      systemNavigationBarIconBrightness: Brightness.light,
    ));
    await _requestGantryPermissions();
  }

  runApp(const ProviderScope(child: MainApp()));
}

Future<void> _requestGantryPermissions() async {
  await [
    Permission.location,
    Permission.bluetooth,
    Permission.bluetoothScan,
    Permission.bluetoothConnect,
    Permission.nearbyWifiDevices,
    Permission.storage,               // Android ≤ 12: read/write external storage
    Permission.manageExternalStorage, // Android 13+: all-files access (for Downloads)
  ].request();
}

class MainApp extends ConsumerWidget {
  const MainApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final themeState = ref.watch(themeProvider);
    final accentColor = themeState.currentAccentColor;

    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'Agri 3D',
      themeMode: themeState.themeMode, // Controlled by Settings Screen
      theme: ThemeData(
        brightness: Brightness.light,
        scaffoldBackgroundColor: const Color(0xFFF1F5F9),
        colorScheme: ColorScheme.light(primary: accentColor, surface: Colors.white),
      ),
      darkTheme: ThemeData(
        brightness: Brightness.dark,
        scaffoldBackgroundColor: const Color(0xFF0B0F1A),
        colorScheme: ColorScheme.dark(primary: accentColor, surface: const Color(0xFF1F2937)),
      ),
      initialRoute: '/',
      routes: {
        '/': (context) => const AgriLoadingScreen(),
        '/home': (context) => const FarmBotApp(),
      },
    );
  }
}