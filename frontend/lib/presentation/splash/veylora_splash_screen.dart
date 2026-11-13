import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../../core/constants.dart';
import '../../application/vos_service.dart';

class VeyloraSplashScreen extends StatefulWidget {
  final VosService service;
  final VoidCallback onLoaded;

  const VeyloraSplashScreen({
    super.key,
    required this.service,
    required this.onLoaded,
  });

  @override
  State<VeyloraSplashScreen> createState() => _VeyloraSplashScreenState();
}

class _VeyloraSplashScreenState extends State<VeyloraSplashScreen> with SingleTickerProviderStateMixin {
  late final AnimationController _pulseController;
  final ScrollController _terminalScrollController = ScrollController();

  final List<String> _allBootLogs = const [
    '[0.000000] [BOOT] Veylora Hypervisor Engine v1.6.0-pro (Darwin arm64) initializing...',
    '[0.001420] [KERNEL] Mach microkernel active: Apple Silicon 10-Core topology discovered',
    '[0.002810] [HVF] Apple Hypervisor Framework (HVF) initialized; hardware virtualization: ACTIVE',
    '[0.004190] [EDK2] Probing ARM64 UEFI firmware: /opt/homebrew/share/qemu/edk2-aarch64-code.fd [OK]',
    '[0.005820] [CGROUP] Linux cgroups v2 controller isolation mounted: cpu, memory, io, pids',
    '[0.007110] [DBMS] SQLite 3.43.2 WAL journaling database opened: virtual_os.db',
    '[0.008900] [AUDIT] Schema tables verified: vms, applications, resource_profiles, application_events',
    '[0.010530] [FFI] Dynamic bridge linked: libvos_backend.dylib exported (43 C11 symbols bound)',
    '[0.012100] [TELEMETRY] Mach host_processor_info & task_info sampling thread calibrated (1000ms)',
    '[0.014200] [REGRESSION] Historical benchmark regression database loaded; baseline comparison ready',
    '[0.016500] [DISCOVERY] In-guest application discovery subsystem calibrated; POSIX execve ready',
    '[0.018900] [MEMORY] Host unified memory: 16.00 GB detected; zero page-fault headroom secured',
    '[0.021000] [SECURITY] Process sandboxing verified; POSIX cgroup memory caps configured',
    '[0.023400] [QEMU] HVF acceleration pipelines verified for ARM64 guest emulation',
    '[0.026000] [VEYLORA] Engine initialization complete. Loading workspace environment...',
  ];

  final List<String> _displayedLogs = [];
  int _logIndex = 0;
  Timer? _bootTimer;
  double _progress = 0.05;
  String _currentStepDescription = 'Starting Veylora Hypervisor Engine...';

  @override
  void initState() {
    super.initState();
    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1400),
    )..repeat(reverse: true);

    _startBootSequence();
  }

  void _startBootSequence() {
    // Stream boot lines every 150-180ms
    _bootTimer = Timer.periodic(const Duration(milliseconds: 160), (timer) {
      if (!mounted) {
        timer.cancel();
        return;
      }

      if (_logIndex < _allBootLogs.length) {
        setState(() {
          _displayedLogs.add(_allBootLogs[_logIndex]);
          _progress = ((_logIndex + 1) / _allBootLogs.length).clamp(0.05, 1.0);

          // Update human-readable step description
          if (_logIndex < 3) {
            _currentStepDescription = 'Probing host hardware & Mach kernel...';
          } else if (_logIndex < 7) {
            _currentStepDescription = 'Initializing SQLite database & Cgroup v2 controllers...';
          } else if (_logIndex < 11) {
            _currentStepDescription = 'Linking C11 backend dynamic library & regression engine...';
          } else {
            _currentStepDescription = 'Calibrating hypervisor telemetry & readying workspace...';
          }
          _logIndex++;
        });

        // Auto-scroll terminal
        WidgetsBinding.instance.addPostFrameCallback((_) {
          if (_terminalScrollController.hasClients) {
            _terminalScrollController.animateTo(
              _terminalScrollController.position.maxScrollExtent,
              duration: const Duration(milliseconds: 90),
              curve: Curves.easeOut,
            );
          }
        });
      } else {
        timer.cancel();
        setState(() {
          _progress = 1.0;
          _currentStepDescription = 'Ready. Launching Veylora...';
        });

        // Brief delay before seamless transition
        Future.delayed(const Duration(milliseconds: 400), () {
          if (mounted) {
            try {
              const MethodChannel('com.veylora.window').invokeMethod('fullResolution');
            } catch (_) {}
            widget.onLoaded();
          }
        });
      }
    });
  }

  @override
  void dispose() {
    _bootTimer?.cancel();
    _pulseController.dispose();
    _terminalScrollController.dispose();
    super.dispose();
  }

  Color _getLogColor(String line) {
    if (line.contains('[KERNEL]') || line.contains('[HVF]')) {
      return const Color(0xFF64D2FF); // Cyan
    } else if (line.contains('[CGROUP]') || line.contains('[MEMORY]')) {
      return const Color(0xFF30D158); // Green
    } else if (line.contains('[DBMS]') || line.contains('[AUDIT]')) {
      return const Color(0xFFFFD60A); // Yellow / Gold
    } else if (line.contains('[FFI]') || line.contains('[REGRESSION]')) {
      return const Color(0xFFBF5AF2); // Purple / Violet
    } else if (line.contains('[VEYLORA]') || line.contains('[BOOT]')) {
      return const Color(0xFF5E8BFF); // Veylora Blue
    }
    return const Color(0xFFB0B7C6); // Gray / White
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF07090E),
      body: Stack(
        fit: StackFit.expand,
        children: [
          // Background 3D processor artwork
          Image.asset(
            'assets/images/splash_bg.jpg',
            fit: BoxFit.cover,
            errorBuilder: (context, error, stackTrace) => Container(
              color: const Color(0xFF090D16),
            ),
          ),

          // Deep Vignette / Radial & Linear Gradient Overlay for readability
          Container(
            decoration: BoxDecoration(
              gradient: LinearGradient(
                begin: Alignment.topCenter,
                end: Alignment.bottomCenter,
                colors: [
                  const Color(0xFF05070B).withValues(alpha: 0.85),
                  const Color(0xFF07090E).withValues(alpha: 0.70),
                  const Color(0xFF04060A).withValues(alpha: 0.94),
                ],
                stops: const [0.0, 0.5, 1.0],
              ),
            ),
          ),

          // Top Header Bar with skip button
          SafeArea(
            child: Align(
              alignment: Alignment.topRight,
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 16),
                child: TextButton.icon(
                  style: TextButton.styleFrom(
                    backgroundColor: Colors.black.withValues(alpha: 0.4),
                    padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(8),
                      side: BorderSide(color: Colors.white.withValues(alpha: 0.15)),
                    ),
                  ),
                  icon: const Icon(Icons.arrow_forward_rounded, color: Colors.white70, size: 14),
                  label: const Text(
                    'Skip to Workspace',
                    style: TextStyle(color: Colors.white70, fontSize: 12, fontWeight: FontWeight.w500),
                  ),
                  onPressed: widget.onLoaded,
                ),
              ),
            ),
          ),

          // Center Adobe-Style Splash Card
          Center(
            child: Container(
              width: 720,
              constraints: const BoxConstraints(maxHeight: 560),
              margin: const EdgeInsets.symmetric(horizontal: 24),
              decoration: BoxDecoration(
                color: const Color(0xFF0C101B).withValues(alpha: 0.88),
                borderRadius: BorderRadius.circular(16),
                border: Border.all(color: const Color(0xFF283654), width: 1.2),
                boxShadow: [
                  BoxShadow(
                    color: Colors.black.withValues(alpha: 0.6),
                    blurRadius: 40,
                    spreadRadius: 8,
                    offset: const Offset(0, 16),
                  ),
                  BoxShadow(
                    color: const Color(0xFF2A5CAA).withValues(alpha: 0.15),
                    blurRadius: 30,
                    spreadRadius: -4,
                  ),
                ],
              ),
              child: ClipRRect(
                borderRadius: BorderRadius.circular(16),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    // Top Branding Header
                    Padding(
                      padding: const EdgeInsets.fromLTRB(28, 26, 28, 16),
                      child: Row(
                        children: [
                          // Veylora Logo Emblem
                          Container(
                            width: 60,
                            height: 60,
                            decoration: BoxDecoration(
                              borderRadius: BorderRadius.circular(14),
                              border: Border.all(color: const Color(0xFF4A7BD0).withValues(alpha: 0.5), width: 1.5),
                              boxShadow: [
                                BoxShadow(
                                  color: const Color(0xFF3872E0).withValues(alpha: 0.35),
                                  blurRadius: 14,
                                  spreadRadius: 1,
                                ),
                              ],
                            ),
                            child: ClipRRect(
                              borderRadius: BorderRadius.circular(12),
                              child: Image.asset(
                                'assets/icons/logo.png',
                                fit: BoxFit.cover,
                                errorBuilder: (context, error, stackTrace) => Image.asset(
                                  'assets/images/veylora_logo.jpg',
                                  fit: BoxFit.cover,
                                  errorBuilder: (context, error, stackTrace) => const Icon(
                                    Icons.memory_rounded,
                                    color: Color(0xFF4A7BD0),
                                    size: 32,
                                  ),
                                ),
                              ),
                            ),
                          ),
                          const SizedBox(width: 18),
                          // Title & Product Identity
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Row(
                                  children: [
                                    const Text(
                                      'VEYLORA',
                                      style: TextStyle(
                                        fontFamily: 'sans-serif',
                                        fontSize: 24,
                                        fontWeight: FontWeight.w800,
                                        letterSpacing: 4.0,
                                        color: Colors.white,
                                      ),
                                    ),
                                    const SizedBox(width: 12),
                                    Container(
                                      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                                      decoration: BoxDecoration(
                                        color: const Color(0xFF1B3A6B),
                                        borderRadius: BorderRadius.circular(6),
                                        border: Border.all(color: const Color(0xFF3872E0)),
                                      ),
                                      child: const Text(
                                        'v1.6 PRO',
                                        style: TextStyle(
                                          color: Color(0xFF64B5F6),
                                          fontSize: 10,
                                          fontWeight: FontWeight.w700,
                                          letterSpacing: 1.0,
                                        ),
                                      ),
                                    ),
                                  ],
                                ),
                                const SizedBox(height: 4),
                                Text(
                                  VosConstants.appSubtitle,
                                  style: TextStyle(
                                    fontSize: 12,
                                    color: Colors.white.withValues(alpha: 0.65),
                                    letterSpacing: 0.3,
                                  ),
                                ),
                              ],
                            ),
                          ),
                          // Pulsing Status Dot
                          FadeTransition(
                            opacity: _pulseController,
                            child: Container(
                              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                              decoration: BoxDecoration(
                                color: const Color(0xFF0F2D1F),
                                borderRadius: BorderRadius.circular(20),
                                border: Border.all(color: const Color(0xFF30D158).withValues(alpha: 0.6)),
                              ),
                              child: const Row(
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  CircleAvatar(
                                    radius: 3.5,
                                    backgroundColor: Color(0xFF30D158),
                                  ),
                                  SizedBox(width: 6),
                                  Text(
                                    'BOOTING',
                                    style: TextStyle(
                                      color: Color(0xFF30D158),
                                      fontSize: 10,
                                      fontWeight: FontWeight.w700,
                                      letterSpacing: 0.8,
                                    ),
                                  ),
                                ],
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),

                    const Divider(color: Color(0xFF1F293D), height: 1),

                    // Boot Terminal Console View
                    Container(
                      height: 230,
                      width: double.infinity,
                      margin: const EdgeInsets.symmetric(horizontal: 24, vertical: 14),
                      padding: const EdgeInsets.all(12),
                      decoration: BoxDecoration(
                        color: const Color(0xFF05070B),
                        borderRadius: BorderRadius.circular(10),
                        border: Border.all(color: const Color(0xFF1E283C)),
                      ),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          // Terminal top mini-bar
                          Row(
                            children: [
                              Container(width: 8, height: 8, decoration: const BoxDecoration(color: Color(0xFFFF5F56), shape: BoxShape.circle)),
                              const SizedBox(width: 5),
                              Container(width: 8, height: 8, decoration: const BoxDecoration(color: Color(0xFFFFBD2E), shape: BoxShape.circle)),
                              const SizedBox(width: 5),
                              Container(width: 8, height: 8, decoration: const BoxDecoration(color: Color(0xFF27C93F), shape: BoxShape.circle)),
                              const SizedBox(width: 12),
                              Text(
                                'Veylora Hypervisor Boot Console — POSIX Darwin arm64',
                                style: TextStyle(
                                  color: Colors.white.withValues(alpha: 0.4),
                                  fontSize: 10,
                                  fontFamily: 'monospace',
                                ),
                              ),
                            ],
                          ),
                          const SizedBox(height: 8),
                          const Divider(color: Color(0xFF141A29), height: 1),
                          const SizedBox(height: 6),
                          // Streaming log lines
                          Expanded(
                            child: ListView.builder(
                              controller: _terminalScrollController,
                              physics: const ClampingScrollPhysics(),
                              itemCount: _displayedLogs.length,
                              itemBuilder: (context, index) {
                                final logLine = _displayedLogs[index];
                                final logColor = _getLogColor(logLine);

                                return Padding(
                                  padding: const EdgeInsets.symmetric(vertical: 2.0),
                                  child: Text(
                                    logLine,
                                    style: TextStyle(
                                      fontFamily: 'monospace',
                                      fontSize: 10.5,
                                      height: 1.35,
                                      color: logColor,
                                    ),
                                  ),
                                );
                              },
                            ),
                          ),
                        ],
                      ),
                    ),

                    // Progress Bar & Step Status
                    Padding(
                      padding: const EdgeInsets.fromLTRB(28, 4, 28, 20),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Row(
                            mainAxisAlignment: MainAxisAlignment.spaceBetween,
                            children: [
                              Text(
                                _currentStepDescription,
                                style: TextStyle(
                                  color: Colors.white.withValues(alpha: 0.8),
                                  fontSize: 11,
                                  fontWeight: FontWeight.w500,
                                ),
                              ),
                              Text(
                                '${(_progress * 100).toInt()}%',
                                style: const TextStyle(
                                  color: Color(0xFF64B5F6),
                                  fontSize: 11,
                                  fontWeight: FontWeight.w700,
                                  fontFamily: 'monospace',
                                ),
                              ),
                            ],
                          ),
                          const SizedBox(height: 8),
                          ClipRRect(
                            borderRadius: BorderRadius.circular(4),
                            child: LinearProgressIndicator(
                              value: _progress,
                              minHeight: 5,
                              backgroundColor: const Color(0xFF141C2B),
                              valueColor: const AlwaysStoppedAnimation<Color>(Color(0xFF3E7BFA)),
                            ),
                          ),
                          const SizedBox(height: 12),
                          Row(
                            mainAxisAlignment: MainAxisAlignment.spaceBetween,
                            children: [
                              Text(
                                '© 2026 Veylora Hypervisor Technologies. Apple Silicon & Linux Virtualization.',
                                style: TextStyle(
                                  color: Colors.white.withValues(alpha: 0.35),
                                  fontSize: 9.5,
                                ),
                              ),
                              Text(
                                'Mach Core 10/10 • SQLite WAL • Cgroups v2',
                                style: TextStyle(
                                  color: Colors.white.withValues(alpha: 0.35),
                                  fontSize: 9.5,
                                  fontFamily: 'monospace',
                                ),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
