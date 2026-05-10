/// NPK nutrient levels (Nitrogen, Phosphorus, Potassium)
class NpkLevel {
  final double n; // Nitrogen
  final double p; // Phosphorus
  final double k; // Potassium

  const NpkLevel({
    required this.n,
    required this.p,
    required this.k,
  });

  // Helper for JSON conversion if you connect to an API later
  factory NpkLevel.fromJson(Map<String, dynamic> json) => NpkLevel(
        n: (json['n'] as num).toDouble(),
        p: (json['p'] as num).toDouble(),
        k: (json['k'] as num).toDouble(),
      );

  Map<String, dynamic> toJson() => {'n': n, 'p': p, 'k': k};
}

enum CropType {
  none(0, 'None'),
  lettuce(1, 'Lettuce'),
  kangkong(2, 'Kangkong'),
  spinach(3, 'Spinach');

  final int value;
  final String label;
  const CropType(this.value, this.label);

  static CropType fromValue(int v) {
    return CropType.values.firstWhere((e) => e.value == v, orElse: () => CropType.none);
  }
}

/// A single crop plant in the bed
class Plot {
  final int id;
  final String name; // Keeping name for backward compatibility, but we can phase it out
  final double x; // Grid X position (mm)
  final double y; // Grid Y position (mm)
  final CropType cropType;
  final double moisture; // Soil moisture percentage (0–100)
  final NpkLevel npk; // Current NPK reading
  final NpkLevel targetNpk; // Operator-set target NPK
  final bool aiDetected; // True if detected by AI automatically
  final double rosetteDiameter; // Diameter in mm for exclusion zone
  final double dx; // Dipping coordinate X
  final double dy; // Dipping coordinate Y
  final int ts; // Timestamp of last check

  const Plot({
    required this.id,
    this.name = 'Plant',
    required this.x,
    required this.y,
    this.cropType = CropType.none,
    required this.moisture,
    required this.npk,
    required this.targetNpk,
    this.aiDetected = false,
    this.rosetteDiameter = 0.0,
    this.dx = 0.0,
    this.dy = 0.0,
    this.ts = 0,
  });

  /// standard Flutter method to update state without mutating the original object
  Plot copyWith({
    int? id,
    String? name,
    double? x,
    double? y,
    CropType? cropType,
    double? moisture,
    NpkLevel? npk,
    NpkLevel? targetNpk,
    bool? aiDetected,
    double? rosetteDiameter,
    double? dx,
    double? dy,
    int? ts,
  }) {
    return Plot(
      id: id ?? this.id,
      name: name ?? this.name,
      x: x ?? this.x,
      y: y ?? this.y,
      cropType: cropType ?? this.cropType,
      moisture: moisture ?? this.moisture,
      npk: npk ?? this.npk,
      targetNpk: targetNpk ?? this.targetNpk,
      aiDetected: aiDetected ?? this.aiDetected,
      rosetteDiameter: rosetteDiameter ?? this.rosetteDiameter,
      dx: dx ?? this.dx,
      dy: dy ?? this.dy,
      ts: ts ?? this.ts,
    );
  }

  // Optimized JSON conversion
  factory Plot.fromJson(Map<String, dynamic> json) {
    return Plot(
      id: json['id'] as int? ?? 0,
      x: (json['x'] as num?)?.toDouble() ?? 0.0,
      y: (json['y'] as num?)?.toDouble() ?? 0.0,
      rosetteDiameter: (json['r'] as num?)?.toDouble() ?? 0.0,
      dx: (json['dx'] as num?)?.toDouble() ?? 0.0,
      dy: (json['dy'] as num?)?.toDouble() ?? 0.0,
      cropType: CropType.fromValue(json['c'] as int? ?? 0),
      ts: json['ts'] as int? ?? 0,
      moisture: 0.0, // Default initialized, can be updated via sensor later
      npk: const NpkLevel(n: 0, p: 0, k: 0),
      targetNpk: const NpkLevel(n: 0, p: 0, k: 0),
    );
  }

  Map<String, dynamic> toJson() => {
        'id': id,
        'x': x,
        'y': y,
        'r': rosetteDiameter,
        'dx': dx,
        'dy': dy,
        'c': cropType.value,
        'ts': ts,
      };
}

/// Grid dimensions — 5 columns × 3 rows = 15 possible plot slots
const int gridCols = 5;
const int gridRows = 3;

/// Initial crop plot data
final List<Plot> initialPlots = [
  const Plot(
    id: 7,
    name: 'Lettuce',
    x: 300.0,
    y: 200.0,
    moisture: 60,
    npk: NpkLevel(n: 45, p: 30, k: 40),
    targetNpk: NpkLevel(n: 50, p: 35, k: 45),
  ),
  const Plot(
    id: 8,
    name: 'Tomato',
    x: 450.0,
    y: 350.0,
    moisture: 55,
    npk: NpkLevel(n: 38, p: 28, k: 35),
    targetNpk: NpkLevel(n: 60, p: 40, k: 50),
  ),
];