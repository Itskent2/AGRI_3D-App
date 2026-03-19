import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:intl/intl.dart';

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
