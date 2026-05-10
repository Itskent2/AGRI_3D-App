// lib/screens/monitoring_dashboard.dart

import 'dart:ui';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:fl_chart/fl_chart.dart';
import '../providers/theme_provider.dart';
import '../providers/sensor_provider.dart';
import '../widgets/sensor_heatmaps.dart';
import '../service/ESP32/esp32_service.dart';

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

              // ── Dynamic Sensor Status Grid ──
              ListenableBuilder(
                listenable: sensors,
                builder: (context, _) {
                  return GridView.count(
                    crossAxisCount: isWide ? 4 : 2,
                    crossAxisSpacing: 16,
                    mainAxisSpacing: 16,
                    shrinkWrap: true,
                    physics: const NeverScrollableScrollPhysics(),
                    childAspectRatio: isWide ? 1.5 : 2.2,
                    children: [
                      _StatusCard(
                        icon: Icons.water_drop,
                        label: 'Soil Moisture',
                        value: '${sensors.soilMoisture.toStringAsFixed(0)}%',
                        color: const Color(0xFF3B82F6),
                        trend: 'Stable',
                        alert: sensors.soilMoisture < 30,
                        isDark: isDark,
                        glassColor: glassColor,
                        borderColor: borderColor,
                        textColor: textColor,
                        subTextColor: subTextColor,
                      ),
                      _StatusCard(
                        icon: Icons.grass,
                        label: 'Nitrogen (N)',
                        value: '${sensors.nitrogen.toStringAsFixed(0)}',
                        color: const Color(0xFF22C55E),
                        trend: 'mg/kg',
                        alert: sensors.nitrogen < 20,
                        isDark: isDark,
                        glassColor: glassColor,
                        borderColor: borderColor,
                        textColor: textColor,
                        subTextColor: subTextColor,
                      ),
                      _StatusCard(
                        icon: Icons.grain,
                        label: 'Phosphorus (P)',
                        value: '${sensors.phosphorus.toStringAsFixed(0)}',
                        color: const Color(0xFFA855F7),
                        trend: 'mg/kg',
                        alert: sensors.phosphorus < 20,
                        isDark: isDark,
                        glassColor: glassColor,
                        borderColor: borderColor,
                        textColor: textColor,
                        subTextColor: subTextColor,
                      ),
                      _StatusCard(
                        icon: Icons.science,
                        label: 'Potassium (K)',
                        value: '${sensors.potassium.toStringAsFixed(0)}',
                        color: const Color(0xFFEAB308),
                        trend: 'mg/kg',
                        alert: sensors.potassium < 20,
                        isDark: isDark,
                        glassColor: glassColor,
                        borderColor: borderColor,
                        textColor: textColor,
                        subTextColor: subTextColor,
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
// Weather banner
// ─────────────────────────────────────────────────────────────

class _WeatherBanner extends StatelessWidget {
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
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(12),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 10, sigmaY: 10),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
          decoration: BoxDecoration(
            color: glassColor,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: accent.withValues(alpha: 0.3)),
          ),
          child: Row(
            children: [
              Icon(Icons.wb_sunny, color: accent, size: 20),
              const SizedBox(width: 12),
              Expanded(
                child: Text(
                  'Clear Skies: Optimal photosynthesis active',
                  style: TextStyle(
                    fontSize: 10,
                    fontWeight: FontWeight.bold,
                    color: textColor,
                    letterSpacing: 1.5,
                  ),
                ),
              ),
              const Icon(Icons.grain, color: Color(0xFF6B7280), size: 16),
              const SizedBox(width: 6),
              const Text(
                'Next Rain: Wed',
                style: TextStyle(
                  fontSize: 10,
                  color: Color(0xFF6B7280),
                  fontFamily: 'monospace',
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
// Status card
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
  });

  final IconData icon;
  final String label, value, trend;
  final Color color, glassColor, borderColor, textColor, subTextColor;
  final bool alert, isDark;

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(12),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 10, sigmaY: 10),
        child: Container(
          padding: const EdgeInsets.all(16),
          decoration: BoxDecoration(
            color: glassColor,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: borderColor),
          ),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Row(
                children: [
                  Container(
                    padding: const EdgeInsets.all(10),
                    decoration: BoxDecoration(
                      color: color.withValues(alpha: 0.1),
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Icon(icon, color: color, size: 20),
                  ),
                  const SizedBox(width: 16),
                  Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        label.toUpperCase(),
                        style: TextStyle(
                          fontSize: 9,
                          color: subTextColor,
                          fontFamily: 'monospace',
                          letterSpacing: 1.5,
                        ),
                      ),
                      Text(
                        value,
                        style: TextStyle(
                          fontSize: 22,
                          fontWeight: FontWeight.w900,
                          color: textColor,
                          fontStyle: FontStyle.italic,
                          letterSpacing: -0.5,
                        ),
                      ),
                    ],
                  ),
                ],
              ),
              Column(
                crossAxisAlignment: CrossAxisAlignment.end,
                children: [
                  Text(
                    trend,
                    style: TextStyle(
                      fontSize: 10,
                      fontWeight: FontWeight.bold,
                      color: alert ? const Color(0xFFEF4444) : subTextColor,
                    ),
                  ),
                  if (alert)
                    Container(
                      margin: const EdgeInsets.only(top: 4),
                      width: 6,
                      height: 6,
                      decoration: const BoxDecoration(
                        shape: BoxShape.circle,
                        color: Color(0xFFEF4444),
                      ),
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

class _DailyMonitoringPanel extends StatelessWidget {
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
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(12),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 10, sigmaY: 10),
        child: Container(
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: glassColor,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: borderColor),
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
                    color: textColor,
                    letterSpacing: 0.5,
                  ),
                  children: [
                    const TextSpan(text: 'DAILY '),
                    TextSpan(
                      text: 'MONITORING',
                      style: TextStyle(color: accent),
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 20),
              _HealthItem(
                icon: Icons.track_changes,
                label: 'Average Nutrient Level',
                value: 'Stable',
                percent: 0.85,
                barColor: const Color(0xFF3B82F6),
                textColor: textColor,
                subTextColor: subTextColor,
                isDark: isDark,
              ),
              const SizedBox(height: 24),
              Divider(color: borderColor),
              const SizedBox(height: 12),
              _AlertTile(
                icon: Icons.warning_amber_rounded,
                iconColor: const Color(0xFFEF4444),
                bgColor: const Color(0xFFEF4444),
                label: 'Crop Alert',
                message: 'Sector B-4: Fungal damage detected in Carrot Plot #3',
                isDark: isDark,
                subTextColor: subTextColor,
              ),
              const SizedBox(height: 8),
              _AlertTile(
                icon: Icons.bug_report_outlined,
                iconColor: accent,
                bgColor: accent,
                label: 'Interference',
                message: 'Weeds detected surrounding Lettuce Plot #2',
                isDark: isDark,
                subTextColor: subTextColor,
              ),
              const SizedBox(height: 8),
              _AlertTile(
                icon: Icons.water_drop_outlined,
                iconColor: const Color(0xFF3B82F6),
                bgColor: const Color(0xFF3B82F6),
                label: 'Moisture',
                message: 'Sector 4 moisture below 35% - Irrigation suggested',
                isDark: isDark,
                subTextColor: subTextColor,
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
