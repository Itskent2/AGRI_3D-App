import 'package:flutter/material.dart';
import '../service/ESP32/esp32_live_feed.dart';

class LiveScreen extends StatefulWidget {
  final String streamUrl;

  const LiveScreen({super.key, required this.streamUrl});

  @override
  State<LiveScreen> createState() => _LiveScreenState();
}

class _LiveScreenState extends State<LiveScreen> {
  bool isPlaying = true;

  @override
  Widget build(BuildContext context) {
    final isDark = Theme.of(context).brightness == Brightness.dark;
    final cardColor = isDark ? const Color(0xFF1F2937) : Colors.white;
    final borderColor = isDark ? const Color(0xFF374151) : Colors.grey.shade300;
    final textColor = isDark ? Colors.white70 : Colors.black87;
    final subTextColor = isDark ? Colors.white38 : Colors.black45;

    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // ── Section Label ──
          Padding(
            padding: const EdgeInsets.only(bottom: 12),
            child: Text(
              'LIVE CAMERA FEED',
              style: TextStyle(
                fontSize: 11,
                letterSpacing: 2,
                fontWeight: FontWeight.bold,
                color: subTextColor,
              ),
            ),
          ),

          // ── Camera Feed Card ──
          Container(
            width: double.infinity,
            decoration: BoxDecoration(
              color: cardColor,
              borderRadius: BorderRadius.circular(16),
              border: Border.all(color: borderColor),
            ),
            clipBehavior: Clip.hardEdge,
            child: Column(
              children: [
                // The live feed widget
                SizedBox(
                  height: 340,
                  child: isPlaying
                      ? const Esp32LiveFeed(isDark: true)
                      : Container(
                          color: Colors.black,
                          child: Center(
                            child: Column(
                              mainAxisAlignment: MainAxisAlignment.center,
                              children: [
                                Icon(Icons.pause_circle_outline,
                                    color: Colors.white38, size: 56),
                                const SizedBox(height: 12),
                                Text(
                                  'Feed Paused',
                                  style: TextStyle(
                                      color: Colors.white38, fontSize: 14),
                                ),
                              ],
                            ),
                          ),
                        ),
                ),

                // ── Controls Row ──
                Container(
                  padding: const EdgeInsets.symmetric(
                      horizontal: 20, vertical: 14),
                  decoration: BoxDecoration(
                    border: Border(top: BorderSide(color: borderColor)),
                  ),
                  child: Row(
                    children: [
                      // Play/Pause button
                      ElevatedButton.icon(
                        style: ElevatedButton.styleFrom(
                          backgroundColor:
                              isPlaying ? Colors.redAccent : Colors.green,
                          foregroundColor: Colors.white,
                          padding: const EdgeInsets.symmetric(
                              horizontal: 24, vertical: 10),
                          shape: RoundedRectangleBorder(
                              borderRadius: BorderRadius.circular(30)),
                        ),
                        onPressed: () {
                          setState(() => isPlaying = !isPlaying);
                        },
                        icon: Icon(
                            isPlaying ? Icons.pause : Icons.play_arrow,
                            size: 18),
                        label: Text(isPlaying ? 'Pause Feed' : 'Resume Feed'),
                      ),
                      const Spacer(),
                      // Status indicator
                      Row(
                        children: [
                          Container(
                            width: 8,
                            height: 8,
                            decoration: BoxDecoration(
                              color: isPlaying
                                  ? Colors.greenAccent
                                  : Colors.grey,
                              shape: BoxShape.circle,
                            ),
                          ),
                          const SizedBox(width: 6),
                          Text(
                            isPlaying ? 'Streaming' : 'Stopped',
                            style: TextStyle(color: textColor, fontSize: 13),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),

          const SizedBox(height: 16),

          // ── Telemetry Cards ──
          Padding(
            padding: const EdgeInsets.only(bottom: 12),
            child: Text(
              'SENSOR TELEMETRY',
              style: TextStyle(
                fontSize: 11,
                letterSpacing: 2,
                fontWeight: FontWeight.bold,
                color: subTextColor,
              ),
            ),
          ),
          Row(
            children: [
              Expanded(
                child: _buildDataCard('Soil Moisture', '-- %',
                    Icons.water_drop, Colors.blueAccent, cardColor, borderColor, textColor, subTextColor),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: _buildDataCard('Nitrogen', '-- mg/kg',
                    Icons.science, Colors.greenAccent, cardColor, borderColor, textColor, subTextColor),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildDataCard(
    String label,
    String value,
    IconData icon,
    Color accentColor,
    Color cardColor,
    Color borderColor,
    Color textColor,
    Color subTextColor,
  ) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: cardColor,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: borderColor),
      ),
      child: Row(
        children: [
          Container(
            padding: const EdgeInsets.all(10),
            decoration: BoxDecoration(
              color: accentColor.withOpacity(0.15),
              borderRadius: BorderRadius.circular(10),
            ),
            child: Icon(icon, color: accentColor, size: 22),
          ),
          const SizedBox(width: 14),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(value,
                  style: TextStyle(
                      fontSize: 20,
                      fontWeight: FontWeight.bold,
                      color: textColor)),
              Text(label,
                  style: TextStyle(color: subTextColor, fontSize: 12)),
            ],
          ),
        ],
      ),
    );
  }
}