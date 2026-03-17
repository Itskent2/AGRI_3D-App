/// Event type for activity log entries
enum LogType { info, alert, success }

/// A single activity log entry from the gantry system
class LogEntry {
  final int id;
  final LogType type;
  final String message;
  final String time; // Format: "HH:mm:ss"
  final String user; // 'Auto' | 'System' | 'Operator' | 'Safety'

  const LogEntry({
    required this.id,
    required this.type,
    required this.message,
    required this.time,
    required this.user,
  });

  /// Helper for converting from JSON (useful for MQTT or WebSocket streams)
  factory LogEntry.fromJson(Map<String, dynamic> json) {
    return LogEntry(
      id: json['id'] as int,
      type: LogType.values.firstWhere(
        (e) => e.name == json['type'],
        orElse: () => LogType.info,
      ),
      message: json['message'] as String,
      time: json['time'] as String,
      user: json['user'] as String,
    );
  }
}

/// Mock activity log data
final List<LogEntry> logs = [
  const LogEntry(
    id: 1,
    type: LogType.info,
    message: 'Gantry homing sequence completed',
    time: '14:23:45',
    user: 'Auto',
  ),
  const LogEntry(
    id: 2,
    type: LogType.alert,
    message: 'Low moisture detected in Plot B-4',
    time: '14:20:12',
    user: 'System',
  ),
  const LogEntry(
    id: 3,
    type: LogType.success,
    message: 'Fertilizing task #442 finished successfully',
    time: '13:45:10',
    user: 'Operator',
  ),
  const LogEntry(
    id: 4,
    type: LogType.info,
    message: 'Manual control mode activated',
    time: '13:10:05',
    user: 'Operator',
  ),
  const LogEntry(
    id: 5,
    type: LogType.success,
    message: 'Watering Plot A-1 to A-6 completed',
    time: '12:30:22',
    user: 'Auto',
  ),
  const LogEntry(
    id: 6,
    type: LogType.alert,
    message: 'Obstruction detected at X:450, Y:120',
    time: '12:15:00',
    user: 'Safety',
  ),
  const LogEntry(
    id: 7,
    type: LogType.info,
    message: 'System startup: Version 2.4.5',
    time: '08:00:00',
    user: 'System',
  ),
];