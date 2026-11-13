import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'core/theme.dart';
import 'core/constants.dart';
import 'application/vos_service.dart';
import 'widgets/navigation_sidebar.dart';
import 'widgets/app_top_bar.dart';
import 'presentation/splash/veylora_splash_screen.dart';
import 'presentation/dashboard/dashboard_screen.dart';
import 'presentation/vm/vm_screen.dart';
import 'presentation/app/app_screen.dart';
import 'presentation/profile/profile_screen.dart';
import 'presentation/monitoring/monitoring_screen.dart';
import 'presentation/simulator/simulator_screen.dart';
import 'presentation/events/events_screen.dart';
import 'presentation/settings/settings_screen.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const VirtualOsApp());
}

class VirtualOsApp extends StatefulWidget {
  const VirtualOsApp({super.key});

  @override
  State<VirtualOsApp> createState() => _VirtualOsAppState();
}

class _VirtualOsAppState extends State<VirtualOsApp> {
  late final VosService _service;
  bool _showSplash = true;

  @override
  void initState() {
    super.initState();
    _service = VosService();
    _service.initBackend();
  }

  @override
  void dispose() {
    _service.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: _service,
      builder: (context, _) {
        return MaterialApp(
          title: VosConstants.appTitle,
          debugShowCheckedModeBanner: false,
          themeMode: _service.themeMode,
          theme: VosTheme.lightTheme,
          darkTheme: VosTheme.darkTheme,
          home: AnimatedSwitcher(
            duration: const Duration(milliseconds: 500),
            switchInCurve: Curves.easeIn,
            switchOutCurve: Curves.easeOut,
            child: _showSplash
                ? VeyloraSplashScreen(
                    key: const ValueKey('splash_screen'),
                    service: _service,
                    onLoaded: () {
                      setState(() => _showSplash = false);
                      try {
                        const MethodChannel('com.veylora.window').invokeMethod('fullResolution');
                      } catch (_) {}
                    },
                  )
                : VirtualOsShell(
                    key: const ValueKey('workspace_shell'),
                    service: _service,
                    onReplaySplash: () => setState(() => _showSplash = true),
                  ),
          ),
        );
      },
    );
  }
}

class VirtualOsShell extends StatefulWidget {
  final VosService service;
  final VoidCallback? onReplaySplash;
  const VirtualOsShell({super.key, required this.service, this.onReplaySplash});

  @override
  State<VirtualOsShell> createState() => _VirtualOsShellState();
}

class _VirtualOsShellState extends State<VirtualOsShell> {
  int _selectedIndex = 0;

  String _getPageCategory(int index) {
    switch (index) {
      case 0:
        return 'OVERVIEW';
      case 1:
      case 2:
      case 3:
        return 'MANAGEMENT';
      case 4:
      case 6:
        return 'OBSERVABILITY';
      case 5:
        return 'TOOLS';
      case 7:
        return 'PREFERENCES';
      default:
        return 'VEYLORA';
    }
  }

  String _getPageTitle(int index) {
    switch (index) {
      case 0:
        return 'System Overview';
      case 1:
        return 'Virtual Machines';
      case 2:
        return 'Applications';
      case 3:
        return 'Resource Management';
      case 4:
        return 'Live Monitoring';
      case 5:
        return 'OS Algorithm Simulator';
      case 6:
        return 'Event Logs & Audit';
      case 7:
        return 'Settings & Diagnostics';
      default:
        return 'Veylora';
    }
  }

  String _getPageSubtitle(int index) {
    switch (index) {
      case 0:
        return 'Host telemetry & active workloads';
      case 1:
        return 'QEMU HVF hardware-accelerated guest management';
      case 2:
        return 'Lifecycle, interactive testing & benchmark suites';
      case 3:
        return 'Linux cgroups v2 resource controllers & limits';
      case 4:
        return 'Multi-layer host/QEMU/guest resource tracking';
      case 5:
        return 'Interactive CPU, memory & deadlock simulation';
      case 6:
        return 'Immutable SQLite audit trail & issue reports';
      case 7:
        return 'Theme preferences, C11 engine status & platform details';
      default:
        return '';
    }
  }

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);

    return Scaffold(
      backgroundColor: colors.bg,
      body: Row(
        children: [
          // Left Sidebar Navigation
          NavigationSidebar(
            selectedIndex: _selectedIndex,
            onDestinationSelected: (idx) {
              setState(() => _selectedIndex = idx);
            },
            service: widget.service,
          ),

          // Main View Content Area
          Expanded(
            child: Column(
              children: [
                // Top Global Bar
                AppTopBar(
                  category: _getPageCategory(_selectedIndex),
                  title: _getPageTitle(_selectedIndex),
                  subtitle: _getPageSubtitle(_selectedIndex),
                  service: widget.service,
                  onReplaySplash: widget.onReplaySplash,
                ),

                // Global Error Banner (if any)
                if (widget.service.lastError != null)
                  Container(
                    width: double.infinity,
                    padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 10),
                    color: colors.danger.withValues(alpha: 0.9),
                    child: Row(
                      children: [
                        const Icon(Icons.error_outline, color: Colors.white, size: 18),
                        const SizedBox(width: 10),
                        Expanded(
                          child: Text(
                            widget.service.lastError!,
                            style: const TextStyle(
                              color: Colors.white,
                              fontSize: 13,
                              fontWeight: FontWeight.w500,
                            ),
                          ),
                        ),
                        IconButton(
                          icon: const Icon(Icons.close, color: Colors.white, size: 16),
                          onPressed: () {
                            setState(() {
                              widget.service.refreshAll();
                            });
                          },
                        ),
                      ],
                    ),
                  ),

                // Screen Views Stack (8 views)
                Expanded(
                  child: IndexedStack(
                    index: _selectedIndex,
                    children: [
                      DashboardScreen(service: widget.service),
                      VmScreen(service: widget.service),
                      AppScreen(service: widget.service),
                      ProfileScreen(service: widget.service),
                      MonitoringScreen(service: widget.service),
                      SimulatorScreen(service: widget.service),
                      EventsScreen(service: widget.service),
                      SettingsScreen(service: widget.service),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
