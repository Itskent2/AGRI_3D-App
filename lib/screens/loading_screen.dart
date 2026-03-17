import 'package:flutter/material.dart';
import 'dart:async';
import '../app.dart';

class AgriLoadingScreen extends StatefulWidget {
  const AgriLoadingScreen({super.key});

  @override
  State<AgriLoadingScreen> createState() => _AgriLoadingScreenState();
}

class _AgriLoadingScreenState extends State<AgriLoadingScreen>
    with TickerProviderStateMixin {

  // ── Controllers ──────────────────────────────────────────
  late AnimationController _xController;
  late AnimationController _zController;
  late AnimationController _pulseController;
  late AnimationController _progressController;
  late AnimationController _dotController;

  // ── Animations ───────────────────────────────────────────
  late Animation<double> _xMove;
  late Animation<double> _zMove;
  late Animation<double> _pulse;
  late Animation<double> _progress;

  String _status = 'BOOTING AGRI 3D CORE...';
  bool _showNozzleGlow = false;

  // Colors
  static const Color c2 = Color(0xFF323946);
  static const Color cyan = Color(0xFF00FFFF);

  @override
  void initState() {
    super.initState();

    // X-axis: slide tool head from left to center
    _xController = AnimationController(
      duration: const Duration(milliseconds: 1400),
      vsync: this,
    );
    _xMove = Tween<double>(begin: -120.0, end: 20.0).animate(
      CurvedAnimation(parent: _xController, curve: Curves.easeOutCubic),
    );

    // Z-axis: drop nozzle with bounce
    _zController = AnimationController(
      duration: const Duration(milliseconds: 900),
      vsync: this,
    );
    _zMove = Tween<double>(begin: 0.0, end: 18.0).animate(
      CurvedAnimation(parent: _zController, curve: Curves.bounceOut),
    );

    // Nozzle tip glow pulse
    _pulseController = AnimationController(
      duration: const Duration(milliseconds: 900),
      vsync: this,
    )..repeat(reverse: true);
    _pulse = Tween<double>(begin: 0.4, end: 1.0).animate(
      CurvedAnimation(parent: _pulseController, curve: Curves.easeInOut),
    );

    // Progress bar
    _progressController = AnimationController(
      duration: const Duration(milliseconds: 3600),
      vsync: this,
    );
    _progress = TweenSequence<double>([
      TweenSequenceItem(tween: Tween(begin: 0.0, end: 0.35), weight: 30),
      TweenSequenceItem(tween: Tween(begin: 0.35, end: 0.68), weight: 30),
      TweenSequenceItem(tween: Tween(begin: 0.68, end: 0.90), weight: 25),
      TweenSequenceItem(tween: Tween(begin: 0.90, end: 1.0),  weight: 15),
    ]).animate(_progressController);

    // Dot bounce
    _dotController = AnimationController(
      duration: const Duration(milliseconds: 1400),
      vsync: this,
    )..repeat();

    _runSequence();
  }

  Future<void> _runSequence() async {
    _progressController.forward();

    await Future.delayed(const Duration(milliseconds: 800));
    if (!mounted) return;
    setState(() => _status = 'SYNCING X-Y GANTRY...');
    await _xController.forward();

    if (!mounted) return;
    setState(() => _status = 'CALIBRATING Z-AXIS PROBE...');
    await _zController.forward();

    if (!mounted) return;
    setState(() {
      _status = 'HARDWARE SECURE';
      _showNozzleGlow = true;
    });

    await Future.delayed(const Duration(milliseconds: 700));
    if (!mounted) return;
    setState(() => _status = 'LAUNCHING DASHBOARD...');

    await Future.delayed(const Duration(milliseconds: 800));
    if (!mounted) return;

    Navigator.pushReplacement(
      context,
      PageRouteBuilder(
        pageBuilder: (c, a1, a2) => const FarmBotApp(),
        transitionsBuilder: (c, anim, a2, child) =>
            FadeTransition(opacity: anim, child: child),
        transitionDuration: const Duration(milliseconds: 1000),
      ),
    );
  }

  @override
  void dispose() {
    _xController.dispose();
    _zController.dispose();
    _pulseController.dispose();
    _progressController.dispose();
    _dotController.dispose();
    super.dispose();
  }

  // ── Dot bounce interval ───────────────────────────────────


  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: c2,
      body: Stack(
        children: [
          // ── Grid background ──
          Positioned.fill(child: _GridBackground()),

          // ── Main content ──
          Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                // ── Gantry scene ──
                _GantryScene(
                  xMove: _xMove,
                  zMove: _zMove,
                  pulse: _pulse,
                  showNozzleGlow: _showNozzleGlow,
                  xController: _xController,
                  zController: _zController,
                ),

                const SizedBox(height: 52),

                // ── Title ──
                const Text(
                  'AGRI 3D',
                  style: TextStyle(
                    color: Colors.white,
                    fontSize: 44,
                    fontWeight: FontWeight.w200,
                    letterSpacing: 14,
                  ),
                ),
                const SizedBox(height: 6),
                Text(
                  'PRECISION GANTRY SYSTEM',
                  style: TextStyle(
                    color: cyan.withOpacity(0.4),
                    fontSize: 8,
                    fontWeight: FontWeight.bold,
                    letterSpacing: 5,
                  ),
                ),

                const SizedBox(height: 28),

                // ── Status text ──
                _StatusText(status: _status),

                const SizedBox(height: 16),

                // ── Progress bar ──
                _ProgressBar(progress: _progress),

                const SizedBox(height: 52),

                // ── Dot spinner ──
                _DotSpinner(controller: _dotController),
              ],
            ),
          ),

          // ── Version tag ──
          const Positioned(
            bottom: 24,
            right: 24,
            child: Text(
              'v1.0.0 · ESP32 READY',
              style: TextStyle(
                color: Colors.white12,
                fontSize: 8,
                letterSpacing: 2,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

// ── Grid Background ───────────────────────────────────────────────────────────
class _GridBackground extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return CustomPaint(painter: _GridPainter());
  }
}

class _GridPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = const Color(0xFF00FFFF).withOpacity(0.03)
      ..strokeWidth = 1;
    const step = 40.0;
    for (double x = 0; x < size.width; x += step) {
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), paint);
    }
    for (double y = 0; y < size.height; y += step) {
      canvas.drawLine(Offset(0, y), Offset(size.width, y), paint);
    }
  }

  @override
  bool shouldRepaint(_GridPainter old) => false;
}

// ── Gantry Scene ──────────────────────────────────────────────────────────────
class _GantryScene extends StatelessWidget {
  final Animation<double> xMove;
  final Animation<double> zMove;
  final Animation<double> pulse;
  final bool showNozzleGlow;
  final AnimationController xController;
  final AnimationController zController;

  static const Color c1   = Color(0xFF19222B);
  static const Color c3   = Color(0xFF202A33);
  static const Color cyan = Color(0xFF00FFFF);

  const _GantryScene({
    required this.xMove,
    required this.zMove,
    required this.pulse,
    required this.showNozzleGlow,
    required this.xController,
    required this.zController,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 320,
      height: 200,
      child: Stack(
        children: [
          // Corner brackets
          _corner(top: 8, left: 8, topBorder: true, leftBorder: true),
          _corner(bottom: 8, right: 8, topBorder: false, leftBorder: false),

          // Rail
          Positioned(
            left: 20,
            right: 20,
            top: 0,
            bottom: 0,
            child: Center(
              child: Container(
                height: 8,
                decoration: BoxDecoration(
                  color: c3,
                  borderRadius: BorderRadius.circular(3),
                  border: Border.all(color: Colors.white10),
                  boxShadow: const [BoxShadow(color: Colors.black45, blurRadius: 6)],
                ),
              ),
            ),
          ),

          // Moving tool head
          AnimatedBuilder(
            animation: Listenable.merge([xController, zController]),
            builder: (context, _) {
              return Positioned(
                left: 100 + xMove.value,
                top: 60 + zMove.value,
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    // Tool body
                    Container(
                      width: 58,
                      height: 48,
                      decoration: BoxDecoration(
                        color: c1,
                        borderRadius: BorderRadius.circular(3),
                        border: Border.all(
                          color: cyan.withOpacity(0.35),
                          width: 2,
                        ),
                        boxShadow: [
                          BoxShadow(
                            color: cyan.withOpacity(0.12),
                            blurRadius: 16,
                          ),
                          const BoxShadow(
                            color: Colors.black54,
                            blurRadius: 10,
                            offset: Offset(0, 5),
                          ),
                        ],
                      ),
                      child: Stack(
                        children: [
                          // LED blink
                          Positioned(
                            top: 6, right: 6,
                            child: AnimatedBuilder(
                              animation: pulse,
                              builder: (_, __) => Opacity(
                                opacity: pulse.value,
                                child: Container(
                                  width: 5, height: 5,
                                  decoration: BoxDecoration(
                                    color: cyan,
                                    shape: BoxShape.circle,
                                    boxShadow: [
                                      BoxShadow(color: cyan.withOpacity(0.8), blurRadius: 6),
                                    ],
                                  ),
                                ),
                              ),
                            ),
                          ),
                          const Center(
                            child: Icon(
                              Icons.precision_manufacturing,
                              size: 22,
                              color: cyan,
                            ),
                          ),
                        ],
                      ),
                    ),

                    // Nozzle
                    Container(
                      width: 4,
                      height: 28,
                      decoration: BoxDecoration(
                        gradient: LinearGradient(
                          begin: Alignment.topCenter,
                          end: Alignment.bottomCenter,
                          colors: [cyan, cyan.withOpacity(0.3)],
                        ),
                        borderRadius: const BorderRadius.only(
                          bottomLeft: Radius.circular(2),
                          bottomRight: Radius.circular(2),
                        ),
                        boxShadow: [
                          BoxShadow(color: cyan.withOpacity(0.4), blurRadius: 6),
                        ],
                      ),
                    ),

                    // Nozzle tip glow
                    AnimatedBuilder(
                      animation: pulse,
                      builder: (_, __) => AnimatedOpacity(
                        opacity: showNozzleGlow ? pulse.value : 0.0,
                        duration: const Duration(milliseconds: 300),
                        child: Container(
                          width: 10, height: 10,
                          decoration: BoxDecoration(
                            color: cyan,
                            shape: BoxShape.circle,
                            boxShadow: [
                              BoxShadow(color: cyan.withOpacity(0.9), blurRadius: 16, spreadRadius: 2),
                            ],
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              );
            },
          ),
        ],
      ),
    );
  }

  Widget _corner({
    double? top, double? bottom, double? left, double? right,
    required bool topBorder, required bool leftBorder,
  }) {
    return Positioned(
      top: top, bottom: bottom, left: left, right: right,
      child: Container(
        width: 16, height: 16,
        decoration: BoxDecoration(
          border: Border(
            top:    topBorder  ? const BorderSide(color: Color(0xFF00FFFF), width: 2) : BorderSide.none,
            left:   leftBorder ? const BorderSide(color: Color(0xFF00FFFF), width: 2) : BorderSide.none,
            bottom: !topBorder  ? const BorderSide(color: Color(0xFF00FFFF), width: 2) : BorderSide.none,
            right:  !leftBorder ? const BorderSide(color: Color(0xFF00FFFF), width: 2) : BorderSide.none,
          ),
        ),
      ),
    );
  }
}

// ── Status Text ───────────────────────────────────────────────────────────────
class _StatusText extends StatelessWidget {
  final String status;
  const _StatusText({required this.status});

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          status,
          style: TextStyle(
            color: const Color(0xFF00FFFF).withOpacity(0.65),
            fontSize: 9,
            fontWeight: FontWeight.bold,
            letterSpacing: 3,
          ),
        ),
        _BlinkingCursor(),
      ],
    );
  }
}

class _BlinkingCursor extends StatefulWidget {
  @override
  State<_BlinkingCursor> createState() => _BlinkingCursorState();
}

class _BlinkingCursorState extends State<_BlinkingCursor>
    with SingleTickerProviderStateMixin {
  late AnimationController _c;

  @override
  void initState() {
    super.initState();
    _c = AnimationController(
      duration: const Duration(milliseconds: 700),
      vsync: this,
    )..repeat(reverse: true);
  }

  @override
  void dispose() { _c.dispose(); super.dispose(); }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: _c,
      builder: (_, __) => Opacity(
        opacity: _c.value > 0.5 ? 1.0 : 0.0,
        child: const Text(
          ' █',
          style: TextStyle(
            color: Color(0xFF00FFFF),
            fontSize: 9,
          ),
        ),
      ),
    );
  }
}

// ── Progress Bar ──────────────────────────────────────────────────────────────
class _ProgressBar extends StatelessWidget {
  final Animation<double> progress;
  const _ProgressBar({required this.progress});

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 280,
      child: AnimatedBuilder(
        animation: progress,
        builder: (_, __) => Column(
          children: [
            ClipRRect(
              borderRadius: BorderRadius.circular(1),
              child: LinearProgressIndicator(
                value: progress.value,
                minHeight: 2,
                backgroundColor: Colors.white.withOpacity(0.06),
                valueColor: const AlwaysStoppedAnimation<Color>(Color(0xFF00FFFF)),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ── Dot Spinner ───────────────────────────────────────────────────────────────
class _DotSpinner extends StatelessWidget {
  final AnimationController controller;
  const _DotSpinner({required this.controller});

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: controller,
      builder: (_, __) {
        return Row(
          mainAxisSize: MainAxisSize.min,
          children: List.generate(3, (i) {
            final t = ((controller.value * 3) - i).clamp(0.0, 1.0);
            final offset = t < 0.5
                ? -8.0 * (1 - (2 * t - 1) * (2 * t - 1))
                : 0.0;
            final glow = (t > 0.2 && t < 0.8);
            return Padding(
              padding: const EdgeInsets.symmetric(horizontal: 5),
              child: Transform.translate(
                offset: Offset(0, offset),
                child: Container(
                  width: 8, height: 8,
                  decoration: BoxDecoration(
                    color: glow
                        ? const Color(0xFF00FFFF).withOpacity(0.6)
                        : const Color(0xFF19222B),
                    shape: BoxShape.circle,
                    border: Border.all(
                      color: const Color(0xFF00FFFF).withOpacity(0.2),
                    ),
                    boxShadow: glow
                        ? [const BoxShadow(color: Color(0xFF00FFFF), blurRadius: 10)]
                        : null,
                  ),
                ),
              ),
            );
          }),
        );
      },
    );
  }
}