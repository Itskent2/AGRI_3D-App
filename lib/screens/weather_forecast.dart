// lib/screens/weather_forecast.dart

import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:intl/intl.dart';
import '../providers/theme_provider.dart';

// ==========================================
// 1. DATA MODELS (Unchanged)
// ==========================================

class WeatherDay {
  final String day;
  final int temp;
  final String condition;
  final IconData icon;
  final int precip;

  const WeatherDay({
    required this.day,
    required this.temp,
    required this.condition,
    required this.icon,
    required this.precip,
  });

  factory WeatherDay.fromJson(Map<String, dynamic> json) {
    final conditionStr = json['condition'] as String;
    IconData mappedIcon;
    switch (conditionStr.toLowerCase()) {
      case 'sunny':
      case 'clear': mappedIcon = Icons.wb_sunny; break;
      case 'rainy':
      case 'rain': mappedIcon = Icons.grain; break;
      case 'cloudy':
      case 'clouds': mappedIcon = Icons.cloud; break;
      case 'storms':
      case 'thunderstorm': mappedIcon = Icons.thunderstorm; break;
      default: mappedIcon = Icons.wb_cloudy;
    }
    return WeatherDay(
      day: json['day'] as String,
      temp: (json['temp'] as num).toInt(),
      condition: conditionStr,
      icon: mappedIcon,
      precip: (json['precip'] as num).toInt(),
    );
  }
}

class CurrentWeather {
  final double tempC;
  final String condition;
  final double feelsLikeC;
  final double windKmh;
  final int humidity;
  final String windDirection;
  final int pressureHpa;
  final String location;

  const CurrentWeather({
    required this.tempC,
    required this.condition,
    required this.feelsLikeC,
    required this.windKmh,
    required this.humidity,
    required this.windDirection,
    required this.pressureHpa,
    required this.location,
  });
}

enum AdvisoryType { info, warning }

class GantryAdvisory {
  final AdvisoryType type;
  final String title;
  final String desc;
  const GantryAdvisory({required this.type, required this.title, required this.desc});
}

class WeatherDashboardState {
  final CurrentWeather current;
  final List<WeatherDay> forecast;
  final List<GantryAdvisory> advisories;
  WeatherDashboardState({required this.current, required this.forecast, required this.advisories});
}

// ==========================================
// 2. API LOGIC (Unchanged)
// ==========================================

String _mapWmoCodeToCondition(int code) {
  if (code == 0) return 'clear';
  if (code >= 1 && code <= 3) return 'cloudy';
  if (code >= 45 && code <= 48) return 'cloudy';
  if (code >= 51 && code <= 67) return 'rainy';
  if (code >= 80 && code <= 82) return 'rainy';
  if (code >= 95 && code <= 99) return 'storms';
  return 'cloudy';
}

String _degreesToCompass(double degrees) {
  const directions = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
  final index = ((degrees / 45) + 0.5).floor() % 8;
  return directions[index];
}

final weatherProvider = FutureProvider<WeatherDashboardState>((ref) async {
  final url = Uri.parse(
      'https://api.open-meteo.com/v1/forecast?latitude=10.296660&longitude=123.907208'
      '&current=temperature_2m,relative_humidity_2m,apparent_temperature,surface_pressure,wind_speed_10m,wind_direction_10m,weather_code'
      '&daily=weather_code,temperature_2m_max,precipitation_probability_max'
      '&timezone=Asia%2FSingapore');

  final response = await http.get(url);
  if (response.statusCode == 200) {
    final data = jsonDecode(response.body);
    final currentData = data['current'];
    final dailyData = data['daily'];

    final current = CurrentWeather(
      tempC: (currentData['temperature_2m'] as num).toDouble(),
      condition: _mapWmoCodeToCondition(currentData['weather_code'] as int),
      feelsLikeC: (currentData['apparent_temperature'] as num).toDouble(),
      windKmh: (currentData['wind_speed_10m'] as num).toDouble(),
      humidity: (currentData['relative_humidity_2m'] as num).toInt(),
      windDirection: _degreesToCompass((currentData['wind_direction_10m'] as num).toDouble()),
      pressureHpa: (currentData['surface_pressure'] as num).toInt(),
      location: '10.30°N, 123.91°E', 
    );

    List<WeatherDay> forecast = [];
    final now = DateTime.now();
    for (int i = 0; i < 7; i++) {
      final dayDate = now.add(Duration(days: i));
      forecast.add(WeatherDay.fromJson({
        'day': DateFormat('EEE').format(dayDate),
        'temp': (dailyData['temperature_2m_max'][i] as num).toInt(),
        'condition': _mapWmoCodeToCondition(dailyData['weather_code'][i] as int),
        'precip': (dailyData['precipitation_probability_max'][i] as num).toInt(),
      }));
    }

    List<GantryAdvisory> advisories = [];
    if (current.tempC > 32) {
      advisories.add(const GantryAdvisory(type: AdvisoryType.warning, title: 'High Temp Alert', desc: 'Temperatures exceeding 32°C. Monitor leafy green plots.'));
    }
    if (forecast.length > 1 && forecast[1].precip > 60) {
      advisories.add(const GantryAdvisory(type: AdvisoryType.info, title: 'Irrigation Adjusted', desc: 'High rain probability tomorrow. Auto-fertigation delayed.'));
    }
    if (advisories.isEmpty) {
      advisories.add(const GantryAdvisory(type: AdvisoryType.info, title: 'Optimal Conditions', desc: 'Current climate is optimal for all active gantry tasks.'));
    }

    return WeatherDashboardState(current: current, forecast: forecast, advisories: advisories);
  } else {
    throw Exception('Failed to load weather data');
  }
});

// ==========================================
// 3. UI WIDGETS (Updated for Dynamic Themes)
// ==========================================

class WeatherForecast extends ConsumerWidget {
  const WeatherForecast({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final themeState = ref.watch(themeProvider);
    final accent = themeState.currentAccentColor;
    final isDark = themeState.isDark(context);
    final isWide = MediaQuery.of(context).size.width >= 900;
    
    // Theme Variables
    final textColor = isDark ? Colors.white : const Color(0xFF111827);
    final subTextColor = isDark ? const Color(0xFF9CA3AF) : const Color(0xFF4B5563);
    final cardColor = isDark ? const Color(0xFF1F2937) : Colors.white;
    final borderColor = isDark ? const Color(0xFF374151) : Colors.grey.shade300;

    final weatherAsync = ref.watch(weatherProvider);

    return Container(
      padding: const EdgeInsets.all(16),
      color: Colors.transparent, // Background handled by parent scaffold
      child: weatherAsync.when(
        skipLoadingOnRefresh: false,
        loading: () => SyncingWeatherLoading(accent: accent, isDark: isDark),
        error: (err, stack) => Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Text('Error: $err', style: const TextStyle(color: Colors.red)),
              const SizedBox(height: 16),
              ElevatedButton(onPressed: () => ref.refresh(weatherProvider), child: const Text('Try Again'))
            ],
          ),
        ),
        data: (weatherState) {
          final current = weatherState.current;
          final forecastData = weatherState.forecast;
          final advisories = weatherState.advisories;
          final todayIcon = forecastData.isNotEmpty ? forecastData.first.icon : Icons.wb_sunny;

          return SingleChildScrollView(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text('Local Weather', style: TextStyle(fontSize: 24, fontWeight: FontWeight.bold, color: textColor, fontStyle: FontStyle.italic)),
                        Text('Farm Location: ${current.location}', style: TextStyle(color: subTextColor, fontSize: 10)),
                      ],
                    ),
                    IconButton(
                      icon: Container(
                        padding: const EdgeInsets.all(8),
                        decoration: BoxDecoration(color: cardColor, border: Border.all(color: borderColor), shape: BoxShape.circle),
                        child: const Icon(Icons.refresh, size: 18)), 
                      onPressed: () => ref.refresh(weatherProvider),
                    ),
                  ],
                ),
                const SizedBox(height: 16),

                if (isWide)
                  Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Expanded(flex: 2, child: _buildCurrentHero(accent, current, todayIcon, isDark, cardColor, borderColor, textColor, subTextColor)),
                      const SizedBox(width: 16),
                      Expanded(child: _buildAdvisoryPanel(accent, advisories, isDark, cardColor, borderColor, textColor, subTextColor)),
                    ],
                  )
                else
                  Column(
                    children: [
                      _buildCurrentHero(accent, current, todayIcon, isDark, cardColor, borderColor, textColor, subTextColor),
                      const SizedBox(height: 16),
                      _buildAdvisoryPanel(accent, advisories, isDark, cardColor, borderColor, textColor, subTextColor),
                    ],
                  ),

                const SizedBox(height: 16),
                Container(
                  padding: const EdgeInsets.all(16),
                  decoration: BoxDecoration(color: cardColor, borderRadius: BorderRadius.circular(20), border: Border.all(color: borderColor)),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('7-Day Outlook', style: TextStyle(color: textColor, fontWeight: FontWeight.bold)),
                      const SizedBox(height: 16),
                      SizedBox(
                        height: 140,
                        child: ListView.builder(
                          scrollDirection: Axis.horizontal,
                          itemCount: forecastData.length,
                          itemBuilder: (context, idx) => ForecastCard(day: forecastData[idx], accent: accent, isDark: isDark),
                        ),
                      )
                    ],
                  ),
                ),
              ],
            ),
          );
        },
      ),
    );
  }

  Widget _buildCurrentHero(Color accent, CurrentWeather current, IconData todayIcon, bool isDark, Color cardColor, Color borderColor, Color textColor, Color subTextColor) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(color: cardColor, borderRadius: BorderRadius.circular(20), border: Border.all(color: borderColor)),
      child: Column(
        children: [
          Row(
            children: [
              Container(
                width: 80, height: 80,
                decoration: BoxDecoration(color: isDark ? Colors.grey[900] : Colors.grey[100], borderRadius: BorderRadius.circular(20), border: Border.all(color: borderColor)),
                child: Icon(todayIcon, color: accent, size: 48),
              ),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('${current.tempC.round()}°', style: TextStyle(fontSize: 48, fontWeight: FontWeight.bold, color: textColor, fontStyle: FontStyle.italic)),
                    Text(current.condition.toUpperCase(), style: TextStyle(color: accent, fontWeight: FontWeight.bold, fontSize: 16)),
                    Text('Feels like ${current.feelsLikeC.round()}°', style: TextStyle(color: subTextColor, fontSize: 10)),
                  ],
                ),
              )
            ],
          ),
          const SizedBox(height: 16),
          Wrap(
            spacing: 16, runSpacing: 16,
            children: [
              WeatherMetricTile(icon: Icons.air, label: 'Wind Speed', value: '${current.windKmh} km/h', accent: accent, isDark: isDark),
              WeatherMetricTile(icon: Icons.water_drop, label: 'Humidity', value: '${current.humidity}%', accent: accent, isDark: isDark),
              WeatherMetricTile(icon: Icons.navigation, label: 'Direction', value: current.windDirection, accent: accent, isDark: isDark),
              WeatherMetricTile(icon: Icons.thermostat, label: 'Pressure', value: '${current.pressureHpa} hPa', accent: accent, isDark: isDark),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildAdvisoryPanel(Color accent, List<GantryAdvisory> advisories, bool isDark, Color cardColor, Color borderColor, Color textColor, Color subTextColor) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(color: cardColor, borderRadius: BorderRadius.circular(20), border: Border.all(color: borderColor)),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(children: [
            Container(width: 6, height: 6, decoration: BoxDecoration(color: accent, shape: BoxShape.circle)),
            const SizedBox(width: 8),
            Text('Gantry Advisory', style: TextStyle(color: textColor, fontWeight: FontWeight.bold)),
          ]),
          const SizedBox(height: 16),
          ...advisories.map((a) => AdvisoryCard(advisory: a, accent: accent, isDark: isDark)),
        ],
      ),
    );
  }
}

class ForecastCard extends StatelessWidget {
  final WeatherDay day;
  final Color accent;
  final bool isDark;
  const ForecastCard({super.key, required this.day, required this.accent, required this.isDark});

  @override
  Widget build(BuildContext context) {
    final cardBg = isDark ? Colors.grey[900]!.withOpacity(0.5) : Colors.grey[50];
    final borderColor = isDark ? Colors.grey[800]! : Colors.grey[200]!;
    final textColor = isDark ? Colors.white : Colors.black87;

    return Container(
      width: 120,
      margin: const EdgeInsets.only(right: 12),
      padding: const EdgeInsets.all(8),
      decoration: BoxDecoration(color: cardBg, borderRadius: BorderRadius.circular(16), border: Border.all(color: borderColor)),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Text(day.day, style: TextStyle(color: isDark ? Colors.grey[400] : Colors.grey[600], fontSize: 10, fontWeight: FontWeight.bold)),
          Icon(day.icon, color: accent, size: 24),
          Text('${day.temp}°', style: TextStyle(color: textColor, fontWeight: FontWeight.bold, fontSize: 18)),
          Text(day.condition.toUpperCase(), style: TextStyle(color: isDark ? Colors.grey[400] : Colors.grey[600], fontSize: 8, fontWeight: FontWeight.bold)),
          const SizedBox(height: 8),
          LinearProgressIndicator(value: day.precip / 100, color: Colors.blue, backgroundColor: isDark ? Colors.grey[800] : Colors.grey[200], minHeight: 4),
          const SizedBox(height: 4),
          Text('${day.precip}% Rain', style: const TextStyle(color: Colors.blue, fontSize: 8, fontWeight: FontWeight.bold)),
        ],
      ),
    );
  }
}

class WeatherMetricTile extends StatelessWidget {
  final IconData icon;
  final String label, value;
  final Color accent;
  final bool isDark;

  const WeatherMetricTile({super.key, required this.icon, required this.label, required this.value, required this.accent, required this.isDark});

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min, 
      children: [
        Container(
          width: 40, height: 40,
          decoration: BoxDecoration(color: isDark ? Colors.grey[900] : Colors.grey[100], borderRadius: BorderRadius.circular(12), border: Border.all(color: isDark ? Colors.grey[800]! : Colors.grey[300]!)),
          child: Icon(icon, size: 18, color: accent),
        ),
        const SizedBox(width: 8),
        Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(label.toUpperCase(), style: TextStyle(color: isDark ? Colors.grey[400] : Colors.grey[600], fontSize: 8, fontWeight: FontWeight.bold)),
            Text(value, style: TextStyle(color: isDark ? Colors.white : Colors.black, fontSize: 12, fontWeight: FontWeight.bold, fontStyle: FontStyle.italic)),
          ],
        )
      ],
    );
  }
}

class AdvisoryCard extends StatelessWidget {
  final GantryAdvisory advisory;
  final Color accent;
  final bool isDark;
  const AdvisoryCard({super.key, required this.advisory, required this.accent, required this.isDark});

  @override
  Widget build(BuildContext context) {
    final isWarning = advisory.type == AdvisoryType.warning;
    final textColor = isWarning ? Colors.orange : (isDark ? Colors.blueAccent : Colors.blue[700]!);

    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(8),
      decoration: BoxDecoration(
        color: textColor.withOpacity(0.1),
        border: Border.all(color: textColor.withOpacity(0.3)),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(children: [
            Container(width: 6, height: 6, decoration: BoxDecoration(color: textColor, shape: BoxShape.circle)),
            const SizedBox(width: 4),
            Expanded(child: Text(advisory.title.toUpperCase(), style: TextStyle(color: textColor, fontWeight: FontWeight.bold, fontSize: 10, fontStyle: FontStyle.italic))),
          ]),
          Text(advisory.desc, style: TextStyle(color: isDark ? Colors.grey[400] : Colors.grey[700], fontSize: 10)),
        ],
      ),
    );
  }
}

// ==========================================
// 4. LOADING (Updated)
// ==========================================

class SyncingWeatherLoading extends StatefulWidget {
  final Color accent;
  final bool isDark;
  const SyncingWeatherLoading({super.key, required this.accent, required this.isDark});
  @override
  State<SyncingWeatherLoading> createState() => _SyncingWeatherLoadingState();
}

class _SyncingWeatherLoadingState extends State<SyncingWeatherLoading> with SingleTickerProviderStateMixin {
  late AnimationController _controller;
  late Animation<double> _animation;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(duration: const Duration(milliseconds: 1500), vsync: this)..repeat(reverse: true);
    _animation = Tween<double>(begin: -10, end: 10).animate(CurvedAnimation(parent: _controller, curve: Curves.easeInOut));
  }

  @override
  void dispose() { _controller.dispose(); super.dispose(); }

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          AnimatedBuilder(
            animation: _animation,
            builder: (context, child) => Transform.translate(offset: Offset(0, _animation.value), child: child),
            child: Icon(Icons.cloud, color: widget.accent, size: 64),
          ),
          const SizedBox(height: 24),
          Text('Syncing weather...', style: TextStyle(color: widget.isDark ? Colors.grey[400] : Colors.grey[600], fontSize: 14, fontWeight: FontWeight.bold, fontStyle: FontStyle.italic)),
        ],
      ),
    );
  }
}