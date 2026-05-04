// lib/service/ESP32/esp32_service.dart

import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'dart:io';

/// Log categories for filtering in different consoles
enum LogTag {
  system,
  camera,
  movement,
  grbl,
  ping,
  state,
  sd,
  npk,
  scan,
  gcode,
  error,
  debug,
}

class TaggedLog {
  final String message;
  final Set<LogTag> tags;
  final DateTime time;
  TaggedLog(this.message, this.tags) : time = DateTime.now();
}

class ESP32Service extends ChangeNotifier {
  WebSocketChannel? _channel;
  bool isConnected = false;
  bool isScanning = false;
  String? _lastKnownIP;
  String? get currentIP => _lastKnownIP;
  List<TaggedLog> taggedLogs = [];

  /// Convenience getter for the raw log strings (used by existing terminal UI)
  List<String> get logs => taggedLogs.map((e) => e.message).toList();

  /// Filtered logs for the Live Feed console (camera + movement + errors only)
  List<TaggedLog> get cameraLogs => taggedLogs
      .where(
        (l) =>
            l.tags.intersection({
              LogTag.camera,
              LogTag.movement,
              LogTag.grbl,
              LogTag.scan,
              LogTag.error,
              LogTag.system,
            }).isNotEmpty &&
            !l.tags.contains(LogTag.ping) &&
            !l.tags.contains(LogTag.state),
      )
      .toList();
  // ── Session tracking (prevents ghost kick-loops) ──
  static final String _sessionId = DateTime.now().millisecondsSinceEpoch
      .toString()
      .substring(7);
  Timer? _watchdogTimer;

  // ── Connection generation counter (prevents stale disconnect storms) ──
  int _connectionGen = 0;
  StreamSubscription? _channelSubscription;
  bool _isConnecting = false; // Mutex: only one connection attempt at a time

  // ── Disconnect Diagnostics ──
  String? lastDisconnectReason; // Human-readable reason for the last disconnect

  // ── UI Update Debounce ──
  Timer? _notifyDebounce;
  bool _notifyScheduled = false;

  // ── PING-PONG VARIABLES ──
  Timer? _pingTimer;
  int _missedPings = 0;
  int latencyMs = 0; // Round-trip time in ms
  int pingCount = 0; // Total pings acknowledged by ESP32
  DateTime? _lastPingSentAt; // When we sent the last PING

  final ValueNotifier<Uint8List?> cameraFrame = ValueNotifier(null);
  DateTime _lastFrameUpdate = DateTime(0); // Throttle UI frame updates

  double x = 0, y = 0, z = 0;
  double maxX = 1000.0, maxY = 1000.0, maxZ = 1000.0;

  // ── NEW: Track if the machine is Homed, Idle, or Running ──
  String machineState = "Unknown";

  // ── Nano (GRBL) connection status ──
  bool nanoConnected = false;

  // ── G-Code Job Progress (0.0 = idle, 0.0–1.0 = running, 1.0 = done) ──
  final ValueNotifier<double> jobProgress = ValueNotifier(0.0);

  // ── SD Card Upload State ──
  bool hasStoredGcode = false;
  int storedGcodeSize = 0;
  bool isUploadingGcode = false;
  double uploadProgress = 0.0;
  Completer<void>? _uploadAckCompleter;

  // ── NEW: Frame Metadata & Plant Detection ──
  Map<String, dynamic>? _pendingFrameMeta;

  // Events for UI to listen to
  final StreamController<Map<String, dynamic>> _plantCandidateCtrl =
      StreamController.broadcast();
  Stream<Map<String, dynamic>> get onPlantCandidate =>
      _plantCandidateCtrl.stream;

  final StreamController<Map<String, dynamic>> _scanCompleteCtrl =
      StreamController.broadcast();
  Stream<Map<String, dynamic>> get onScanComplete => _scanCompleteCtrl.stream;

  final StreamController<Map<String, dynamic>> _frameCapturedCtrl =
      StreamController.broadcast();
  Stream<Map<String, dynamic>> get onFrameCaptured => _frameCapturedCtrl.stream;

  final StreamController<Map<String, dynamic>> _npkUpdateCtrl =
      StreamController.broadcast();
  Stream<Map<String, dynamic>> get onNpkUpdate => _npkUpdateCtrl.stream;

  static final ESP32Service instance = ESP32Service();

  Completer<void>? _discoveryCompleter;

  Future<void> autoDiscover({String reason = "Unknown"}) async {
    if (isConnected) return;
    // If a discovery is already running, wait for it to finish rather than starting another
    if (_discoveryCompleter != null) return _discoveryCompleter!.future;

    _addLog("SYS: 🔍 Discovery triggered (Reason: $reason)", {
      LogTag.system,
      LogTag.debug,
    });

    _discoveryCompleter = Completer<void>();
    isScanning = true;
    _scheduleNotify();

    final prefs = await SharedPreferences.getInstance();
    _lastKnownIP = prefs.getString('lastKnownIP');

    // ── Single Discovery Pass ──
    // We try each host once. If we connect, we stop.
    // This allows the Future to complete so UI (RefreshIndicator) can finish.

    final Set<String> candidates = {};
    if (kIsWeb) candidates.add("192.168.0.137");
    if (_lastKnownIP != null) candidates.add(_lastKnownIP!);
    candidates.add("192.168.4.1");
    if (!kIsWeb) candidates.add("farmbot.local");

    for (final host in candidates) {
      if (isConnected) break;
      await _connectAndVerify(host, reason: "$reason ($host)");
    }

    if (!isConnected && !kIsWeb) {
      await sweepMobileSubnets(this);
    }

    isScanning = false;
    _scheduleNotify();

    // Clean up the completer so next call can run
    final c = _discoveryCompleter;
    _discoveryCompleter = null;
    c?.complete();

    // Start a watchdog to re-trigger discovery if we drop later
    _watchdogTimer?.cancel();
    _watchdogTimer = Timer.periodic(const Duration(seconds: 30), (timer) {
      if (!isConnected && !isScanning && !_isConnecting) {
        autoDiscover(reason: "Watchdog");
      }
    });
  }

  Future<void> _connectAndVerify(
    String host, {
    String reason = "Unknown",
  }) async {
    if (isConnected || _isConnecting) return;
    final int thisGen = ++_connectionGen;
    _addLog(
      "SYS: 🔌 Connection attempt to $host (Gen: $thisGen, Reason: $reason)",
      {LogTag.system, LogTag.debug},
    );
    _isConnecting = true;
    final completer = Completer<bool>();

    try {
      // Kill any previous connection & listener cleanly
      _channelSubscription?.cancel();
      _channelSubscription = null;
      if (_channel != null) {
        _addLog("SYS: ♻ Cleaning up old channel before Gen $thisGen", {
          LogTag.system,
          LogTag.debug,
        });
        _channel!.sink.close();
        _channel = null;
      }

      final url = "ws://$host/ws?sid=$_sessionId";
      final channel = WebSocketChannel.connect(Uri.parse(url));
      await channel.ready.timeout(const Duration(seconds: 5));

      // Double-check: if something else connected while we were awaiting
      if (isConnected || thisGen != _connectionGen) {
        _addLog(
          "SYS: 🛡 Closing redundant connection (Gen: $thisGen vs $_connectionGen)",
          {LogTag.system, LogTag.debug},
        );
        channel.sink.close();
        return;
      }

      bool identified = false;
      _channel = channel;

      _channelSubscription = _channel!.stream.listen(
        (msg) async {
          // ANY data received (binary or text) means connection is alive
          _missedPings = 0;

          if (msg is List<int>) {
            if (!identified) return;
            final bytes = msg is Uint8List ? msg : Uint8List.fromList(msg);

            // Throttle UI updates to ~15fps to avoid overwhelming Flutter
            final now = DateTime.now();
            if (now.difference(_lastFrameUpdate).inMilliseconds >= 66) {
              _lastFrameUpdate = now;
              cameraFrame.value = bytes;
            }

            // If we have pending metadata (from a scan), emit the captured frame event
            if (_pendingFrameMeta != null) {
              _pendingFrameMeta!['image'] = bytes; // attach image
              _frameCapturedCtrl.add(_pendingFrameMeta!);
              _pendingFrameMeta = null;
            }
            return;
          }

          String textMsg = msg.toString();

          if (!identified) {
            if (textMsg.contains('"evt":"SYSTEM_STATE"') ||
                textMsg.contains('"system":"AGRI_3D"') ||
                textMsg.startsWith("FARMBOT_ID:")) {
              identified = true;
              isConnected = true;
              _addLog("SYS: ✓ Online → $host (Gen: $thisGen)", {LogTag.system});
              _startPingLoop();
              sendCommand("GET_GCODE_INFO");

              try {
                final parsed = jsonDecode(textMsg);
                if (parsed['x'] != null) x = (parsed['x'] as num).toDouble();
                if (parsed['y'] != null) y = (parsed['y'] as num).toDouble();
                if (parsed['z'] != null) z = (parsed['z'] as num).toDouble();
                if (parsed['maxX'] != null)
                  maxX = (parsed['maxX'] as num).toDouble();
                if (parsed['maxY'] != null)
                  maxY = (parsed['maxY'] as num).toDouble();
                _scheduleNotify();
              } catch (_) {}
              if (!completer.isCompleted) completer.complete(true);
            }
            return;
          }

          // ── CATCH THE PONG IMMEDIATELY ──
          if (textMsg.contains('"evt":"PONG"') ||
              textMsg.contains('"status":"PONG"')) {
            // (_missedPings already reset at top of listen)

            // Calculate round-trip latency

            if (_lastPingSentAt != null) {
              latencyMs = DateTime.now()
                  .difference(_lastPingSentAt!)
                  .inMilliseconds;
              _lastPingSentAt = null;
            }

            // Extract ping count from ESP32 response
            try {
              final pong = jsonDecode(textMsg);
              if (pong['ping_no'] != null)
                pingCount = (pong['ping_no'] as num).toInt();
            } catch (_) {}

            _addLog("RX: PONG (Latency: ${latencyMs}ms)", {
              LogTag.ping,
              LogTag.debug,
            });
            _scheduleNotify(); // Update UI with new latency
            return;
          }

          // FIXED: Filter out continuous status reports from terminal logs
          //if (!textMsg.contains("<") && !textMsg.contains(">")) {
          _addLog("RX: $textMsg", _classifyRx(textMsg));
          //}

          try {
            final parsed = jsonDecode(textMsg);

            // ── Frame Metadata & Detection ──
            if (parsed['evt'] == 'FRAME_META' ||
                parsed['evt'] == 'DETECT_FRAME') {
              _pendingFrameMeta =
                  parsed; // The NEXT binary message is this frame
              return;
            }
            if (parsed['evt'] == 'PLANT_CANDIDATE') {
              _plantCandidateCtrl.add(parsed);
              _pendingFrameMeta = parsed; // The NEXT binary is the thumbnail
              return;
            }
            if (parsed['evt'] == 'DETECTION_COMPLETE') {
              _scanCompleteCtrl.add(parsed);
              return;
            }
            if (parsed['evt'] == 'NPK_N' ||
                parsed['evt'] == 'NPK_P' ||
                parsed['evt'] == 'NPK_K' ||
                parsed['evt'] == 'NPK' ||
                parsed['evt'] == 'NPK_LOG_CHUNK' ||
                parsed['evt'] == 'NPK_LOG_END') {
              _npkUpdateCtrl.add(parsed);
              return;
            }
            if (parsed['evt'] == 'GCODE_INFO') {
              hasStoredGcode = parsed['file'] != null;
              storedGcodeSize = parsed['size'] ?? 0;
              _scheduleNotify();
              return;
            }
            if (parsed['evt'] == 'UPLOAD_ACK') {
              _uploadAckCompleter?.complete();
              _uploadAckCompleter = null;
              return;
            }
            if (parsed['evt'] == 'SD_PROGRESS') {
              jobProgress.value = (parsed['pct'] as num).toDouble() / 100.0;
              return;
            }
            if (parsed['evt'] == 'SD_START') {
              jobProgress.value = 0.01;
              _addLog("SYS: SD Stream Started (${parsed['file']})");
              return;
            }
            if (parsed['evt'] == 'SD_COMPLETE') {
              jobProgress.value = 0.0;
              _addLog("SYS: SD Stream Complete. (${parsed['lines']} lines)");
              return;
            }
            if (parsed['evt'] == 'SD_STOPPED') {
              jobProgress.value = 0.0;
              _addLog("SYS: SD Stream Stopped by User.");
              return;
            }

            // ── System State Update (including Nano connection) ──
            if (parsed['evt'] == 'SYSTEM_STATE') {
              if (parsed['nano'] != null) {
                final wasConnected = nanoConnected;
                nanoConnected = parsed['nano'] == 'CONNECTED';
                if (nanoConnected != wasConnected) {
                  _addLog(
                    nanoConnected
                        ? "SYS: Nano (GRBL) connected."
                        : "SYS: ⚠ Nano (GRBL) not detected — check Serial1 wiring.",
                  );
                }
              }

              if (parsed['x'] != null) x = (parsed['x'] as num).toDouble();
              if (parsed['y'] != null) y = (parsed['y'] as num).toDouble();
              if (parsed['z'] != null) z = (parsed['z'] as num).toDouble();
              _scheduleNotify();
              return; // Handled system state
            }

            if (parsed['nano_raw'] != null) {
              String raw = parsed['nano_raw'].toString();
              _parseGrblStatus(raw);

              if (raw.startsWith("\$130=")) {
                maxX = double.tryParse(raw.substring(5)) ?? maxX;
                _scheduleNotify();
              }
              if (raw.startsWith("\$131=")) {
                maxY = double.tryParse(raw.substring(5)) ?? maxY;
                _scheduleNotify();
              }
              if (raw.startsWith("\$132=")) {
                maxZ = double.tryParse(raw.substring(5)) ?? maxZ;
                _scheduleNotify();
              }
            }
          } catch (_) {}
        },
        onDone: () {
          if (!completer.isCompleted) completer.complete(false);
          // Only process disconnect if THIS connection is still the active one
          if (thisGen == _connectionGen) {
            // Extract close code & reason from the WebSocket sink
            final closeCode = _channel?.closeCode;
            final closeReason = _channel?.closeReason;
            _handleDisconnect(_describeDone(closeCode, closeReason));
          }
        },
        onError: (err) {
          if (!completer.isCompleted) completer.complete(false);
          if (thisGen == _connectionGen) {
            _handleDisconnect('Socket error: $err');
          }
        },
      );
      await completer.future.timeout(const Duration(seconds: 3));
    } catch (e) {
      _isConnecting = false;
      _handleDisconnect('Connection failed: $e');
    } finally {
      _isConnecting = false;
    }
  }

  void _parseGrblStatus(String raw) {
    // ── NEW: Extract Machine State (Idle, Run, Alarm, etc.) ──
    if (raw.startsWith("<")) {
      int firstPipe = raw.indexOf("|");
      if (firstPipe > 1) {
        machineState = raw.substring(1, firstPipe);
      }
    }

    int posStart = raw.indexOf("MPos:");
    if (posStart == -1) posStart = raw.indexOf("WPos:");

    if (posStart != -1) {
      posStart += 5;
      int posEnd = raw.indexOf("|", posStart);
      if (posEnd == -1) posEnd = raw.indexOf(">", posStart);

      if (posEnd != -1) {
        String posStr = raw.substring(posStart, posEnd);
        List<String> parts = posStr.split(",");
        if (parts.length >= 3) {
          x = double.tryParse(parts[0]) ?? x;
          y = double.tryParse(parts[1]) ?? y;
          z = double.tryParse(parts[2]) ?? z;
          _scheduleNotify();
        }
      }
    }
  }

  Future<void> connectAndVerifyHost(String host) => _connectAndVerify(host);

  /// Describe the WebSocket close reason from the close code.
  String _describeDone(int? code, String? reason) {
    // Build a concise human-readable explanation
    final parts = <String>[];
    if (code != null) {
      switch (code) {
        case 1000:
          parts.add('Normal closure');
          break;
        case 1001:
          parts.add('Server going away');
          break;
        case 1006:
          parts.add(
            'Abnormal closure (no close frame received — likely kicked or network drop)',
          );
          break;
        case 1008:
          parts.add('Policy violation');
          break;
        case 1011:
          parts.add('Server internal error');
          break;
        default:
          parts.add('Close code $code');
      }
    } else {
      parts.add('Connection lost (no close code)');
    }
    if (reason != null && reason.isNotEmpty) {
      parts.add('reason: $reason');
    }
    return parts.join(' — ');
  }

  void _handleDisconnect([String? reason]) {
    if (!isConnected && _channel == null)
      return; // Already disconnected, don't spam
    isConnected = false;
    nanoConnected = false; // Reset Nano state on disconnect
    _pingTimer?.cancel(); // Kill the Ping loop
    _channelSubscription?.cancel();
    _channelSubscription = null;
    _channel = null;
    cameraFrame.value = null;

    // Store & log the specific reason
    final int? code = _channel?.closeCode;
    lastDisconnectReason = reason ?? 'Unknown';
    _addLog(
      "SYS: ⚠ Disconnected (Code: $code) → $lastDisconnectReason (Gen: $_connectionGen)",
      {LogTag.system, LogTag.error},
    );
    _scheduleNotify();
  }

  void _startPingLoop() {
    _pingTimer?.cancel();
    _missedPings = 0;

    // Ping the ESP32 every 2 seconds
    _pingTimer = Timer.periodic(const Duration(seconds: 2), (timer) {
      if (!isConnected) {
        timer.cancel();
        return;
      }

      // If we missed 20 PONGs in a row (40 seconds total), the ESP32 is gone!
      // This is extremely lenient to accommodate heavy processing or poor WiFi.
      if (_missedPings >= 20) {
        _handleDisconnect(
          'Ping watchdog timeout (${_missedPings} missed PONGs — ESP32 unresponsive)',
        );
        return;
      }

      _missedPings++; // Add a strike
      _lastPingSentAt = DateTime.now(); // Record send time for latency
      sendCommand("PING"); // Send the ping to the ESP32
    });
  }

  void sendCommand(String cmd) {
    if (_channel != null && isConnected) {
      _channel!.sink.add(cmd);
      _addLog("TX: $cmd", _classifyTx(cmd));
    }
  }

  void setFPM(int fpm) {
    sendCommand("SET_FPM:$fpm");
  }

  void startPhotoScan(
    int cols,
    int rows,
    double stepX,
    double stepY,
    double zHeight,
  ) {
    sendCommand(
      "SCAN_PHOTO:$cols:$rows:${stepX.toStringAsFixed(1)}:${stepY.toStringAsFixed(1)}:${zHeight.toStringAsFixed(1)}",
    );
  }

  void startAutoDetect(
    int cols,
    int rows,
    double stepX,
    double stepY,
    double zHeight,
  ) {
    sendCommand(
      "AUTO_DETECT_PLANTS:$cols:$rows:${stepX.toStringAsFixed(1)}:${stepY.toStringAsFixed(1)}:${zHeight.toStringAsFixed(1)}",
    );
  }

  void confirmPlant(double x, double y, String name) {
    sendCommand(
      "CONFIRM_PLANT:${x.toStringAsFixed(1)}:${y.toStringAsFixed(1)}:$name",
    );
  }

  void rejectPlant(double x, double y) {
    sendCommand("REJECT_PLANT:${x.toStringAsFixed(1)}:${y.toStringAsFixed(1)}");
  }

  void updatePos(String axis, double val, String gcode) {
    if (axis == 'x') x = val;
    if (axis == 'y') y = val;
    if (axis == 'z') z = val;
    sendCommand(gcode);
    _scheduleNotify();
  }

  void addLog(String m) => _addLog(m, {LogTag.system});
  void _addLog(String m, [Set<LogTag> tags = const {LogTag.system}]) {
    taggedLogs.add(TaggedLog(m, tags));
    if (taggedLogs.length > 200) taggedLogs.removeAt(0);
    _scheduleNotify(); // Debounced — prevents assertion floods on rapid log bursts
  }

  /// Debounced notifyListeners — coalesces rapid updates into a single frame
  void _scheduleNotify() {
    if (_notifyScheduled) return;
    _notifyScheduled = true;

    // Timer.run is safer than microtasks for UI updates on Web
    // as it ensures we aren't inside the same rendering frame.
    Timer.run(() {
      _notifyScheduled = false;
      if (hasListeners) {
        notifyListeners();
      }
    });
  }

  void clearLogs() {
    taggedLogs.clear();
    _scheduleNotify();
  }

  /// Classify an outgoing TX command into log tags
  Set<LogTag> _classifyTx(String cmd) {
    if (cmd == 'PING') return {LogTag.ping};
    if (cmd == 'GET_GCODE_INFO' || cmd == 'GET_STATE') return {LogTag.state};
    if (cmd.startsWith('START_STREAM') ||
        cmd.startsWith('STOP_STREAM') ||
        cmd.startsWith('SET_FPM') ||
        cmd.startsWith('SET_RES'))
      return {LogTag.camera};
    if (cmd.startsWith('G0') || cmd.startsWith('G1') || cmd.startsWith('G28'))
      return {LogTag.movement};
    if (cmd.startsWith('\$H')) return {LogTag.movement};
    if (cmd.startsWith('SCAN_') || cmd.startsWith('AUTO_DETECT'))
      return {LogTag.scan, LogTag.camera};
    if (cmd.startsWith('UPLOAD_') ||
        cmd.startsWith('START_SD') ||
        cmd == 'STOP_SD')
      return {LogTag.sd, LogTag.gcode};
    if (cmd.startsWith('NPK') || cmd == 'GET_NPK') return {LogTag.npk};
    return {LogTag.gcode};
  }

  /// Classify an incoming RX message into log tags
  Set<LogTag> _classifyRx(String msg) {
    if (msg.contains('"evt":"SYSTEM_STATE"')) return {LogTag.state};
    if (msg.contains('"evt":"PONG"')) return {LogTag.ping};
    if (msg.contains('nano_raw')) {
      if (msg.contains('MPos:') ||
          msg.contains('Home') ||
          msg.contains('Run') ||
          msg.contains('Jog'))
        return {LogTag.grbl, LogTag.movement};
      return {LogTag.grbl};
    }
    if (msg.contains('"evt":"FRAME_META"') ||
        msg.contains('"evt":"DETECT_FRAME"') ||
        msg.contains('PLANT_CANDIDATE') ||
        msg.contains('DETECTION_COMPLETE'))
      return {LogTag.camera, LogTag.scan};
    if (msg.contains('"evt":"SD_') ||
        msg.contains('"evt":"GCODE_INFO"') ||
        msg.contains('"evt":"UPLOAD_ACK"'))
      return {LogTag.sd, LogTag.gcode};
    if (msg.contains('NPK')) return {LogTag.npk};
    if (msg.contains('"evt":"ERROR"') || msg.contains('error'))
      return {LogTag.error};
    return {LogTag.system};
  }

  Future<void> executeGCode(List<String> lines) async {
    for (var line in lines) {
      if (!isConnected) break;
      line = line.trim();
      if (line.isEmpty || line.startsWith(';')) continue;

      sendCommand(line);
      // Wait a bit to not overwhelm the ESP32 / GRBL buffer
      await Future.delayed(const Duration(milliseconds: 100));
    }
  }

  // ── SD Card Upload and Execution ──
  Future<void> uploadGcodeChunked(String content) async {
    if (!isConnected) return;
    isUploadingGcode = true;
    uploadProgress = 0.0;
    _scheduleNotify();

    _addLog("SYS: Starting G-Code Upload...");
    sendCommand("UPLOAD_START");
    await Future.delayed(const Duration(milliseconds: 500));

    final lines = content.split('\n');
    int currentLine = 0;

    while (currentLine < lines.length && isUploadingGcode) {
      if (!isConnected) break;

      // Send in chunks of 50 lines to avoid WebSocket limits
      int endLine = currentLine + 50;
      if (endLine > lines.length) endLine = lines.length;

      String chunk = '${lines.sublist(currentLine, endLine).join('\n')}\n';

      _uploadAckCompleter = Completer<void>();
      sendCommand("UPLOAD_CHUNK:$chunk");

      try {
        await _uploadAckCompleter!.future.timeout(const Duration(seconds: 3));
      } catch (e) {
        _addLog("SYS: Upload timeout! Retrying chunk...");
        await Future.delayed(const Duration(milliseconds: 1000));
        continue;
      }

      currentLine = endLine;
      uploadProgress = currentLine / lines.length;
      _scheduleNotify();
    }

    if (isUploadingGcode) {
      sendCommand("UPLOAD_END");
      hasStoredGcode = true;
      _addLog("SYS: Upload complete!");
    }

    isUploadingGcode = false;
    uploadProgress = 0.0;
    _scheduleNotify();
  }

  void cancelGcodeUpload() {
    isUploadingGcode = false;
    sendCommand("UPLOAD_END");
    _scheduleNotify();
  }

  void runStoredGcode() {
    if (!isConnected || !hasStoredGcode) return;
    sendCommand("START_SD:/gcode/current_job.gcode");
  }

  void cancelGcodeJob() {
    sendCommand("STOP_SD");
  }
}

Future<void> sweepMobileSubnets(ESP32Service service) async {
  // If we are on Web, RawDatagramSocket is not available.
  if (kIsWeb) return;

  service.addLog("SYS: Listening for UDP discovery beacons...");
  RawDatagramSocket? socket;
  try {
    socket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 4210);
    socket.broadcastEnabled = true;

    final completer = Completer<String?>();

    socket.listen((RawSocketEvent event) {
      if (event == RawSocketEvent.read) {
        Datagram? datagram = socket!.receive();
        if (datagram != null) {
          String message = String.fromCharCodes(datagram.data);
          if (message.startsWith("AGRI3D_DISCOVERY:")) {
            String ip = message.substring(17).trim();
            if (!completer.isCompleted) {
              completer.complete(ip);
            }
          }
        }
      }
    });

    // Timeout after 3.5 seconds (beacon is sent every 3 seconds)
    String? discoveredIp = await completer.future.timeout(
      const Duration(milliseconds: 3500),
      onTimeout: () => null,
    );

    socket.close();

    if (discoveredIp != null && !service.isConnected) {
      service.addLog("SYS: Discovered ESP32 at $discoveredIp");
      await service.connectAndVerifyHost(discoveredIp);
    } else if (discoveredIp == null) {
      service.addLog("SYS: UDP discovery timeout.");
    }
  } catch (e) {
    service.addLog("SYS: UDP Error: $e");
    socket?.close();
  }
}
