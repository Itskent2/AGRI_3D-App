// lib/widgets/app_launcher_icon.dart

import 'dart:math';
import 'package:flutter/material.dart';

class AppLauncherIcon extends StatelessWidget {
  const AppLauncherIcon({super.key});

  @override
  Widget build(BuildContext context) {
    // This is the full background of your adaptive launcher icon
    return Container(
      width: 1024,
      height: 1024,
      color: const Color(0xFF111827), // Matches your dark theme
      child: Center(
        child: SizedBox(
          width: 700, // Provides a clean safe-zone margin
          height: 700,
          child: CustomPaint(
            painter: _FarmBotGantryIconPainter(),
          ),
        ),
      ),
    );
  }
}

class _FarmBotGantryIconPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = size.width / 2;

    // Use a clean gradient for the main icon elements
    final gradient = LinearGradient(
      begin: Alignment.topCenter,
      end: Alignment.bottomCenter,
      colors: [
        Colors.cyan[300]!, // e.g., for Water/IoT
        Colors.blue[600]!, // e.g., for Mechanics/Structure
      ],
    );

    final Paint mainPaint = Paint()
      ..shader = gradient.createShader(Rect.fromCircle(center: center, radius: radius))
      ..style = PaintingStyle.stroke
      ..strokeWidth = 40.0 // Bold stroke for high visibility
      ..strokeCap = StrokeCap.round;

    final Paint fillPaint = Paint()
      ..shader = gradient.createShader(Rect.fromCircle(center: center, radius: radius))
      ..style = PaintingStyle.fill;

    // 1. Draw the "Structure/Gantry" element: An upward-facing arch
    Path gantryPath = Path();
    gantryPath.moveTo(center.dx - 280, center.dy + 200); // Bottom left
    gantryPath.quadraticBezierTo(center.dx, center.dy - 350, center.dx + 280, center.dy + 200);
    canvas.drawPath(gantryPath, mainPaint);

    // 2. Draw the "Tool Head/Weeder" element: A central gear shape
    const double gearRadius = 140;
    canvas.drawCircle(center, gearRadius, mainPaint);
    
    // Draw the gear teeth
    final Paint gearPaint = Paint()
      ..color = Colors.blue[300]!
      ..style = PaintingStyle.fill;
      
    const int numTeeth = 8;
    for (int i = 0; i < numTeeth; i++) {
      final double angle = 2 * pi * i / numTeeth;
      final Offset toothCenter = Offset(
        center.dx + gearRadius * cos(angle),
        center.dy + gearRadius * sin(angle),
      );
      canvas.drawCircle(toothCenter, 30, gearPaint);
    }

    // 3. Draw the "Water Nozzle" element: A stylized downward spray arrow
    Path nozzlePath = Path();
    nozzlePath.moveTo(center.dx, center.dy + gearRadius + 20); // Stem start
    nozzlePath.lineTo(center.dx, center.dy + gearRadius + 180); // Stem end

    // The nozzle head (downward triangle)
    nozzlePath.moveTo(center.dx - 60, center.dy + gearRadius + 140);
    nozzlePath.lineTo(center.dx + 60, center.dy + gearRadius + 140);
    nozzlePath.lineTo(center.dx, center.dy + gearRadius + 220);
    nozzlePath.close();

    canvas.drawPath(nozzlePath, fillPaint);
    
    // Draw a dark center vein through the nozzle for contrast
    final Paint contrastveinPaint = Paint()
      ..color = const Color(0xFF111827) // Matching background color
      ..style = PaintingStyle.stroke
      ..strokeWidth = 15.0
      ..strokeCap = StrokeCap.round;
      
    canvas.drawLine(
      Offset(center.dx, center.dy + gearRadius + 50),
      Offset(center.dx, center.dy + gearRadius + 130),
      contrastveinPaint,
    );
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}