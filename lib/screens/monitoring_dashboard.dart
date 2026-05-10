// lib/screens/monitoring_dashboard.dart

import 'dart:ui';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:fl_chart/fl_chart.dart';
import '../providers/theme_provider.dart';
import '../providers/sensor_provider.dart';
import '../widgets/sensor_heatmaps.dart';
import '../service/ESP32/esp32_service.dart';
import 'dart:math' as math;

// ─────────────────────────────────────────────────────────────
// Telemetry data model
// ─────────────────────────────────────────────────────────────

class _TelemetryPoint {
  final String time;
  final double temp;
  final double humidity;
  final double moisture;
  const _TelemetryPoint(this.time, this.temp, this.humidity, this.moisture);
}

const _telemetry = [
  _TelemetryPoint('08:00', 24, 65, 45),
  _TelemetryPoint('09:00', 25, 63, 44),
  _TelemetryPoint('10:00', 27, 60, 42),
  _TelemetryPoint('11:00', 28, 58, 41),
  _TelemetryPoint('12:00', 30, 55, 39),
  _TelemetryPoint('13:00', 31, 53, 38),
  _TelemetryPoint('14:00', 30, 54, 37),
];

// ─────────────────────────────────────────────────────────────
// Screen
// ─────────────────────────────────────────────────────────────

class MonitoringDashboardScreen extends ConsumerWidget {
  const MonitoringDashboardScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final themeState = ref.watch(themeProvider);
    final sensors = ref.watch(sensorProvider);
    final accent = themeState.currentAccentColor;
    final isDark = themeState.isDark(context);
    final isWide = MediaQuery.of(context).size.width >= 900;

    final bgColor = isDark ? const Color(0xFF0B0F1A) : const Color(0xFFF1F5F9);
    final textColor = isDark ? Colors.white : const Color(0xFF111827);
    final subTextColor = isDark
        ? const Color(0xFF9CA3AF)
        : const Color(0xFF4B5563);
    final borderColor = isDark
        ? Colors.white10
        : Colors.grey.withValues(alpha: 0.2);
    final glassColor = isDark
        ? Colors.white.withValues(alpha: 0.05)
        : Colors.white.withValues(alpha: 0.7);

    return TweenAnimationBuilder<double>(
      duration: const Duration(milliseconds: 800),
      curve: Curves.easeOutBack,
      tween: Tween(begin: 0.0, end: 1.0),
      builder: (context, value, child) {
        return Opacity(
          opacity: value.clamp(
            0.0,
            1.0,
          ), // ✅ FIX 1: Clamp prevents opacity crash from easeOutBack overshoot
          child: Transform.scale(
            scale: 0.95 + (0.05 * value),
            child: Transform.translate(
              offset: Offset(0, 30 * (1 - value)),
              child: child,
            ),
          ),
        );
      },
      child: Container(
        color: bgColor,
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _WeatherBanner(
                accent: accent,
                isDark: isDark,
                glassColor: glassColor,
                borderColor: borderColor,
                textColor: textColor,
              ),
              const SizedBox(height: 16),

              // ── Section Header ──
              Padding(
                padding: const EdgeInsets.only(bottom: 10),
                child: Row(
                  children: [
                    Container(
                      width: 3,
                      height: 14,
                      decoration: BoxDecoration(
                        color: accent,
                        borderRadius: BorderRadius.circular(2),
                        boxShadow: [BoxShadow(color: accent.withValues(alpha: 0.6), blurRadius: 6)],
                      ),
                    ),
                    const SizedBox(width: 8),
                    Text(
                      'SOIL SENSORS',
                      style: TextStyle(
                        fontSize: 10,
                        fontWeight: FontWeight.w900,
                        color: textColor,
                        letterSpacing: 2.5,
                      ),
                    ),
                    const Spacer(),
                    Text(
                      'LIVE',
                      style: TextStyle(
                        fontSize: 8,
                        fontWeight: FontWeight.bold,
                        color: const Color(0xFF22C55E),
                        letterSpacing: 2,
                      ),
                    ),
                    const SizedBox(width: 4),
                    Container(
                      width: 5, height: 5,
                      decoration: const BoxDecoration(
                        shape: BoxShape.circle,
                        color: Color(0xFF22C55E),
                      ),
                    ),
                  ],
                ),
              ),

              // ── Dynamic Sensor Status Grid ──
              ListenableBuilder(
                listenable: sensors,
                builder: (context, _) {
                  final cardWidth = isWide
                      ? (MediaQuery.of(context).size.width - 96) / 4
                      : (MediaQuery.of(context).size.width - 64) / 2;
                  return Wrap(
                    spacing: 12,
                    runSpacing: 12,
                    children: [
                      SizedBox(
                        width: cardWidth,
                        child: _StatusCard(
                          icon: Icons.water_drop,
                          label: 'Soil Moisture',
                          value: sensors.hasMoistureData ? '${sensors.soilMoisture.toStringAsFixed(0)}%' : '--',
                          color: const Color(0xFF3B82F6),
                          trend: !sensors.hasMoistureData ? 'Awaiting data' : (sensors.soilMoisture < 30 ? '⚠ Below threshold' : 'Optimal range'),
                          alert: sensors.hasMoistureData && sensors.soilMoisture < 30,
                          percent: sensors.hasMoistureData ? (sensors.soilMoisture / 100).clamp(0.0, 1.0) : 0.0,
                          isDark: isDark,
                          glassColor: glassColor,
                          borderColor: borderColor,
                          textColor: textColor,
                          subTextColor: subTextColor,
                        ),
                      ),
                      SizedBox(
                        width: cardWidth,
                        child: _StatusCard(
                          icon: Icons.grass,
                          label: 'Nitrogen (N)',
                          value: sensors.hasNpkData ? '${sensors.nitrogen.toStringAsFixed(0)} mg/kg' : '--',
                          color: const Color(0xFF22C55E),
                          trend: !sensors.hasNpkData ? 'Awaiting dip' : (sensors.nitrogen < 20 ? '⚠ Deficient' : 'Sufficient'),
                          alert: sensors.hasNpkData && sensors.nitrogen < 20,
                          percent: sensors.hasNpkData ? (sensors.nitrogen / 300).clamp(0.0, 1.0) : 0.0,
                          isDark: isDark,
                          glassColor: glassColor,
                          borderColor: borderColor,
                          textColor: textColor,
                          subTextColor: subTextColor,
                        ),
                      ),
                      SizedBox(
                        width: cardWidth,
                        child: _StatusCard(
                          icon: Icons.grain,
                          label: 'Phosphorus (P)',
                          value: sensors.hasNpkData ? '${sensors.phosphorus.toStringAsFixed(0)} mg/kg' : '--',
                          color: const Color(0xFFA855F7),
                          trend: !sensors.hasNpkData ? 'Awaiting dip' : (sensors.phosphorus < 20 ? '⚠ Deficient' : 'Sufficient'),
                          alert: sensors.hasNpkData && sensors.phosphorus < 20,
                          percent: sensors.hasNpkData ? (sensors.phosphorus / 200).clamp(0.0, 1.0) : 0.0,
                          isDark: isDark,
                          glassColor: glassColor,
                          borderColor: borderColor,
                          textColor: textColor,
                          subTextColor: subTextColor,
                        ),
                      ),
                      SizedBox(
                        width: cardWidth,
                        child: _StatusCard(
                          icon: Icons.science,
                          label: 'Potassium (K)',
                          value: sensors.hasNpkData ? '${sensors.potassium.toStringAsFixed(0)} mg/kg' : '--',
                          color: const Color(0xFFEAB308),
                          trend: !sensors.hasNpkData ? 'Awaiting dip' : (sensors.potassium < 20 ? '⚠ Deficient' : 'Sufficient'),
                          alert: sensors.hasNpkData && sensors.potassium < 20,
                          percent: sensors.hasNpkData ? (sensors.potassium / 400).clamp(0.0, 1.0) : 0.0,
                          isDark: isDark,
                          glassColor: glassColor,
                          borderColor: borderColor,
                          textColor: textColor,
                          subTextColor: subTextColor,
                        ),
                      ),
                      SizedBox(
                        width: cardWidth,
                        child: _StatusCard(
                          icon: Icons.bolt,
                          label: 'Conductivity (EC)',
                          value: sensors.hasNpkData ? '${sensors.ec.toStringAsFixed(0)} μS/cm' : '--',
                          color: const Color(0xFF06B6D4),
                          trend: !sensors.hasNpkData ? 'Awaiting dip' : (sensors.ec < 100 ? '⚠ Low' : 'Normal'),
                          alert: sensors.hasNpkData && sensors.ec < 100,
                          percent: sensors.hasNpkData ? (sensors.ec / 1000).clamp(0.0, 1.0) : 0.0,
                          isDark: isDark,
                          glassColor: glassColor,
                          borderColor: borderColor,
                          textColor: textColor,
                          subTextColor: subTextColor,
                        ),
                      ),
                      SizedBox(
                        width: cardWidth,
                        child: _StatusCard(
                          icon: Icons.opacity,
                          label: 'Soil pH',
                          value: sensors.hasNpkData ? sensors.ph.toStringAsFixed(1) : '--',
                          color: const Color(0xFFF97316),
                          trend: !sensors.hasNpkData ? 'Awaiting dip' : (sensors.ph < 6.0 ? 'Acidic' : (sensors.ph > 7.5 ? 'Alkaline' : 'Neutral')),
                          alert: sensors.hasNpkData && (sensors.ph < 5.0 || sensors.ph > 8.5),
                          percent: sensors.hasNpkData ? (sensors.ph / 14).clamp(0.0, 1.0) : 0.0,
                          isDark: isDark,
                          glassColor: glassColor,
                          borderColor: borderColor,
                          textColor: textColor,
                          subTextColor: subTextColor,
                        ),
                      ),
                    ],
                  );
                },
              ),

              const SizedBox(height: 16),
              if (isWide)
                IntrinsicHeight(
                  child: Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Expanded(
                        flex: 2,
                        child: _TelemetryChart(
                          accent: accent,
                          isDark: isDark,
                          glassColor: glassColor,
                          borderColor: borderColor,
                          textColor: textColor,
                          subTextColor: subTextColor,
                        ),
                      ),
                      const SizedBox(width: 16),
                      Expanded(
                        child: _DailyMonitoringPanel(
                          accent: accent,
                          isDark: isDark,
                          glassColor: glassColor,
                          borderColor: borderColor,
                          textColor: textColor,
                          subTextColor: subTextColor,
                        ),
                      ),
                    ],
                  ),
                )
              else
                Column(
                  children: [
                    _TelemetryChart(
                      accent: accent,
                      isDark: isDark,
                      glassColor: glassColor,
                      borderColor: borderColor,
                      textColor: textColor,
                      subTextColor: subTextColor,
                    ),
                    const SizedBox(height: 16),
                    _DailyMonitoringPanel(
                      accent: accent,
                      isDark: isDark,
                      glassColor: glassColor,
                      borderColor: borderColor,
                      textColor: textColor,
                      subTextColor: subTextColor,
                    ),
                  ],
                ),
              _ManualControlPanel(
                accent: accent,
                isDark: isDark,
                glassColor: glassColor,
                borderColor: borderColor,
                textColor: textColor,
                subTextColor: subTextColor,
              ),
              const SizedBox(height: 16),
              const SensorHeatmaps(),
            ],
          ),
        ),
      ),
    );
  }
}

// ─────────────────────────────────────────────────────────────
// Connection Status Banner (replaces weather banner)
// ─────────────────────────────────────────────────────────────

class _WeatherBanner extends StatefulWidget {
  const _WeatherBanner({
    required this.accent,
    required this.isDark,
    required this.glassColor,
    required this.borderColor,
    required this.textColor,
  });

  final Color accent, glassColor, borderColor, textColor;
  final bool isDark;

  @override
  State<_WeatherBanner> createState() => _WeatherBannerState();
}

class _WeatherBannerState extends State<_WeatherBanner>
    with SingleTickerProviderStateMixin {
  late AnimationController _pulseCtrl;
  late Animation<double> _pulseAnim;

  @override
  void initState() {
    super.initState();
    _pulseCtrl = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1400),
    )..repeat(reverse: true);
    _pulseAnim = Tween(begin: 0.4, end: 1.0).animate(
      CurvedAnimation(parent: _pulseCtrl, curve: Curves.easeInOut),
    );
  }

  @override
  void dispose() {
    _pulseCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final service = ESP32Service.instance;
    final connected = service.isConnected;
    final dotColor =
        connected ? const Color(0xFF22C55E) : const Color(0xFFEF4444);
    final statusText = connected ? 'SYSTEM ONLINE' : 'SEARCHING…';
    final subText = connected
        ? 'All sensors active · Real-time telemetry'
        : 'Waiting for ESP32 connection';

    return ClipRRect(
      borderRadius: BorderRadius.circular(16),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 12, sigmaY: 12),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 14),
          decoration: BoxDecoration(
            gradient: LinearGradient(
              colors: [
                widget.accent.withValues(alpha: 0.12),
                widget.glassColor,
              ],
              begin: Alignment.centerLeft,
              end: Alignment.centerRight,
            ),
            borderRadius: BorderRadius.circular(16),
            border: Border.all(color: widget.accent.withValues(alpha: 0.35)),
          ),
          child: Row(
            children: [
              // Pulsing dot
              AnimatedBuilder(
                animation: _pulseAnim,
                builder: (_, __) => Stack(
                  alignment: Alignment.center,
                  children: [
                    Container(
                      width: 20,
                      height: 20,
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        color:
                            dotColor.withValues(alpha: 0.25 * _pulseAnim.value),
                      ),
                    ),
                    Container(
                      width: 8,
                      height: 8,
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        color: dotColor,
                        boxShadow: [
                          BoxShadow(
                            color: dotColor.withValues(alpha: 0.8),
                            blurRadius: 8,
                            spreadRadius: 1,
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      statusText,
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w900,
                        color: widget.textColor,
                        letterSpacing: 2,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      subText,
                      style: TextStyle(
                        fontSize: 9,
                        color: widget.textColor.withValues(alpha: 0.5),
                        letterSpacing: 0.5,
                      ),
                    ),
                  ],
                ),
              ),
              // AGRI-3D chip
              Container(
                padding:
                    const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                decoration: BoxDecoration(
                  color: widget.accent.withValues(alpha: 0.15),
                  borderRadius: BorderRadius.circular(99),
                  border:
                      Border.all(color: widget.accent.withValues(alpha: 0.4)),
                ),
                child: Text(
                  'AGRI-3D',
                  style: TextStyle(
                    fontSize: 9,
                    fontWeight: FontWeight.w900,
                    color: widget.accent,
                    letterSpacing: 1.5,
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// ─────────────────────────────────────────────────────────────
// Enhanced Status Card with arc progress
// ─────────────────────────────────────────────────────────────

class _StatusCard extends StatelessWidget {
  const _StatusCard({
    required this.icon,
    required this.label,
    required this.value,
    required this.color,
    required this.trend,
    required this.alert,
    required this.isDark,
    required this.glassColor,
    required this.borderColor,
    required this.textColor,
    required this.subTextColor,
    this.percent = 0.0,
  });

  final IconData icon;
  final String label, value, trend;
  final Color color, glassColor, borderColor, textColor, subTextColor;
  final bool alert, isDark;
  final double percent; // 0.0 to 1.0 for arc

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(16),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 12, sigmaY: 12),
        child: Container(
          padding: const EdgeInsets.all(14),
          decoration: BoxDecoration(
            color: glassColor,
            borderRadius: BorderRadius.circular(16),
            border: Border.all(
              color: alert
                  ? const Color(0xFFEF4444).withValues(alpha: 0.6)
                  : color.withValues(alpha: 0.25),
              width: alert ? 1.5 : 1.0,
            ),
            boxShadow: [
              BoxShadow(
                color: color.withValues(alpha: 0.08),
                blurRadius: 16,
                offset: const Offset(0, 4),
              ),
            ],
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              // Top row: icon + alert dot
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Container(
                    padding: const EdgeInsets.all(8),
                    decoration: BoxDecoration(
                      color: color.withValues(alpha: 0.12),
                      borderRadius: BorderRadius.circular(10),
                    ),
                    child: Icon(icon, color: color, size: 18),
                  ),
                  if (alert)
                    Container(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 6, vertical: 2),
                      decoration: BoxDecoration(
                        color: const Color(0xFFEF4444).withValues(alpha: 0.15),
                        borderRadius: BorderRadius.circular(99),
                        border: Border.all(
                          color: const Color(0xFFEF4444).withValues(alpha: 0.4),
                        ),
                      ),
                      child: const Text(
                        '! LOW',
                        style: TextStyle(
                          fontSize: 7,
                          fontWeight: FontWeight.w900,
                          color: Color(0xFFEF4444),
                          letterSpacing: 1,
                        ),
                      ),
                    ),
                ],
              ),
              const SizedBox(height: 8),
              // Value
              Text(
                value,
                style: TextStyle(
                  fontSize: 24,
                  fontWeight: FontWeight.w900,
                  color: textColor,
                  fontStyle: FontStyle.italic,
                  letterSpacing: -0.5,
                  height: 1.0,
                ),
              ),
              // Label + unit
              Text(
                label.toUpperCase(),
                style: TextStyle(
                  fontSize: 8,
                  color: subTextColor,
                  fontFamily: 'monospace',
                  letterSpacing: 1.2,
                ),
              ),
              const SizedBox(height: 6),
              // Progress bar
              ClipRRect(
                borderRadius: BorderRadius.circular(99),
                child: LinearProgressIndicator(
                  value: percent.clamp(0.0, 1.0),
                  minHeight: 3,
                  backgroundColor: isDark
                      ? Colors.white.withValues(alpha: 0.07)
                      : Colors.grey.shade200,
                  valueColor: AlwaysStoppedAnimation<Color>(
                    alert ? const Color(0xFFEF4444) : color,
                  ),
                ),
              ),
              const SizedBox(height: 4),
              // Trend text
              Text(
                trend,
                style: TextStyle(
                  fontSize: 9,
                  fontWeight: FontWeight.bold,
                  color: alert
                      ? const Color(0xFFEF4444)
                      : color.withValues(alpha: 0.85),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// ─────────────────────────────────────────────────────────────
// Telemetry area chart
// ─────────────────────────────────────────────────────────────

class _TelemetryChart extends StatefulWidget {
  final Color accent, glassColor, borderColor, textColor, subTextColor;
  final bool isDark;

  const _TelemetryChart({
    required this.accent,
    required this.isDark,
    required this.glassColor,
    required this.borderColor,
    required this.textColor,
    required this.subTextColor,
  });

  @override
  State<_TelemetryChart> createState() => _TelemetryChartState();
}

class _TelemetryChartState extends State<_TelemetryChart>
    with SingleTickerProviderStateMixin {
  // ✅ FIX 2: Use AnimationController for left-to-right line draw effect
  late final AnimationController _controller;
  late final Animation<double> _progress;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1500),
    );
    _progress = CurvedAnimation(
      parent: _controller,
      curve: Curves.easeInOutQuart,
    );
    // Small delay so the screen entry animation finishes first
    Future.delayed(const Duration(milliseconds: 400), () {
      if (mounted) _controller.forward();
    });
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  List<FlSpot> _spots(double Function(_TelemetryPoint) fn) {
    return _telemetry.asMap().entries.map((e) {
      return FlSpot(e.key.toDouble(), fn(e.value));
    }).toList();
  }

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(12),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 10, sigmaY: 10),
        child: Container(
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: widget.glassColor,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: widget.borderColor),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _buildHeader(),
              const SizedBox(height: 20),
              // ✅ FIX 2: AnimatedBuilder drives the ClipRect widthFactor for L→R reveal
              AnimatedBuilder(
                animation: _progress,
                builder: (context, child) {
                  return Align(
                    alignment: Alignment.centerLeft,
                    child: ClipRect(
                      child: Align(
                        alignment: Alignment.centerLeft,
                        widthFactor: _progress.value,
                        child: child,
                      ),
                    ),
                  );
                },
                child: SizedBox(
                  height: 220,
                  child: LineChart(
                    LineChartData(
                      minY: 0,
                      maxY: 80,
                      lineTouchData: LineTouchData(
                        handleBuiltInTouches: true,
                        getTouchedSpotIndicator:
                            (LineChartBarData barData, List<int> spotIndexes) {
                              return spotIndexes.map((index) {
                                return TouchedSpotIndicatorData(
                                  FlLine(
                                    color: widget.accent.withValues(alpha: 0.5),
                                    strokeWidth: 2,
                                    dashArray: [4, 4],
                                  ),
                                  FlDotData(
                                    show: true,
                                    getDotPainter:
                                        (
                                          spot,
                                          percent,
                                          barData,
                                          index,
                                        ) => FlDotCirclePainter(
                                          radius: 5,
                                          color: barData.color ?? widget.accent,
                                          strokeWidth: 2,
                                          strokeColor: widget.isDark
                                              ? const Color(0xFF111827)
                                              : Colors.white,
                                        ),
                                  ),
                                );
                              }).toList();
                            },
                        touchTooltipData: LineTouchTooltipData(
                          getTooltipColor: (spot) => widget.isDark
                              ? const Color(0xFF1F2937).withValues(alpha: 0.95)
                              : Colors.white.withValues(alpha: 0.95),
                          tooltipRoundedRadius: 8,
                          tooltipPadding: const EdgeInsets.symmetric(
                            horizontal: 16,
                            vertical: 12,
                          ),
                          tooltipBorder: BorderSide(color: widget.borderColor),
                          fitInsideHorizontally: true,
                          fitInsideVertically: true,
                          getTooltipItems: (List<LineBarSpot> touchedSpots) {
                            if (touchedSpots.isEmpty) return [];

                            return touchedSpots.map((LineBarSpot spot) {
                              // Only render tooltip on the first bar to avoid duplicates
                              if (spot.barIndex != touchedSpots.first.barIndex)
                                return null;

                              final xIndex = spot.x.toInt();
                              if (xIndex < 0 || xIndex >= _telemetry.length)
                                return null;

                              final point = _telemetry[xIndex];

                              // ✅ FIX 3: Humidity shown FIRST, Avg Temp shown BELOW
                              return LineTooltipItem(
                                '${point.humidity.toInt()}%\n',
                                const TextStyle(
                                  color: Color(0xFF3B82F6),
                                  fontWeight: FontWeight.w900,
                                  fontSize: 16,
                                ),
                                children: [
                                  TextSpan(
                                    text: 'Avg Humidity\n\n',
                                    style: TextStyle(
                                      color: widget.subTextColor,
                                      fontSize: 10,
                                      fontWeight: FontWeight.normal,
                                    ),
                                  ),
                                  TextSpan(
                                    text: '${point.temp.toInt()}°C\n',
                                    style: TextStyle(
                                      color: widget.accent,
                                      fontWeight: FontWeight.w900,
                                      fontSize: 16,
                                    ),
                                  ),
                                  TextSpan(
                                    text: 'Avg Temp',
                                    style: TextStyle(
                                      color: widget.subTextColor,
                                      fontSize: 10,
                                      fontWeight: FontWeight.normal,
                                    ),
                                  ),
                                ],
                              );
                            }).toList();
                          },
                        ),
                      ),
                      gridData: FlGridData(
                        show: true,
                        drawVerticalLine: false,
                        getDrawingHorizontalLine: (_) =>
                            FlLine(color: widget.borderColor, strokeWidth: 0.8),
                      ),
                      borderData: FlBorderData(show: false),
                      titlesData: _buildTitles(),
                      lineBarsData: [
                        _lineData(widget.accent, (p) => p.temp, hasArea: true),
                        _lineData(const Color(0xFF3B82F6), (p) => p.humidity),
                      ],
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  LineChartBarData _lineData(
    Color color,
    double Function(_TelemetryPoint) fn, {
    bool hasArea = false,
  }) {
    return LineChartBarData(
      spots: _spots(fn),
      isCurved: true,
      color: color,
      barWidth: 3,
      isStrokeCapRound: true,
      dotData: const FlDotData(show: false),
      belowBarData: BarAreaData(
        show: hasArea,
        color: color.withValues(alpha: 0.1),
      ),
    );
  }

  FlTitlesData _buildTitles() {
    return FlTitlesData(
      leftTitles: AxisTitles(
        sideTitles: SideTitles(
          showTitles: true,
          reservedSize: 28,
          getTitlesWidget: (v, _) => Text(
            v.toInt().toString(),
            style: TextStyle(fontSize: 8, color: widget.subTextColor),
          ),
        ),
      ),
      bottomTitles: AxisTitles(
        sideTitles: SideTitles(
          showTitles: true,
          getTitlesWidget: (v, _) {
            final i = v.toInt();
            return (i < 0 || i >= _telemetry.length)
                ? const SizedBox.shrink()
                : Text(
                    _telemetry[i].time,
                    style: TextStyle(fontSize: 8, color: widget.subTextColor),
                  );
          },
        ),
      ),
      rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
      topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
    );
  }

  Widget _buildHeader() {
    return Wrap(
      alignment: WrapAlignment.spaceBetween,
      crossAxisAlignment: WrapCrossAlignment.center,
      spacing: 16,
      runSpacing: 8,
      children: [
        RichText(
          text: TextSpan(
            style: TextStyle(
              fontSize: 13,
              fontWeight: FontWeight.w900,
              fontStyle: FontStyle.italic,
              color: widget.textColor,
              letterSpacing: 0.5,
            ),
            children: [
              const TextSpan(text: 'ENVIRONMENT '),
              TextSpan(
                text: 'TELEMETRY',
                style: TextStyle(color: widget.accent),
              ),
            ],
          ),
        ),
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            _legend(widget.accent, 'Avg Temp'),
            const SizedBox(width: 12),
            _legend(const Color(0xFF3B82F6), 'Avg Humidity'),
          ],
        ),
      ],
    );
  }

  Widget _legend(Color color, String label) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 8,
          height: 8,
          decoration: BoxDecoration(shape: BoxShape.circle, color: color),
        ),
        const SizedBox(width: 6),
        Text(
          label,
          style: TextStyle(
            fontSize: 10,
            color: color,
            fontWeight: FontWeight.bold,
          ),
        ),
      ],
    );
  }
}

// ─────────────────────────────────────────────────────────────
// Daily monitoring panel
// ─────────────────────────────────────────────────────────────

class _DailyMonitoringPanel extends StatefulWidget {
  const _DailyMonitoringPanel({
    required this.accent,
    required this.isDark,
    required this.glassColor,
    required this.borderColor,
    required this.textColor,
    required this.subTextColor,
  });

  final Color accent, glassColor, borderColor, textColor, subTextColor;
  final bool isDark;

  @override
  State<_DailyMonitoringPanel> createState() => _DailyMonitoringPanelState();
}

class _DailyMonitoringPanelState extends State<_DailyMonitoringPanel> {
  @override
  void initState() {
    super.initState();
    ESP32Service.instance.addListener(_onUpdate);
  }

  @override
  void dispose() {
    ESP32Service.instance.removeListener(_onUpdate);
    super.dispose();
  }

  void _onUpdate() {
    if (mounted) setState(() {});
  }

  /// Computes the average of a given field across the last hour of NPK history.
  /// Returns null if no valid data exists.
  double? _avgLastHour(double Function(NpkLogEntry) getter) {
    final now = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    final cutoff = now - 3600; // 1 hour ago
    final history = ESP32Service.instance.npkHistory;
    final recent = history.where((e) => e.ts >= cutoff).toList();
    if (recent.isEmpty) return null;
    final vals = recent.map(getter).where((v) => v >= 0).toList();
    if (vals.isEmpty) return null;
    return vals.reduce((a, b) => a + b) / vals.length;
  }

  @override
  Widget build(BuildContext context) {
    final avgN = _avgLastHour((e) => e.n);
    final avgP = _avgLastHour((e) => e.p);
    final avgK = _avgLastHour((e) => e.k);
    final avgM = _avgLastHour((e) => e.m);

    // Compute composite nutrient score: avg(N,P,K) mapped to 0..1 range (max ~300 mg/kg)
    double nutrientScore = 0.0;
    String nutrientLabel = 'No data yet';
    bool hasNpk = false;
    if (avgN != null && avgP != null && avgK != null) {
      hasNpk = true;
      final avgNpk = (avgN + avgP + avgK) / 3.0;
      nutrientScore = (avgNpk / 150.0).clamp(0.0, 1.0);
      nutrientLabel = avgNpk < 30 ? 'Deficient' : (avgNpk < 80 ? 'Moderate' : 'Stable');
    }

    // Moisture alert
    bool moistureLow = avgM != null && avgM < 35.0;

    return ClipRRect(
      borderRadius: BorderRadius.circular(12),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 10, sigmaY: 10),
        child: Container(
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: widget.glassColor,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: widget.borderColor),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              RichText(
                text: TextSpan(
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w900,
                    fontStyle: FontStyle.italic,
                    color: widget.textColor,
                    letterSpacing: 0.5,
                  ),
                  children: [
                    const TextSpan(text: 'DAILY '),
                    TextSpan(
                      text: 'MONITORING',
                      style: TextStyle(color: widget.accent),
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 20),
              _HealthItem(
                icon: Icons.track_changes,
                label: hasNpk ? 'Avg Nutrient (N+P+K) — 1h' : 'Average Nutrient Level',
                value: hasNpk ? nutrientLabel : 'No data',
                percent: nutrientScore,
                barColor: const Color(0xFF3B82F6),
                textColor: widget.textColor,
                subTextColor: widget.subTextColor,
                isDark: widget.isDark,
              ),
              if (hasNpk) ...[
                const SizedBox(height: 12),
                _HealthItem(
                  icon: Icons.grass,
                  label: 'N avg: ${avgN!.toStringAsFixed(1)} · P avg: ${avgP!.toStringAsFixed(1)} · K avg: ${avgK!.toStringAsFixed(1)}',
                  value: 'mg/kg',
                  percent: math.min(1.0, (avgN + avgP + avgK) / 450.0),
                  barColor: const Color(0xFF22C55E),
                  textColor: widget.textColor,
                  subTextColor: widget.subTextColor,
                  isDark: widget.isDark,
                ),
              ],
              const SizedBox(height: 24),
              Divider(color: widget.borderColor),
              const SizedBox(height: 12),
              if (moistureLow)
                _AlertTile(
                  icon: Icons.water_drop_outlined,
                  iconColor: const Color(0xFF3B82F6),
                  bgColor: const Color(0xFF3B82F6),
                  label: 'Moisture Alert',
                  message: 'Avg soil moisture ${avgM!.toStringAsFixed(0)}% — Irrigation suggested',
                  isDark: widget.isDark,
                  subTextColor: widget.subTextColor,
                )
              else
                _AlertTile(
                  icon: Icons.water_drop_outlined,
                  iconColor: const Color(0xFF3B82F6),
                  bgColor: const Color(0xFF3B82F6),
                  label: 'Moisture',
                  message: avgM != null ? 'Avg moisture ${avgM.toStringAsFixed(0)}% — OK' : 'No moisture data yet — run Dip NPK',
                  isDark: widget.isDark,
                  subTextColor: widget.subTextColor,
                ),
              const SizedBox(height: 8),
              _AlertTile(
                icon: Icons.bug_report_outlined,
                iconColor: widget.accent,
                bgColor: widget.accent,
                label: 'Interference',
                message: 'Weeds detected surrounding Lettuce Plot #2',
                isDark: widget.isDark,
                subTextColor: widget.subTextColor,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// ─────────────────────────────────────────────────────────────
// Sub-widgets
// ─────────────────────────────────────────────────────────────

class _HealthItem extends StatelessWidget {
  const _HealthItem({
    required this.icon,
    required this.label,
    required this.value,
    required this.percent,
    required this.barColor,
    required this.textColor,
    required this.subTextColor,
    required this.isDark,
  });

  final IconData icon;
  final String label, value;
  final double percent;
  final Color barColor, textColor, subTextColor;
  final bool isDark;

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Row(
          children: [
            Container(
              width: 32,
              height: 32,
              decoration: BoxDecoration(
                color: isDark ? const Color(0xFF111827) : Colors.grey.shade100,
                borderRadius: BorderRadius.circular(8),
              ),
              child: Icon(icon, color: subTextColor, size: 16),
            ),
            const SizedBox(width: 12),
            Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  label,
                  style: TextStyle(
                    fontSize: 11,
                    color: textColor,
                    fontWeight: FontWeight.w500,
                  ),
                ),
                Text(value, style: TextStyle(fontSize: 9, color: subTextColor)),
              ],
            ),
          ],
        ),
        Column(
          crossAxisAlignment: CrossAxisAlignment.end,
          children: [
            Text(
              '${(percent * 100).toInt()}%',
              style: TextStyle(
                fontSize: 9,
                color: subTextColor,
                fontFamily: 'monospace',
              ),
            ),
            const SizedBox(height: 4),
            SizedBox(
              width: 64,
              height: 4,
              child: ClipRRect(
                borderRadius: BorderRadius.circular(99),
                child: LinearProgressIndicator(
                  value: percent,
                  backgroundColor: isDark
                      ? const Color(0xFF374151)
                      : Colors.grey.shade300,
                  valueColor: AlwaysStoppedAnimation<Color>(barColor),
                ),
              ),
            ),
          ],
        ),
      ],
    );
  }
}

class _AlertTile extends StatelessWidget {
  const _AlertTile({
    required this.icon,
    required this.iconColor,
    required this.bgColor,
    required this.label,
    required this.message,
    required this.isDark,
    required this.subTextColor,
  });

  final IconData icon;
  final Color iconColor, bgColor, subTextColor;
  final String label, message;
  final bool isDark;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: bgColor.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: bgColor.withValues(alpha: 0.2)),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, color: iconColor, size: 16),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  label.toUpperCase(),
                  style: TextStyle(
                    fontSize: 9,
                    fontWeight: FontWeight.w900,
                    color: iconColor,
                    letterSpacing: 1.5,
                    fontStyle: FontStyle.italic,
                  ),
                ),
                const SizedBox(height: 2),
                Text(
                  message,
                  style: TextStyle(
                    fontSize: 10,
                    color: subTextColor,
                    height: 1.4,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

// ─────────────────────────────────────────────────────────────
// Manual Control Panel
// ─────────────────────────────────────────────────────────────

class _ManualControlPanel extends StatefulWidget {
  final Color accent, glassColor, borderColor, textColor, subTextColor;
  final bool isDark;

  const _ManualControlPanel({
    required this.accent,
    required this.isDark,
    required this.glassColor,
    required this.borderColor,
    required this.textColor,
    required this.subTextColor,
  });

  @override
  State<_ManualControlPanel> createState() => _ManualControlPanelState();
}

class _ManualControlPanelState extends State<_ManualControlPanel> {
  DateTime? _lastMoveTime;

  void _move(String axis, int direction) {
    final now = DateTime.now();
    if (_lastMoveTime != null &&
        now.difference(_lastMoveTime!).inMilliseconds < 200) {
      return;
    }
    _lastMoveTime = now;

    final service = ESP32Service.instance;
    if (service.machineState == 'Alarm') {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Motors locked! Unlock via Control Panel first.'),
          backgroundColor: Colors.red,
        ),
      );
      return;
    }
    double delta = direction * 100.0; // 100mm default step
    final gcode =
        "\$J=G21G91${axis.toUpperCase()}${delta.toStringAsFixed(1)}F1000";
    service.sendCommand(gcode);
  }

  Widget _buildDirBtn(IconData icon, VoidCallback onTap) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        width: 44,
        height: 44,
        decoration: BoxDecoration(
          color: widget.isDark ? const Color(0xFF374151) : Colors.grey.shade100,
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: widget.accent.withOpacity(0.5)),
        ),
        child: Icon(icon, color: widget.accent),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(12),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 10, sigmaY: 10),
        child: Container(
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: widget.glassColor,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: widget.borderColor),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              RichText(
                text: TextSpan(
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w900,
                    fontStyle: FontStyle.italic,
                    color: widget.textColor,
                    letterSpacing: 0.5,
                  ),
                  children: [
                    const TextSpan(text: 'MANUAL '),
                    TextSpan(
                      text: 'CONTROL & SENSOR',
                      style: TextStyle(color: widget.accent),
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 20),
              Wrap(
                alignment: WrapAlignment.center,
                runAlignment: WrapAlignment.center,
                crossAxisAlignment: WrapCrossAlignment.center,
                spacing: 24,
                runSpacing: 24,
                children: [
                  // Auto Farm Button
                  Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Container(
                        width: 60,
                        height: 60,
                        decoration: BoxDecoration(
                          color: const Color(0xFF10B981).withOpacity(0.1),
                          shape: BoxShape.circle,
                          border: Border.all(color: const Color(0xFF10B981), width: 2),
                        ),
                        child: IconButton(
                          icon: const Icon(
                            Icons.precision_manufacturing,
                            color: Color(0xFF10B981),
                            size: 28,
                          ),
                          onPressed: () {
                            ESP32Service.instance.sendCommand("AUTO_FARM");
                            ScaffoldMessenger.of(context).showSnackBar(
                              const SnackBar(
                                content: Text('Starting Autonomous Routine...'),
                                backgroundColor: Color(0xFF10B981),
                              ),
                            );
                          },
                        ),
                      ),
                      const SizedBox(height: 12),
                      Text(
                        "AUTO FARM",
                        style: TextStyle(
                          color: widget.textColor,
                          fontSize: 11,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 4),
                      Text(
                        "Run full AI routine",
                        style: TextStyle(
                          color: widget.subTextColor,
                          fontSize: 9,
                        ),
                      ),
                    ],
                  ),

                  // NPK Dip Button
                  Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Container(
                        width: 60,
                        height: 60,
                        decoration: BoxDecoration(
                          color: widget.accent.withOpacity(0.1),
                          shape: BoxShape.circle,
                          border: Border.all(color: widget.accent, width: 2),
                        ),
                        child: IconButton(
                          icon: Icon(
                            Icons.water_drop,
                            color: widget.accent,
                            size: 28,
                          ),
                          onPressed: () {
                            ESP32Service.instance.sendCommand("GET_NPK");
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(
                                content: const Text('Dipping NPK Sensor...'),
                                backgroundColor: widget.accent,
                              ),
                            );
                          },
                        ),
                      ),
                      const SizedBox(height: 12),
                      Text(
                        "DIP SENSOR",
                        style: TextStyle(
                          color: widget.textColor,
                          fontSize: 11,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 4),
                      Text(
                        "Takes 1 soil reading",
                        style: TextStyle(
                          color: widget.subTextColor,
                          fontSize: 9,
                        ),
                      ),
                    ],
                  ),

                  // D-Pad
                  Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(
                        "Move Gantry (100mm steps)",
                        style: TextStyle(
                          color: widget.subTextColor,
                          fontSize: 10,
                        ),
                      ),
                      const SizedBox(height: 12),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.center,
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          _buildDirBtn(
                            Icons.keyboard_arrow_left,
                            () => _move('x', 1),
                          ),
                          const SizedBox(width: 8),
                          Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              _buildDirBtn(
                                Icons.keyboard_arrow_up,
                                () => _move('y', -1),
                              ),
                              const SizedBox(
                                height: 12,
                              ), // Adjusted for tighter wrap
                              _buildDirBtn(
                                Icons.keyboard_arrow_down,
                                () => _move('y', 1),
                              ),
                            ],
                          ),
                          const SizedBox(width: 8),
                          _buildDirBtn(
                            Icons.keyboard_arrow_right,
                            () => _move('x', -1),
                          ),
                        ],
                      ),
                    ],
                  ),
                  // Z-Axis Control
                  Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(
                        "Z-Axis",
                        style: TextStyle(
                          color: widget.subTextColor,
                          fontSize: 10,
                        ),
                      ),
                      const SizedBox(height: 12),
                      Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          _buildDirBtn(
                            Icons.keyboard_double_arrow_up,
                            () => _move('z', -1),
                          ),
                          const SizedBox(height: 8),
                          _buildDirBtn(
                            Icons.keyboard_double_arrow_down,
                            () => _move('z', 1),
                          ),
                        ],
                      ),
                    ],
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}
