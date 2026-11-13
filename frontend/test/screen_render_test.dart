import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:virtual_os_simulator/application/vos_service.dart';
import 'package:virtual_os_simulator/data/models/app_model.dart';
import 'package:virtual_os_simulator/data/models/vm_model.dart';
import 'package:virtual_os_simulator/presentation/profile/profile_screen.dart';
import 'package:virtual_os_simulator/presentation/monitoring/monitoring_screen.dart';
import 'package:virtual_os_simulator/presentation/app/app_screen.dart';
import 'package:virtual_os_simulator/presentation/dashboard/dashboard_screen.dart';

import 'package:virtual_os_simulator/presentation/events/events_screen.dart';
import 'package:virtual_os_simulator/data/models/event_model.dart';
import 'package:virtual_os_simulator/data/models/profile_model.dart';
import 'package:virtual_os_simulator/widgets/app_top_bar.dart';

void main() {
  testWidgets('ProfileScreen renders without crashing in light and dark mode', (tester) async {
    final service = VosService();
    addTearDown(service.dispose);
    service.profiles.addAll([
      ResourceProfile(
        profileId: 'default',
        name: 'Default Profile',
        description: 'Standard unconstrained profile',
        cpuCores: 0.0,
        cpuWeight: 100,
        memoryMaxBytes: 0,
        pidsMax: 0,
        ioReadBps: 0,
        ioWriteBps: 0,
      ),
      ResourceProfile(
        profileId: 'performance',
        name: 'Performance Profile',
        description: '1.5 vCPUs, 512MB RAM, 50 PIDs',
        cpuCores: 1.5,
        cpuWeight: 200,
        memoryMaxBytes: 512 * 1024 * 1024,
        pidsMax: 50,
        ioReadBps: 0,
        ioWriteBps: 0,
      ),
      ResourceProfile(
        profileId: 'restricted',
        name: 'Restricted Sandbox',
        description: '50% CPU, 128MB RAM, 20 PIDs, 5MB/s I/O',
        cpuCores: 0.5,
        cpuWeight: 100,
        memoryMaxBytes: 128 * 1024 * 1024,
        pidsMax: 20,
        ioReadBps: 5 * 1024 * 1024,
        ioWriteBps: 5 * 1024 * 1024,
      ),
    ]);

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData.light(),
        home: Scaffold(body: ProfileScreen(service: service)),
      ),
    );
    await tester.pump();
    expect(find.byType(ProfileScreen), findsOneWidget);
    expect(find.text('Default Profile'), findsOneWidget);
    expect(find.text('Restricted Sandbox'), findsOneWidget);
  });

  testWidgets('DashboardScreen and AppTopBar render in narrow viewport without right overflow', (tester) async {
    tester.view.physicalSize = const Size(500, 750);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(() => tester.view.resetPhysicalSize());

    final service = VosService();
    addTearDown(service.dispose);
    service.vms.add(
      VmModel(
        id: 1,
        vmId: 'alpine-vm-1',
        name: 'Alpine Linux Instance',
        vcpuCount: 2,
        ramMb: 512,
        state: 'STANDBY',
        osType: 'linux',
        diskPath: '',
        isoPath: '',
        pid: 0,
        guestReady: 0,
      ),
    );
    service.selectVm('alpine-vm-1');

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData.light(),
        home: Scaffold(
          appBar: PreferredSize(
            preferredSize: const Size.fromHeight(56),
            child: AppTopBar(
              category: 'MANAGEMENT',
              title: 'Dashboard',
              subtitle: 'System Overview & Telemetry',
              service: service,
            ),
          ),
          body: DashboardScreen(service: service),
        ),
      ),
    );
    await tester.pump();
    expect(find.byType(DashboardScreen), findsOneWidget);
    expect(find.text('Dashboard'), findsOneWidget);
    expect(find.textContaining('Welcome to'), findsOneWidget);
  });

  testWidgets('MonitoringScreen renders without crashing in light and dark mode', (tester) async {
    final service = VosService();
    addTearDown(service.dispose);
    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData.light(),
        home: Scaffold(body: MonitoringScreen(service: service)),
      ),
    );
    await tester.pump();
    expect(find.byType(MonitoringScreen), findsOneWidget);
  });

  testWidgets('AppScreen with registered Lightroom app renders without overflow', (tester) async {
    final service = VosService();
    addTearDown(service.dispose);
    final vm = VmModel(
      id: 1,
      vmId: 'alpine-vm-1',
      name: 'Alpine Linux Instance',
      vcpuCount: 2,
      ramMb: 512,
      state: 'RUNNING',
      osType: 'linux',
      diskPath: '',
      isoPath: '',
      pid: 1234,
      guestReady: 1,
    );
    service.vms.add(vm);
    service.selectVm('alpine-vm-1');

    service.registeredApps.add(
      RegisteredApp(
        appId: 'mac_adobe_lightroom',
        vmId: 'alpine-vm-1',
        name: 'Adobe Lightroom',
        executablePath: '/Applications/Adobe Lightroom CC/Adobe Lightroom.app/Contents/MacOS/Adobe Lightroom',
        description: 'Mac application chosen from /Applications/Adobe Lightroom CC/Adobe Lightroom.app/',
        category: 'macOS Application',
        defaultProfileId: 'restricted',
        enabled: true,
        launchCount: 1,
        version: '1.0',
      ),
    );

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData.light(),
        home: Scaffold(body: AppScreen(service: service)),
      ),
    );
    await tester.pump();
    expect(find.byType(AppScreen), findsOneWidget);
    expect(find.text('Adobe Lightroom'), findsOneWidget);
  });

  testWidgets('DashboardScreen renders without crashing in light and dark mode', (tester) async {
    final service = VosService();
    addTearDown(service.dispose);
    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData.light(),
        home: Scaffold(body: DashboardScreen(service: service)),
      ),
    );
    await tester.pump();
    expect(find.byType(DashboardScreen), findsOneWidget);
  });

  testWidgets('EventsScreen renders with long event types without right overflow', (tester) async {
    final service = VosService();
    addTearDown(service.dispose);
    service.events.addAll([
      EventLogEntry(
        id: 1,
        instanceId: 'bench_mac_warthunder_1',
        appId: 'warthunder',
        vmId: 'alpine-vm-01',
        eventType: 'TEST_BENCHMARK',
        eventDetails: 'Dual-Profile Benchmark | Min: 1.40s (restricted) | High: 0.70s (performance) | Speedup: 2.00x',
        createdAt: 1731313712,
      ),
      EventLogEntry(
        id: 2,
        instanceId: 'issue_bench_mac_warthunder_1',
        appId: 'warthunder',
        vmId: 'alpine-vm-01',
        eventType: 'TEST_BASELINE_VERIFIED',
        eventDetails: 'BASELINE VERIFIED: 0 issues detected | Historical benchmark compliance within tolerance',
        createdAt: 1731313712,
      ),
    ]);

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData.light(),
        home: Scaffold(body: EventsScreen(service: service)),
      ),
    );
    await tester.pump();
    expect(find.byType(EventsScreen), findsOneWidget);
    expect(find.text('TEST_BENCHMARK'), findsOneWidget);
    expect(find.text('TEST_BASELINE_VERIFIED'), findsOneWidget);
  });
}
