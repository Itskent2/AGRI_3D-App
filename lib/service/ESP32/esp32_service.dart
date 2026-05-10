import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'package:web_socket_channel/io.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'esp32_sensors.dart';

enum LogLevel {
  info,
  warn,
  error,
  success,
}

enum EnvironmentState {
  clear,
  rainSensor,
  weatherGated,
  rainAndWeather,
}

class LogEntry {
  final String message;
  final String tag;     // e.g. "NET", "SCAN", "FERT"
  final LogLevel level;
  final DateTime time;

  LogEntry({
    required this.message,
    required this.tag,
    this.level = LogLevel.info,
  }) : time = DateTime.now();

  @override
  String toString() {
    return "[$tag] $message";
  }
}

class ESP32Service extends ChangeNotifier {
  WebSocketChannel? _channel;
  bool isConnected = false;
  bool isScanning = false;
  String? _lastKnownIP;
  String? get currentIP => _lastKnownIP;
  List<LogEntry> logs = [];

  List<String> get logStrings => logs.map((e) => "[${e.tag}] ${e.message}").toList();

  // ── Security & Session ──
  static const String _authKey = "AGRI3D_SECURE_TOKEN_V1"; 
  static final String _sessionId = DateTime.now().millisecondsSinceEpoch
      .toString()
      .substring(7);
  Timer? _watchdogTimer;

  // ── Connection generation counter (prevents stale disconnect storms) ──
  int _connectionGen = 0;
  StreamSubscription? _channelSubscription;
  bool _isConnecting = false; 
  final Map<String, DateTime> _lastAttemptTimes = {}; 

  String? lastDisconnectReason; 

  Timer? _notifyDebounce;
  bool _notifyScheduled = false;

  // ── PING-PONG VARIABLES ──
  Timer? _pingTimer;
  int _missedPings = 0;
  int latencyMs = 0; 
  int pingCount = 0; 
  DateTime? _lastPingSentAt; 

  final ValueNotifier<Uint8List?> cameraFrame = ValueNotifier(null);
  DateTime _lastFrameUpdate = DateTime(0); 

  double x = 0, y = 0, z = 0;
  double maxX = 1000.0, maxY = 1000.0, maxZ = 1000.0;
  Map<String, String> grblSettings = {};
  double waterFlowRate = 10.0;
  double fertFlowRate = 10.0;

  String machineState = "Unknown";
  EnvironmentState environment = EnvironmentState.clear;
  bool nanoConnected = false;

  final ValueNotifier<double> jobProgress = ValueNotifier(0.0);

  bool hasStoredGcode = false;
  int storedGcodeSize = 0;
  bool isUploadingGcode = false;
  double uploadProgress = 0.0;
  Completer<void>? _uploadAckCompleter;

  Map<String, dynamic>? _pendingFrameMeta;

  final StreamController<Map<String, dynamic>> _plantCandidateCtrl = StreamController.broadcast();
  Stream<Map<String, dynamic>> get onPlantCandidate => _plantCandidateCtrl.stream;

  final StreamController<Map<String, dynamic>> _scanCompleteCtrl = StreamController.broadcast();
  Stream<Map<String, dynamic>> get onScanComplete => _scanCompleteCtrl.stream;

  final StreamController<Map<String, dynamic>> _frameCapturedCtrl = StreamController.broadcast();
  Stream<Map<String, dynamic>> get onFrameCaptured => _frameCapturedCtrl.stream;

  final StreamController<Map<String, dynamic>> _npkUpdateCtrl = StreamController.broadcast();
  Stream<Map<String, dynamic>> get onNpkUpdate => _npkUpdateCtrl.stream;

  static final ESP32Service instance = ESP32Service._internal();

  ESP32Service._internal() {
    autoDiscover(reason: "System Initialization");
  }

  Completer<void>? _discoveryCompleter;
  RawDatagramSocket? _udpSocket;

  Future<void> autoDiscover({String reason = "Unknown"}) async {
    if (isConnected) return;

    _startUdpDiscovery();

    if (_discoveryCompleter != null) return _discoveryCompleter!.future;

    addLog("🔍 mDNS Discovery (farmbot.local)", tag: "SYSTEM");
    _discoveryCompleter = Completer<void>();
    isScanning = true;
    _scheduleNotify();

    final prefs = await SharedPreferences.getInstance();
    _lastKnownIP = prefs.getString('lastKnownIP');

    final Set<String> uniqueHosts = {};
    if (_lastKnownIP != null) uniqueHosts.add(_lastKnownIP!);
    if (!kIsWeb) uniqueHosts.add("farmbot.local");
    uniqueHosts.add("192.168.4.1");

    for (final host in uniqueHosts) {
      if (isConnected) break;

      String hostReason = "Discovery Cycle";
      if (host == _lastKnownIP) hostReason = "Last Known IP";
      else if (host == "farmbot.local") hostReason = "mDNS (farmbot.local)";
      else if (host == "192.168.4.1") hostReason = "AP Fallback (Hotspot)";

      await _connectAndVerify(host, reason: hostReason);
    }

    isScanning = false;
    _scheduleNotify();

    final c = _discoveryCompleter;
    _discoveryCompleter = null;
    c?.complete();

    _watchdogTimer?.cancel();
    _watchdogTimer = Timer.periodic(const Duration(seconds: 30), (timer) {
      if (!isConnected && !isScanning && !_isConnecting) {
        autoDiscover(reason: "Watchdog");
      }
    });
  }

  void _startUdpDiscovery() async {
    if (kIsWeb || _udpSocket != null) return;

    try {
      _udpSocket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 4210);
      _udpSocket!.listen((event) {
        if (event == RawSocketEvent.read) {
          final datagram = _udpSocket!.receive();
          if (datagram == null) return;

          final message = utf8.decode(datagram.data);
          if (message.startsWith("AGRI3D_DISCOVERY:")) {
            final ip = message.substring(17).trim();
            if (!isConnected && !_isConnecting) {
              addLog("📡 UDP Beacon received: $ip", tag: "NET");
              _connectAndVerify(ip, reason: "UDP Discovery");
            }
          }
        }
      });
    } catch (e) {
      addLog("⚠ UDP Discovery Error: $e", tag: "NET", level: LogLevel.error);
    }
  }

  Future<void> _connectAndVerify(String host, {String reason = "Unknown"}) async {
    if (isConnected || _isConnecting) return;

    final nowTime = DateTime.now();
    final lastTime = _lastAttemptTimes[host];
    if (lastTime != null && nowTime.difference(lastTime).inSeconds < 2) {
      return;
    }
    _lastAttemptTimes[host] = nowTime;

    final int thisGen = ++_connectionGen;
    addLog("🔌 Connection attempt to $host (Gen: $thisGen, Reason: $reason)", tag: "NET");
    _isConnecting = true;
    final completer = Completer<bool>();

    try {
      _channelSubscription?.cancel();
      _channelSubscription = null;
      if (_channel != null) {
        addLog("♻ Cleaning up old channel before Gen $thisGen", tag: "NET");
        _channel!.sink.close(1000, "Switching to Gen $thisGen");
        _channel = null;
      }

      // ── ADD THE GEN PARAMETER TO THE URL ──
      final url = "ws://$host/ws?sid=$_sessionId&key=$_authKey&gen=$thisGen";
      addLog("SYS: Connecting to $url");

      // ── OS-LEVEL TIMEOUT ENFORCEMENT ──
      if (kIsWeb) {
        _channel = WebSocketChannel.connect(Uri.parse(url));
      } else {
        final client = HttpClient();
        client.connectionTimeout = const Duration(seconds: 3); // Fail fast!
        final ws = await WebSocket.connect(url, customClient: client);
        _channel = IOWebSocketChannel(ws);
      }

      if (isConnected || thisGen != _connectionGen) {
        addLog("🛡 Closing redundant connection (Gen: $thisGen vs $_connectionGen)", tag: "NET");
        _channel?.sink.close(1000, "Redundant connection abort");
        return;
      }

      bool identified = false;

      _channelSubscription = _channel!.stream.listen(
        (msg) async {
          _missedPings = 0;

          if (msg is List<int>) {
            if (!identified) return;
            final bytes = msg is Uint8List ? msg : Uint8List.fromList(msg);

            final now = DateTime.now();
            if (now.difference(_lastFrameUpdate).inMilliseconds >= 66) {
              _lastFrameUpdate = now;
              cameraFrame.value = bytes;
            }

            if (_pendingFrameMeta != null) {
              _pendingFrameMeta!['image'] = bytes; 
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
              addLog("✓ Online → $host (Gen: $thisGen)", tag: "NET", level: LogLevel.success);

              _lastKnownIP = host;
              SharedPreferences.getInstance().then((prefs) {
                prefs.setString('lastKnownIP', host);
              });

              _startPingLoop();
              sendCommand("GET_GCODE_INFO");

              try {
                final parsed = jsonDecode(textMsg);
                if (parsed['x'] != null) x = (parsed['x'] as num).toDouble();
                if (parsed['y'] != null) y = (parsed['y'] as num).toDouble();
                if (parsed['z'] != null) z = (parsed['z'] as num).toDouble();
                if (parsed['maxX'] != null) maxX = (parsed['maxX'] as num).toDouble();
                if (parsed['maxY'] != null) maxY = (parsed['maxY'] as num).toDouble();
                _scheduleNotify();
              } catch (_) {}
              if (!completer.isCompleted) completer.complete(true);
            }
            return;
          }
          // Try to decode as JSON (Telemetry/Events)
          Map<String, dynamic>? parsed;
          try {
            final decoded = jsonDecode(textMsg);
            if (decoded is Map<String, dynamic>) {
              parsed = decoded;
              // Update global sensor state
              ESP32Sensors.instance.updateSensorsFromJson(parsed);
            }
          } catch (e) {
            // Not JSON
          }

          if (textMsg.contains('"evt":"PONG"') || textMsg.contains('"status":"PONG"')) {
            if (_lastPingSentAt != null) {
              latencyMs = DateTime.now().difference(_lastPingSentAt!).inMilliseconds;
              _lastPingSentAt = null;
            }
            try {
              final pong = parsed ?? jsonDecode(textMsg);
              if (pong['ping_no'] != null) pingCount = (pong['ping_no'] as num).toInt();
            } catch (_) {}

            addLog("RX: PONG (Latency: ${latencyMs}ms)", tag: "PING");
            _scheduleNotify(); 
            return;
          }

          // Avoid logging frequent telemetry to console
          if (parsed == null || parsed['type'] != 'telemetry') {
            addLog(textMsg);
          }

          if (parsed != null) {
            if (parsed['evt'] == 'FRAME_META' || parsed['evt'] == 'DETECT_FRAME') {
              _pendingFrameMeta = parsed;
              return;
            }
            if (parsed['evt'] == 'PLANT_CANDIDATE') {
              _plantCandidateCtrl.add(parsed);
              _pendingFrameMeta = parsed; 
              return;
            }
            if (parsed['evt'] == 'DETECTION_COMPLETE') {
              _scanCompleteCtrl.add(parsed);
              return;
            }
            if (parsed['evt'] == 'NPK_N' || parsed['evt'] == 'NPK_P' || parsed['evt'] == 'NPK_K' || parsed['evt'] == 'NPK' || parsed['evt'] == 'NPK_LOG_CHUNK' || parsed['evt'] == 'NPK_LOG_END') {
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
              addLog("SD Stream Started (${parsed['file']})", tag: "SD");
              return;
            }
            if (parsed['evt'] == 'SD_COMPLETE') {
              jobProgress.value = 0.0;
              addLog("SD Stream Complete. (${parsed['lines']} lines)", tag: "SD", level: LogLevel.success);
              return;
            }
            if (parsed['evt'] == 'SD_STOPPED') {
              jobProgress.value = 0.0;
              addLog("SD Stream Stopped by User.", tag: "SD", level: LogLevel.warn);
              return;
            }

            if (parsed['evt'] == 'SYSTEM_STATE') {
              if (parsed['nano'] != null) {
                final wasConnected = nanoConnected;
                nanoConnected = parsed['nano'] == 'CONNECTED';
                if (nanoConnected != wasConnected) {
                  addLog(
                    nanoConnected ? "SYS: Nano (GRBL) connected." : "SYS: ⚠ Nano (GRBL) not detected — check Serial1 wiring.",
                    tag: "GRBL",
                    level: nanoConnected ? LogLevel.success : LogLevel.error,
                  );
                }
              }
              if (parsed['environment'] != null) {
                final env = parsed['environment'].toString();
                if (env == "RAIN_SENSOR") environment = EnvironmentState.rainSensor;
                else if (env == "WEATHER_GATED") environment = EnvironmentState.weatherGated;
                else if (env == "RAIN_AND_WEATHER") environment = EnvironmentState.rainAndWeather;
                else environment = EnvironmentState.clear;
              }
              if (parsed['x'] != null) x = (parsed['x'] as num).toDouble();
              if (parsed['y'] != null) y = (parsed['y'] as num).toDouble();
              if (parsed['z'] != null) z = (parsed['z'] as num).toDouble();
              if (parsed['w_rate'] != null) waterFlowRate = (parsed['w_rate'] as num).toDouble();
              if (parsed['f_rate'] != null) fertFlowRate = (parsed['f_rate'] as num).toDouble();
              _scheduleNotify();
              return; 
            }

            if (parsed['nano_raw'] != null) {
              String raw = parsed['nano_raw'].toString();
              _parseGrblStatus(raw);

              if (raw.startsWith("\$") && raw.contains("=")) {
                final parts = raw.split("=");
                if (parts.length == 2) {
                  grblSettings[parts[0]] = parts[1];
                  _scheduleNotify();
                }
              }

              if (raw.startsWith("\$130=")) { maxX = double.tryParse(raw.substring(5)) ?? maxX; _scheduleNotify(); }
              if (raw.startsWith("\$131=")) { maxY = double.tryParse(raw.substring(5)) ?? maxY; _scheduleNotify(); }
              if (raw.startsWith("\$132=")) { maxZ = double.tryParse(raw.substring(5)) ?? maxZ; _scheduleNotify(); }
            }
          }
        },
        onDone: () {
          if (!completer.isCompleted) completer.complete(false);
          if (thisGen == _connectionGen) {
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
      
      await completer.future.timeout(const Duration(seconds: 10));
    } catch (e) {
      _isConnecting = false;
      _handleDisconnect('Connection failed: $e');
    } finally {
      _isConnecting = false;
    }
  }

  void _parseGrblStatus(String raw) {
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

  String _describeDone(int? code, String? reason) {
    final parts = <String>[];
    if (code != null) {
      switch (code) {
        case 1000: parts.add('Normal closure'); break;
        case 1001: parts.add('Server going away'); break;
        case 1006: parts.add('Abnormal closure (no close frame received)'); break;
        case 1008: parts.add('Policy violation'); break;
        case 1011: parts.add('Server internal error'); break;
        default: parts.add('Close code $code');
      }
    } else {
      parts.add('Connection lost (no close code)');
    }
    if (reason != null && reason.isNotEmpty) parts.add('reason: $reason');
    return parts.join(' — ');
  }

  void _handleDisconnect([String? reason]) {
    if (!isConnected && _channel == null) return; 
    
    isConnected = false;
    nanoConnected = false; 
    _pingTimer?.cancel(); 
    _channelSubscription?.cancel();
    _channelSubscription = null;
    _channel = null;
    cameraFrame.value = null;

    lastDisconnectReason = reason ?? 'Unknown';
    addLog(
      "⚠ Disconnected → $lastDisconnectReason",
      tag: "NET",
      level: LogLevel.error,
    );
    _scheduleNotify();

    Future.delayed(const Duration(seconds: 5), () {
      if (!isConnected && !isScanning && !_isConnecting) {
        autoDiscover(reason: "Disconnect Recovery");
      }
    });
  }

  void _startPingLoop() {
    _pingTimer?.cancel();
    _missedPings = 0;

    _pingTimer = Timer.periodic(const Duration(seconds: 2), (timer) {
      if (!isConnected) {
        timer.cancel();
        return;
      }

      if (_missedPings >= 20) {
        _handleDisconnect(
          'Ping watchdog timeout ($_missedPings missed PONGs — ESP32 unresponsive)',
        );
        return;
      }

      _missedPings++; 
      _lastPingSentAt = DateTime.now(); 
      
      // Inject the generation into the ping!
      sendCommand('{"cmd":"PING", "gen": $_connectionGen}', tag: "PING"); 
    });
  }

  void sendCommand(String cmd, {String tag = "TX"}) {
    if (_channel != null && isConnected) {
      _channel!.sink.add(cmd);
      addLog(cmd, tag: tag);
    }
  }

  void requestGrblSettings() {
    sendCommand("\$\$");
  }

  void setFPM(int fpm) {
    sendCommand("SET_FPM:$fpm");
  }

  void startPhotoScan(int cols, int rows, double stepX, double stepY, double zHeight) {
    sendCommand("SCAN_PHOTO:$cols:$rows:${stepX.toStringAsFixed(1)}:${stepY.toStringAsFixed(1)}:${zHeight.toStringAsFixed(1)}");
  }

  void startAutoDetect(int cols, int rows, double stepX, double stepY, double zHeight) {
    sendCommand("AUTO_DETECT_PLANTS:$cols:$rows:${stepX.toStringAsFixed(1)}:${stepY.toStringAsFixed(1)}:${zHeight.toStringAsFixed(1)}");
  }

  void confirmPlant(double x, double y, String name) {
    sendCommand("CONFIRM_PLANT:${x.toStringAsFixed(1)}:${y.toStringAsFixed(1)}:$name");
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

  void addLog(String m, {String tag = "SYSTEM", LogLevel level = LogLevel.info}) {
    String cleanMsg = m;
    String finalTag = tag;
    LogLevel finalLevel = level;

    final prefixRegExp = RegExp(r'^([A-Z]+):\s*(.*)$');
    final prefixMatch = prefixRegExp.firstMatch(m);
    if (prefixMatch != null) {
      final possibleTag = prefixMatch.group(1);
      if (possibleTag == "SYS") {
        finalTag = "SYSTEM";
      } else {
        finalTag = possibleTag ?? tag;
      }
      cleanMsg = prefixMatch.group(2) ?? m;
    }

    final regExp = RegExp(r'^\[([A-Z]+)\]\[([A-Z]+)\]\s*(.*)$');
    final match = regExp.firstMatch(cleanMsg);

    if (match != null) {
      finalTag = match.group(1) ?? finalTag;
      final levelStr = match.group(2);
      cleanMsg = match.group(3) ?? cleanMsg;

      if (levelStr == "ERR") finalLevel = LogLevel.error;
      else if (levelStr == "OK") finalLevel = LogLevel.success;
      else if (levelStr == "WARN") finalLevel = LogLevel.warn;
      else finalLevel = LogLevel.info;
    } else if (m.startsWith("TX: ")) {
       finalTag = "TX";
       cleanMsg = m.substring(4);
    } else if (m.startsWith("RX: ")) {
       finalTag = "RX";
       cleanMsg = m.substring(4);
    }

    logs.add(LogEntry(message: cleanMsg, tag: finalTag, level: finalLevel));
    if (logs.length > 300) logs.removeAt(0); 
    _scheduleNotify();
  }

  void _scheduleNotify() {
    if (_notifyScheduled) return;
    _notifyScheduled = true;
    Timer.run(() {
      _notifyScheduled = false;
      if (hasListeners) {
        notifyListeners();
      }
    });
  }

  void clearLogs() {
    logs.clear();
    _scheduleNotify();
  }

  Future<void> executeGCode(List<String> lines) async {
    for (var line in lines) {
      if (!isConnected) break;
      line = line.trim();
      if (line.isEmpty || line.startsWith(';')) continue;

      sendCommand(line);
      await Future.delayed(const Duration(milliseconds: 100));
    }
  }

  Future<void> uploadGcodeChunked(String content) async {
    if (!isConnected) return;
    isUploadingGcode = true;
    uploadProgress = 0.0;
    _scheduleNotify();

    addLog("Starting G-Code Upload...", tag: "SD");
    sendCommand("UPLOAD_START");
    await Future.delayed(const Duration(milliseconds: 500));

    final lines = content.split('\n');
    int currentLine = 0;

    while (currentLine < lines.length && isUploadingGcode) {
      if (!isConnected) break;

      int endLine = currentLine + 50;
      if (endLine > lines.length) endLine = lines.length;

      String chunk = '${lines.sublist(currentLine, endLine).join('\n')}\n';

      _uploadAckCompleter = Completer<void>();
      sendCommand("UPLOAD_CHUNK:$chunk");

      try {
        await _uploadAckCompleter!.future.timeout(const Duration(seconds: 3));
      } catch (e) {
        addLog("Upload timeout! Retrying chunk...", tag: "SD", level: LogLevel.warn);
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
      addLog("Upload complete!", tag: "SD", level: LogLevel.success);
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
