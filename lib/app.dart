import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

// Providers
import 'providers/theme_provider.dart';

// Screens
import 'screens/monitoring_dashboard.dart';
import 'screens/control_panel.dart';
import 'screens/log_panel.dart';
import 'screens/plot_map.dart';
import 'screens/weather_forecast.dart';
import 'screens/settings.dart';

// Widgets
import 'widgets/side_bar.dart';

class FarmBotApp extends ConsumerWidget {
  const FarmBotApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return const AppInner();
  }
}

class AppInner extends ConsumerStatefulWidget {
  const AppInner({super.key});

  @override
  ConsumerState<AppInner> createState() => _AppInnerState();
}

class _AppInnerState extends ConsumerState<AppInner> {
  String _activeTab = 'dashboard';
  final GlobalKey<ScaffoldState> _scaffoldKey = GlobalKey<ScaffoldState>();

  String _tabDisplayName(String tab) {
    const names = <String, String>{
      'dashboard': 'DASHBOARD',
      'controls': 'CONTROL PANEL',
      'monitoring': 'MONITORING',
      'map': 'PLOT MAP',
      'weather': 'WEATHER FORECAST',
      'logs': 'ACTIVITY LOGS',
      'settings': 'SETTINGS',
    };
    return names[tab] ?? tab.toUpperCase();
  }

  Widget _buildContent(Color accent) {
    switch (_activeTab) {
      case 'dashboard':
        return _DashboardLayout(accent: accent);
      case 'controls':
        return const ControlPanel();
      case 'monitoring':
        return const MonitoringDashboardScreen();
      case 'map':
        return const PlotMapScreen();
      case 'weather':
        return const WeatherForecast();
      case 'logs':
        return const LogPanel(logs: []);
      case 'settings':
        return const SettingsScreen();
      default:
        return _DashboardLayout(accent: accent);
    }
  }

  @override
  Widget build(BuildContext context) {
    final themeState = ref.watch(themeProvider);
    final isDark = themeState.isDark(context);
    final accent = themeState.currentAccentColor;
    final bgColor = isDark ? const Color(0xFF0B0F1A) : const Color(0xFFF1F5F9);

    return Scaffold(
      key: _scaffoldKey,
      backgroundColor: bgColor,
      body: Row(
        children: [
          if (MediaQuery.of(context).size.width >= 1024)
            SizedBox(
              width: 256,
              child: Sidebar(
                activeTab: _activeTab,
                setActiveTab: (tab) => setState(() => _activeTab = tab),
              ),
            ),
          Expanded(
            child: Column(
              children: [
                _TopHeader(
                  tabLabel: _tabDisplayName(_activeTab),
                  accent: accent,
                  onMenuTap: () => _scaffoldKey.currentState?.openDrawer(),
                ),
                Expanded(child: _buildContent(accent)),
              ],
            ),
          ),
        ],
      ),
      drawer: MediaQuery.of(context).size.width < 1024
          ? Drawer(
              child: Sidebar(
                activeTab: _activeTab,
                setActiveTab: (tab) {
                  setState(() => _activeTab = tab);
                  Navigator.of(context).pop();
                },
              ),
            )
          : null,
    );
  }
}

class _TopHeader extends StatelessWidget {
  const _TopHeader({required this.tabLabel, required this.accent, required this.onMenuTap});
  final String tabLabel;
  final Color accent;
  final VoidCallback onMenuTap;

  @override
  Widget build(BuildContext context) {
    final isDark = Theme.of(context).brightness == Brightness.dark;
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 16, 16, 0),
      child: Container(
        height: 64,
        decoration: BoxDecoration(
          color: isDark ? const Color(0xFF1F2937) : Colors.white,
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: isDark ? const Color(0xFF374151) : Colors.grey.shade300),
        ),
        padding: const EdgeInsets.symmetric(horizontal: 16),
        child: Row(
          children: [
            if (MediaQuery.of(context).size.width < 1024)
              IconButton(icon: Icon(Icons.menu, color: isDark ? const Color(0xFF9CA3AF) : Colors.black54), onPressed: onMenuTap),
            Column(
              mainAxisAlignment: MainAxisAlignment.center,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text('CURRENT VIEW', style: TextStyle(fontSize: 8, color: Color(0xFF6B7280), fontWeight: FontWeight.bold, letterSpacing: 2)),
                Text(tabLabel, style: TextStyle(fontSize: 12, fontFamily: 'monospace', color: accent, fontWeight: FontWeight.bold)),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _DashboardLayout extends StatelessWidget {
  const _DashboardLayout({required this.accent});
  final Color accent;

  @override
  Widget build(BuildContext context) {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        children: [
          _LiveCamHero(accent: accent),
          const SizedBox(height: 16),
          const MonitoringDashboardScreen(),
        ],
      ),
    );
  }
}

class _LiveCamHero extends StatelessWidget {
  const _LiveCamHero({required this.accent});
  final Color accent;

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(16),
      child: SizedBox(
        height: 256,
        child: Stack(
          fit: StackFit.expand,
          children: [
            Image.asset(
              'assets/image.png',
              fit: BoxFit.cover,
              errorBuilder: (_, __, ___) => Container(
                color: const Color(0xFF1F2937),
                child: const Icon(Icons.image_not_supported, color: Color(0xFF4B5563), size: 48),
              ),
            ),
            DecoratedBox(
              decoration: BoxDecoration(
                gradient: LinearGradient(
                  begin: Alignment.topCenter,
                  end: Alignment.bottomCenter,
                  colors: [Colors.transparent, const Color(0xFF030712).withValues(alpha: 0.4), const Color(0xFF030712)],
                  stops: const [0.0, 0.4, 1.0],
                ),
              ),
            ),
            Padding(
              padding: const EdgeInsets.all(32),
              child: Column(
                mainAxisAlignment: MainAxisAlignment.end,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('CEBU TECHNOLOGICAL UNIVERSITY',
                    style: TextStyle(color: Colors.white.withValues(alpha: 0.7), fontSize: 11, fontFamily: 'monospace', letterSpacing: 1.2)),
                  const SizedBox(height: 8),
                  RichText(
                    text: TextSpan(
                      style: const TextStyle(fontSize: 28, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: Colors.white),
                      children: [
                        const TextSpan(text: 'AGRI 3D '),
                        TextSpan(text: 'Gantry System', style: TextStyle(color: accent)),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
