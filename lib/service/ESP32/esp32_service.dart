import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'package:web_socket_channel/io.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:crypto/crypto.dart';
import '../../models/plot_model.dart';
import 'esp32_sensors.dart';

class NpkLogEntry {
  final int x, y, ts;
  final double n, p, k, m, ec, ph, temp;

  NpkLogEntry({
    required this.x, required this.y, required this.ts,
    required this.n, required this.p, required this.k, required this.m,
    required this.ec, required this.ph, required this.temp,
  });

  Map<String, dynamic> toJson() => {
    'x': x, 'y': y, 'ts': ts,
    'n': n, 'p': p, 'k': k, 'm': m,
    'ec': ec, 'ph': ph, 'temp': temp,
  };

  factory NpkLogEntry.fromJson(Map<String, dynamic> json) {
    int parsedX = (json['mmX'] as num?)?.toInt() ?? (json['x'] as num).toInt();
    int parsedY = (json['mmY'] as num?)?.toInt() ?? (json['y'] as num).toInt();
    
    // If we only got grid coordinates (0-4 for X, 0-2 for Y) from history, map them back to mm
    if (parsedX <= 10 && parsedY <= 10 && !json.containsKey('mmX')) {
      parsedX = parsedX * 250; // (1000 / (5 - 1))
      parsedY = parsedY * 500; // (1000 / (3 - 1))
    }

    return NpkLogEntry(
      x: parsedX,
      y: parsedY,
      ts: (json['ts'] as num).toInt(),
      n: (json['n'] as num).toDouble(),
      p: (json['p'] as num).toDouble(),
      k: (json['k'] as num).toDouble(),
      m: (json['m'] as num).toDouble(),
      ec: (json['ec'] ?? 0).toDouble(),
      ph: (json['ph'] ?? 0).toDouble(),
      temp: (json['temp'] ?? 0).toDouble(),
    );
  }
}

enum LogLevel { info, warn, error, success }

enum EnvironmentState { clear, rainSensor, weatherGated, rainAndWeather }

enum WifiState { disconnected, connecting, connected }

enum FlutterState { disconnected, connected }

enum NanoState { unknown, connected, unresponsive }

enum GrblState { unknown, idle, run, jog, home, hold, alarm, check, door }

enum OperationState {
  idle,
  homing,
  sdRunning,
  fertilizing,
  scanning,
  uploading,
  aiWeeding,
  npkDip,
  rainPaused,
  alarmRecovery,
}

class LogEntry {
  final String message;
  final String tag; // e.g. "NET", "SCAN", "FERT"
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
  List<NpkLogEntry> npkHistory = [];
  bool isLoadingHistory = true;

  // Accumulator for chunked plant map delivery
  final List<dynamic> _pendingPlants = [];

  List<String> get logStrings =>
      logs.map((e) => "[${e.tag}] ${e.message}").toList();

  // ── Security & Session ──
  static const String _authKey = "AGRI3D_SECURE_TOKEN_V1";
  Timer? _watchdogTimer;

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
  final ValueNotifier<List<Map<String, dynamic>>> aiDetections = ValueNotifier(
    [],
  );
  DateTime _lastFrameUpdate = DateTime(0);

  double x = 0, y = 0, z = 0;
  double maxX = 1000.0, maxY = 1000.0, maxZ = 1000.0;
  Map<String, String> grblSettings = {};
  double waterFlowRate = 10.0;
  double fertFlowRate = 10.0;
  int resolution = 1; // Default QQVGA
  double cameraOffset = 100.0; // Camera-to-gantry offset in mm (default 100 mm)

  String machineState = "Unknown";
  EnvironmentState environment = EnvironmentState.clear;
  bool nanoConnected = false;

  final ValueNotifier<double> jobProgress = ValueNotifier(0.0);

  bool hasStoredGcode = false;
  int storedGcodeSize = 0;
  bool isUploadingGcode = false;
  double uploadProgress = 0.0;
  Completer<void>? _uploadAckCompleter;

  // ── Plant Map Scan State ──
  /// true = SD scan complete, UPLOAD_SCAN available
  bool isScanReady = false;

  /// true = upload phase in progress (OP_UPLOADING on ESP32)
  bool isUploadingScan = false;

  /// 0.0–1.0 progress of Phase 1 (SD capture)
  double scanProgress = 0.0;
  int scanFrameIdx = 0;

  // Plant Registry
  List<Plot> registeredPlots = [];
  int scanFrameTotal = 0;

  /// 0.0–1.0 progress of Phase 2 (upload to Flutter)
  double uploadScanProgress = 0.0;

  Map<String, dynamic>? _pendingFrameMeta;

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

  static final ESP32Service instance = ESP32Service._internal();

  ESP32Service._internal() {
    _loadLocalNpkHistory();
    autoDiscover(reason: "System Initialization");
  }

  Future<void> _loadLocalNpkHistory() async {
    final prefs = await SharedPreferences.getInstance();
    final jsonStr = prefs.getString('npk_history');
    if (jsonStr != null) {
      try {
        final list = jsonDecode(jsonStr) as List;
        npkHistory = list.map((e) => NpkLogEntry.fromJson(e)).toList();
        _deduplicateAndSortHistory();
      } catch (e) {
        // Ignore parse errors
      }
    }
    isLoadingHistory = false;
    _scheduleNotify();
  }

  void _deduplicateAndSortHistory() {
    final seen = <int>{};
    final unique = <NpkLogEntry>[];
    for (var entry in npkHistory) {
      if (!seen.contains(entry.ts)) {
        seen.add(entry.ts);
        unique.add(entry);
      }
    }
    npkHistory.clear();
    npkHistory.addAll(unique);
    npkHistory.sort((a, b) => a.ts.compareTo(b.ts));
  }

  Future<void> _saveLocalNpkHistory() async {
    final prefs = await SharedPreferences.getInstance();
    final list = npkHistory.map((e) => e.toJson()).toList();
    await prefs.setString('npk_history', jsonEncode(list));
  }

  /// Converts a raw JSON list of plant objects into a typed [Plot] list.
  List<Plot> _parsePlotList(List<dynamic> list) {
    return list.map((p) {
      return Plot(
        id: (p['idx'] as num).toInt(),
        name: p['name']?.toString() ?? 'Plant',
        x: (p['x'] as num).toDouble(),
        y: (p['y'] as num).toDouble(),
        rosetteDiameter: (p['diameter'] as num?)?.toDouble() ?? 0.0,
        dx: (p['dx'] as num?)?.toDouble() ?? 0.0,
        dy: (p['dy'] as num?)?.toDouble() ?? 0.0,
        cropType: CropType.fromValue((p['c'] as num?)?.toInt() ?? 0),
        ts: (p['ts'] as num?)?.toInt() ?? 0,
        moisture: 0.0,
        npk: NpkLevel(
          n: (p['n'] as num?)?.toDouble() ?? 0.0,
          p: (p['p'] as num?)?.toDouble() ?? 0.0,
          k: (p['k'] as num?)?.toDouble() ?? 0.0,
        ),
        targetNpk: NpkLevel(
          n: (p['tN'] as num?)?.toDouble() ?? 0.0,
          p: (p['tP'] as num?)?.toDouble() ?? 0.0,
          k: (p['tK'] as num?)?.toDouble() ?? 0.0,
        ),
      );
    }).toList();
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
    uniqueHosts.add("192.168.0.115"); // Hardcoded ESP32 IP for Flutter Web

    for (final host in uniqueHosts) {
      if (isConnected) break;

      String hostReason = "Discovery Cycle";
      if (host == _lastKnownIP)
        hostReason = "Last Known IP";
      else if (host == "farmbot.local")
        hostReason = "mDNS (farmbot.local)";
      else if (host == "192.168.4.1")
        hostReason = "AP Fallback (Hotspot)";

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

  Future<void> _connectAndVerify(
    String host, {
    String reason = "Unknown",
  }) async {
    if (isConnected || _isConnecting) return;

    final nowTime = DateTime.now();
    final lastTime = _lastAttemptTimes[host];
    if (lastTime != null && nowTime.difference(lastTime).inSeconds < 2) {
      return;
    }
    _lastAttemptTimes[host] = nowTime;

    addLog("🔌 Connection attempt to $host (Reason: $reason)", tag: "NET");
    _isConnecting = true;
    final completer = Completer<bool>();

    try {
      _channelSubscription?.cancel();
      _channelSubscription = null;
      if (_channel != null) {
        addLog("♻ Cleaning up old channel", tag: "NET");
        _channel!.sink.close(1000, "Reconnecting");
        _channel = null;
      }

      // ── ADD THE GEN PARAMETER TO THE URL ──
      final url = "ws://$host/ws";
      addLog("SYS: Connecting to $url");

      // ── OS-LEVEL TIMEOUT ENFORCEMENT ──
      if (kIsWeb) {
        _channel = WebSocketChannel.connect(Uri.parse(url));
      } else {
        final client = HttpClient();
        client.connectionTimeout = const Duration(seconds: 3); // Fail fast!
        final ws = await WebSocket.connect(url, customClient: client);
        ws.pingInterval = const Duration(seconds: 5); // Buffer/keep-alive ping
        _channel = IOWebSocketChannel(ws);
      }

      if (isConnected) {
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

          Map<String, dynamic>? parsed;
          try {
            final decoded = jsonDecode(textMsg);
            if (decoded is Map<String, dynamic>) {
              parsed = decoded;
              // Update global sensor state
              ESP32Sensors.instance.updateSensorsFromJson(parsed);

              // FORCE LOG TO CONSOLE FOR DEBUGGING
              if (parsed['evt'] == 'NPK' || parsed.containsKey('n')) {
                print("🚨 NPK DATA RECEIVED: $textMsg");
              }
            }
          } catch (e) {
            // Not JSON
          }

          // ── Step 1: Security Handshake ──
          if (parsed != null && parsed['evt'] == 'CHALLENGE') {
            _respondToChallenge(parsed['nonce']);
            return;
          }

          // ── Step 2: Auth Success ──
          if (parsed != null && parsed['evt'] == 'AUTH_SUCCESS') {
            identified = true;
            isConnected = true;
            addLog(
              "✓ Securely Online → $host",
              tag: "NET",
              level: LogLevel.success,
            );

            _lastKnownIP = host;
            SharedPreferences.getInstance().then((prefs) {
              prefs.setString('lastKnownIP', host);
            });

            _startPingLoop();
            sendCommand("GET_GCODE_INFO");
            sendCommand("GET_PLANT_MAP"); // Fetch full plant registry on every connect
            if (!completer.isCompleted) completer.complete(true);
            return;
          }

          if (parsed != null && parsed['evt'] == 'AUTH_FAILED') {
            _handleDisconnect("Authentication Failed - Bad Token");
            return;
          }

          if (!identified) return;

          if (textMsg.contains('"evt":"PONG"') ||
              textMsg.contains('"status":"PONG"')) {
            if (_lastPingSentAt != null) {
              latencyMs = DateTime.now()
                  .difference(_lastPingSentAt!)
                  .inMilliseconds;
              _lastPingSentAt = null;
            }
            try {
              final pong = parsed ?? jsonDecode(textMsg);
              if (pong['ping_no'] != null)
                pingCount = (pong['ping_no'] as num).toInt();

              if (pong['plants'] != null) {
                final plantsList = pong['plants'] as List<dynamic>? ?? [];
                // Update basic plantMap if needed or registeredPlots
                // The provided JSON from ESP32 contains short keys: i, x, y, d
                // Let's integrate them into registeredPlots or a simple plantMap
                // For safety and minimal intrusion, we map it to ESP32Service's plot or plant structs
              }
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
            if (parsed['evt'] == 'AI_DETECTIONS') {
              aiDetections.value = List<Map<String, dynamic>>.from(
                parsed['detections'],
              );
              return;
            }

            if (parsed['evt'] == 'FRAME_META' ||
                parsed['evt'] == 'DETECT_FRAME') {
              _pendingFrameMeta = parsed;
              // Track upload progress during Phase 2
              if (isUploadingScan && scanFrameTotal > 0) {
                final idx = (parsed['idx'] as num?)?.toInt() ?? 0;
                uploadScanProgress = idx / scanFrameTotal;
                _scheduleNotify();
              }
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
            // ── Scan Phase 1: SD capture progress ──
            if (parsed['evt'] == 'SCAN_START') {
              isScanReady = false;
              isUploadingScan = false;
              scanProgress = 0.0;
              scanFrameIdx = 0;
              scanFrameTotal = (parsed['total'] as num?)?.toInt() ?? 0;
              if (parsed['maxX'] != null)
                maxX = (parsed['maxX'] as num).toDouble();
              if (parsed['maxY'] != null)
                maxY = (parsed['maxY'] as num).toDouble();
              addLog('Scan started: ${scanFrameTotal} frames', tag: 'SCAN');
              _scheduleNotify();
              return;
            }
            if (parsed['evt'] == 'SCAN_PROGRESS') {
              scanFrameIdx = (parsed['idx'] as num?)?.toInt() ?? scanFrameIdx;
              scanFrameTotal =
                  (parsed['total'] as num?)?.toInt() ?? scanFrameTotal;
              scanProgress = scanFrameTotal > 0
                  ? scanFrameIdx / scanFrameTotal
                  : 0.0;
              _scheduleNotify();
              return;
            }
            if (parsed['evt'] == 'SCAN_COMPLETE') {
              isScanReady =
                  parsed['ready'] == true && parsed['aborted'] != true;
              scanProgress = 1.0;
              addLog(
                isScanReady
                    ? 'Scan complete — ${parsed["total"]} frames on SD. Tap “Upload Plant Map”.'
                    : 'Scan aborted (${parsed["total"]} frames)',
                tag: 'SCAN',
                level: isScanReady ? LogLevel.success : LogLevel.warn,
              );
              _scheduleNotify();
              return;
            }
            // ── Scan Phase 2: upload progress ──
            if (parsed['evt'] == 'UPLOAD_SCAN_START') {
              isUploadingScan = true;
              uploadScanProgress = 0.0;
              scanFrameTotal = (parsed['total'] as num?)?.toInt() ?? 0;
              addLog(
                'Uploading plant map: $scanFrameTotal frames…',
                tag: 'SCAN',
              );
              _scheduleNotify();
              return;
            }
            if (parsed['evt'] == 'UPLOAD_SCAN_COMPLETE') {
              isUploadingScan = false;
              isScanReady = false; // reset — user can scan again
              uploadScanProgress = 1.0;
              addLog(
                'Plant map upload complete (${parsed["sent"]}/${parsed["total"]} frames)',
                tag: 'SCAN',
                level: LogLevel.success,
              );
              _scheduleNotify();
              return;
            }
            if (parsed['evt'] == 'UPLOAD_SCAN_ERROR') {
              isUploadingScan = false;
              addLog(
                'Upload error: ${parsed["reason"]}',
                tag: 'SCAN',
                level: LogLevel.error,
              );
              _scheduleNotify();
              return;
            }
            if (parsed['evt'] == 'NPK_N' ||
                parsed['evt'] == 'NPK_P' ||
                parsed['evt'] == 'NPK_K' ||
                parsed['evt'] == 'NPK' ||
                parsed['evt'] == 'NPK_LOG_CHUNK' ||
                parsed['evt'] == 'NPK_LOG_END') {
              
              if (parsed['evt'] == 'NPK_LOG_CHUNK') {
                final readings = parsed['readings'] as List;
                for (var r in readings) {
                  npkHistory.add(NpkLogEntry.fromJson(r));
                }
              } else if (parsed['evt'] == 'NPK_LOG_END') {
                _deduplicateAndSortHistory();
                _saveLocalNpkHistory();
                isLoadingHistory = false;
                _scheduleNotify();
              } else if (parsed['evt'] == 'NPK') {
                int ts = (parsed['ts'] as num?)?.toInt() ?? (DateTime.now().millisecondsSinceEpoch ~/ 1000);
                npkHistory.add(NpkLogEntry(
                    x: (parsed['x'] as num).toInt(),
                    y: (parsed['y'] as num).toInt(),
                    ts: ts,
                    n: (parsed['n'] as num).toDouble(),
                    p: (parsed['p'] as num).toDouble(),
                    k: (parsed['k'] as num).toDouble(),
                    m: (parsed['m'] ?? -1).toDouble(),
                    ec: (parsed['ec'] ?? 0).toDouble(),
                    ph: (parsed['ph'] ?? 0).toDouble(),
                    temp: (parsed['temp'] ?? 0).toDouble(),
                ));
                _deduplicateAndSortHistory();
                _saveLocalNpkHistory();
                _scheduleNotify();
              }
              
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
              addLog(
                "SD Stream Complete. (${parsed['lines']} lines)",
                tag: "SD",
                level: LogLevel.success,
              );
              return;
            }
            if (parsed['evt'] == 'SD_STOPPED') {
              jobProgress.value = 0.0;
              addLog(
                "SD Stream Stopped by User.",
                tag: "SD",
                level: LogLevel.warn,
              );
              return;
            }

            if (parsed['evt'] == 'SYSTEM_STATE') {
              if (parsed['nano'] != null) {
                final wasConnected = nanoConnected;
                final nanoInt = (parsed['nano'] as num).toInt();
                nanoConnected = (nanoInt == NanoState.connected.index);
                if (nanoConnected != wasConnected) {
                  addLog(
                    nanoConnected
                        ? 'SYS: Nano (GRBL) connected.'
                        : 'SYS: ⚠ Nano (GRBL) not detected — check Serial1 wiring.',
                    tag: 'GRBL',
                    level: nanoConnected ? LogLevel.success : LogLevel.error,
                  );
                }
              }
              if (parsed['environment'] != null) {
                final envInt = (parsed['environment'] as num).toInt();
                if (envInt >= 0 && envInt < EnvironmentState.values.length) {
                  environment = EnvironmentState.values[envInt];
                }
              }

              if (parsed['x'] != null) x = (parsed['x'] as num).toDouble();
              if (parsed['y'] != null) y = (parsed['y'] as num).toDouble();
              if (parsed['z'] != null) z = (parsed['z'] as num).toDouble();
              if (parsed['w_rate'] != null)
                waterFlowRate = (parsed['w_rate'] as num).toDouble();
              if (parsed['f_rate'] != null)
                fertFlowRate = (parsed['f_rate'] as num).toDouble();
              if (parsed['res'] != null)
                resolution = (parsed['res'] as num).toInt();
              if (parsed['scan_ready'] != null)
                isScanReady = parsed['scan_ready'] == true;
              if (parsed['cam_offset'] != null)
                cameraOffset = (parsed['cam_offset'] as num).toDouble();

              // Sync uploading state from operation integer
              if (parsed['operation'] != null) {
                final opInt = (parsed['operation'] as num).toInt();
                if (opInt == OperationState.uploading.index &&
                    !isUploadingScan) {
                  isUploadingScan = true;
                } else if (opInt != OperationState.uploading.index &&
                    isUploadingScan) {
                  // ESP32 finished uploading but we missed the UPLOAD_SCAN_COMPLETE
                  isUploadingScan = false;
                  uploadScanProgress = 1.0;
                }
              }

              _scheduleNotify();
              return;
            }

            // ── Legacy single-shot PLANT_MAP (small registries ≤~7 plants) ──
            if (parsed['evt'] == 'PLANT_MAP') {
              final List<dynamic> list = parsed['plants'] ?? [];
              registeredPlots = _parsePlotList(list);
              _scheduleNotify();
              return;
            }

            // ── Chunked plant map protocol ──
            if (parsed['evt'] == 'PLANT_MAP_START') {
              _pendingPlants.clear();
              return;
            }

            if (parsed['evt'] == 'PLANT_CHUNK') {
              final List<dynamic> chunk = parsed['plants'] ?? [];
              _pendingPlants.addAll(chunk);
              return;
            }

            if (parsed['evt'] == 'PLANT_MAP_END') {
              registeredPlots = _parsePlotList(_pendingPlants);
              _pendingPlants.clear();
              _scheduleNotify();
              return;
            }

            if (parsed['evt'] == 'PLANT_REGISTERED' ||
                parsed['evt'] == 'PLANT_DELETED' ||
                parsed['evt'] == 'PLANTS_CLEARED') {
              // Re-fetch entire map to ensure sync
              sendCommand("GET_PLANT_MAP");
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
          }
        },
        onDone: () {
          if (!completer.isCompleted) completer.complete(false);
          if (true) {
            final closeCode = _channel?.closeCode;
            final closeReason = _channel?.closeReason;
            _handleDisconnect(_describeDone(closeCode, closeReason));
          }
        },
        onError: (err) {
          if (!completer.isCompleted) completer.complete(false);
          if (true) {
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
        case 1000:
          parts.add('Normal closure');
          break;
        case 1001:
          parts.add('Server going away');
          break;
        case 1006:
          parts.add('Abnormal closure (no close frame received)');
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
    _channel?.sink.close(1000, "Client Disconnected");
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

  void _respondToChallenge(String nonce) {
    var key = utf8.encode(_authKey);
    var bytes = utf8.encode(nonce);
    var hmacSha256 = Hmac(sha256, key);
    var digest = hmacSha256.convert(bytes);

    addLog(
      "DEBUG: Sending AUTH response for nonce $nonce",
      tag: "AUTH",
      level: LogLevel.info,
    );
    sendCommand('{"cmd":"AUTH", "hash":"$digest"}');
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
      sendCommand('{"cmd":"PING"}', tag: "PING");
    });
  }

  void sendCommand(String cmd, {String tag = "TX"}) {
    if (_channel != null) {
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

  void setResolution(int res) {
    sendCommand("SET_RES:$res");
  }

  void startPhotoScan(
    int cols,
    int rows,
    double stepX,
    double stepY,
    double zHeight,
  ) {
    sendCommand(
      "SCAN_PLANT:$cols:$rows:${stepX.toStringAsFixed(1)}:${stepY.toStringAsFixed(1)}:${zHeight.toStringAsFixed(1)}",
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
      'AUTO_DETECT_PLANTS:$cols:$rows:${stepX.toStringAsFixed(1)}:${stepY.toStringAsFixed(1)}:${zHeight.toStringAsFixed(1)}',
    );
  }

  /// Trigger Phase 2: read SD frames and stream them to Flutter.
  void startScanUpload() {
    if (!isConnected || !isScanReady) return;
    isUploadingScan = true;
    uploadScanProgress = 0.0;
    _scheduleNotify();
    sendCommand('UPLOAD_SCAN');
  }

  void confirmPlant(double x, double y, String name) {
    sendCommand(
      "CONFIRM_PLANT:${x.toStringAsFixed(1)}:${y.toStringAsFixed(1)}:$name",
    );
  }

  void rejectPlant(double x, double y) {
    sendCommand("REJECT_PLANT:${x.toStringAsFixed(1)}:${y.toStringAsFixed(1)}");
  }

  void registerPlant(Plot plot) {
    // Send as JSON payload formatted as command
    final payload = jsonEncode(plot.toJson());
    sendCommand("REGISTER_PLANT:$payload");
  }

  void deletePlant(int idx) {
    sendCommand("DELETE_PLANT:$idx");
  }

  void clearPlants() {
    sendCommand("CLEAR_PLANTS");
  }

  /// Update the camera-to-gantry offset on the ESP32 and store locally.
  void setCamOffset(double mm) {
    cameraOffset = mm;
    sendCommand("SET_CAM_OFFSET:${mm.toStringAsFixed(1)}");
    _scheduleNotify();
  }

  void updatePos(String axis, double val, String gcode) {
    if (axis == 'x') x = val;
    if (axis == 'y') y = val;
    if (axis == 'z') z = val;
    sendCommand(gcode);
    _scheduleNotify();
  }

  void addLog(
    String m, {
    String tag = "SYSTEM",
    LogLevel level = LogLevel.info,
  }) {
    String cleanMsg = m;
    String finalTag = tag;
    LogLevel finalLevel = level;

    if (m.contains('"evt":"AI_DETECTIONS"')) {
      finalTag = "AI";
    }

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

      if (levelStr == "ERR")
        finalLevel = LogLevel.error;
      else if (levelStr == "OK")
        finalLevel = LogLevel.success;
      else if (levelStr == "WARN")
        finalLevel = LogLevel.warn;
      else
        finalLevel = LogLevel.info;
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
        addLog(
          "Upload timeout! Retrying chunk...",
          tag: "SD",
          level: LogLevel.warn,
        );
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
