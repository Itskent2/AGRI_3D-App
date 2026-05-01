import 'package:flutter/material.dart';
import 'esp32_service.dart';

class Esp32LiveFeed extends StatefulWidget {
  final bool isDark;
  
  const Esp32LiveFeed({super.key, required this.isDark});

  @override
  State<Esp32LiveFeed> createState() => _Esp32LiveFeedState();
}

class _Esp32LiveFeedState extends State<Esp32LiveFeed> {
  bool _didStartStream = false;

  @override
  void initState() {
    super.initState();
    ESP32Service.instance.addListener(_onServiceUpdate);
    ESP32Service.instance.autoDiscover(); // FORCE search when entering screen
    _checkAndStartStream();
  }

  @override
  void dispose() {
    ESP32Service.instance.removeListener(_onServiceUpdate);
    if (ESP32Service.instance.isConnected) {
      ESP32Service.instance.sendCommand("STOP_STREAM");
    }
    super.dispose();
  }

  void _onServiceUpdate() {
    _checkAndStartStream();
  }

  void _checkAndStartStream() {
    final service = ESP32Service.instance;
    if (service.isConnected && !_didStartStream) {
      _didStartStream = true; 
      
      service.sendCommand("START_STREAM");
      
      // CHANGE 13 to 8 (VGA - 640x480) or 5 (QVGA - 320x240)
      // Do NOT send 13.
      service.sendCommand("SET_RES:8"); 
    } 
    else if (!service.isConnected && _didStartStream) {
      _didStartStream = false;
    }
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: ESP32Service.instance,
      builder: (context, child) {
        final service = ESP32Service.instance;
        final isConnected = service.isConnected;

        if (!isConnected) {
          return Container(
            height: 300,
            width: double.infinity,
            decoration: BoxDecoration(
              color: widget.isDark ? const Color(0xFF1F2937) : Colors.grey.shade200,
              borderRadius: BorderRadius.circular(16),
              border: Border.all(color: widget.isDark ? const Color(0xFF374151) : Colors.grey.shade300),
            ),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(Icons.videocam_off, size: 48, color: widget.isDark ? Colors.grey.shade600 : Colors.grey.shade400),
                const SizedBox(height: 16),
                Text(
                  "Camera Offline",
                  style: TextStyle(
                    color: widget.isDark ? Colors.grey.shade400 : Colors.grey.shade600,
                    fontWeight: FontWeight.bold,
                  ),
                ),
                Text(
                  "Connect to ESP32 to view live feed",
                  style: TextStyle(
                    color: widget.isDark ? Colors.grey.shade500 : Colors.grey.shade500,
                    fontSize: 12,
                  ),
                )
              ],
            ),
          );
        }

        return Container(
          height: 300,
          width: double.infinity,
          decoration: BoxDecoration(
            color: Colors.black,
            borderRadius: BorderRadius.circular(16),
            border: Border.all(color: widget.isDark ? const Color(0xFF374151) : Colors.grey.shade300, width: 2),
            boxShadow: [
              BoxShadow(
                color: Colors.black.withValues(alpha: 0.2),
                blurRadius: 10,
                offset: const Offset(0, 5),
              )
            ]
          ),
          clipBehavior: Clip.hardEdge,
          child: Stack(
            fit: StackFit.expand,
            children: [
              // Listen natively to binary JPEG frames streaming out of the WebSocket provider
              ValueListenableBuilder(
                valueListenable: service.cameraFrame,
                builder: (context, frameBytes, _) {
                  if (frameBytes == null || frameBytes.isEmpty) {
                    return Center(
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          const CircularProgressIndicator(color: Colors.white24),
                          const SizedBox(height: 16),
                          Text("Awaiting Video Stream...", style: TextStyle(color: Colors.white54, fontSize: 12, fontStyle: FontStyle.italic)),
                        ],
                      ),
                    );
                  }

                  return Image.memory(
                    frameBytes,
                    fit: BoxFit.cover,
                    gaplessPlayback: true, // Prevents flickering between frames!
                    errorBuilder: (context, error, stackTrace) {
                      return Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          const Icon(Icons.broken_image, color: Colors.orange, size: 40),
                          const SizedBox(height: 8),
                          Text("Corrupted Frame (${frameBytes.length} bytes)", style: const TextStyle(color: Colors.orange, fontSize: 10)),
                        ],
                      );
                    },
                  );
                },
              ),
              
              // Overlay indicators
              Positioned(
                top: 12,
                right: 12,
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                  decoration: BoxDecoration(
                    color: Colors.black.withValues(alpha: 0.6),
                    borderRadius: BorderRadius.circular(20),
                  ),
                  child: Row(
                    children: [
                      Container(
                        width: 8,
                        height: 8,
                        decoration: const BoxDecoration(
                            color: Colors.redAccent, shape: BoxShape.circle),
                      ),
                      const SizedBox(width: 6),
                      const Text(
                        "LIVE",
                        style: TextStyle(
                          color: Colors.white,
                          fontSize: 10,
                          fontWeight: FontWeight.bold,
                          letterSpacing: 1.2,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}