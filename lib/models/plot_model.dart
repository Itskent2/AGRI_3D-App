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

/// A single crop plant in the bed
class Plot {
  final int id;
  final String name;
  final double x; // Grid X position (mm)
  final double y; // Grid Y position (mm)
  final String type; // Fixed as 'crop'
  final double moisture; // Soil moisture percentage (0–100)
  final NpkLevel npk; // Current NPK reading
  final NpkLevel targetNpk; // Operator-set target NPK
  final bool aiDetected; // True if detected by AI automatically
  final double rosetteDiameter; // Diameter in mm for exclusion zone

  const Plot({
    required this.id,
    required this.name,
    required this.x,
    required this.y,
    this.type = 'crop',
    required this.moisture,
    required this.npk,
    required this.targetNpk,
    this.aiDetected = false,
    this.rosetteDiameter = 0.0,
  });

  /// standard Flutter method to update state without mutating the original object
  Plot copyWith({
    int? id,
    String? name,
    double? x,
    double? y,
    double? moisture,
    NpkLevel? npk,
    NpkLevel? targetNpk,
    bool? aiDetected,
    double? rosetteDiameter,
  }) {
    return Plot(
      id: id ?? this.id,
      name: name ?? this.name,
      x: x ?? this.x,
      y: y ?? this.y,
      moisture: moisture ?? this.moisture,
      npk: npk ?? this.npk,
      targetNpk: targetNpk ?? this.targetNpk,
      aiDetected: aiDetected ?? this.aiDetected,
      rosetteDiameter: rosetteDiameter ?? this.rosetteDiameter,
    );
  }
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