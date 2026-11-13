import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:virtual_os_simulator/data/models/simulation_models.dart';
import 'package:virtual_os_simulator/widgets/status_badge.dart';
import 'package:virtual_os_simulator/widgets/metric_card.dart';
import 'package:virtual_os_simulator/widgets/gantt_chart_widget.dart';
import 'package:virtual_os_simulator/widgets/memory_map_widget.dart';

void main() {
  testWidgets('StatusBadge displays correct text and theme color', (
    WidgetTester tester,
  ) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(body: StatusBadge(label: 'RUNNING')),
      ),
    );

    expect(find.text('RUNNING'), findsOneWidget);
  });

  testWidgets('MetricCard renders title, value, and subtitle', (
    WidgetTester tester,
  ) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: MetricCard(
            title: 'Host CPU Utilization',
            value: '42.5%',
            subtitle: '10 Cores (Mach Kernel)',
            icon: Icons.speed,
          ),
        ),
      ),
    );

    expect(find.text('Host CPU Utilization'), findsOneWidget);
    expect(find.text('42.5%'), findsOneWidget);
    expect(find.text('10 Cores (Mach Kernel)'), findsOneWidget);
  });

  testWidgets('GanttChartWidget renders segments and legend', (
    WidgetTester tester,
  ) async {
    final segments = [
      GanttSegment(pid: 1, start: 0, end: 5),
      GanttSegment(pid: 2, start: 5, end: 8),
    ];

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: GanttChartWidget(segments: segments, totalTime: 8),
        ),
      ),
    );

    expect(find.text('Process P1'), findsOneWidget);
    expect(find.text('Process P2'), findsOneWidget);
  });

  testWidgets('MemoryMapWidget renders partition KPIs and allocations', (
    WidgetTester tester,
  ) async {
    final memResult = MemResultModel(
      strategy: 1,
      strategyName: 'First Fit',
      totalMem: 1700,
      usedMem: 629,
      freeMem: 1071,
      internalFrag: 71,
      failed: 0,
      allocations: [
        MemAllocation(
          pid: 1,
          name: 'Job-A',
          size: 212,
          block: 2,
          blockSize: 500,
          frag: 288,
          allocated: true,
        ),
      ],
    );

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: MemoryMapWidget(memResult: memResult)),
      ),
    );

    expect(find.text('Total Memory'), findsOneWidget);
    expect(find.text('1700 KB'), findsOneWidget);
    expect(find.text('Used Memory'), findsOneWidget);
    expect(find.text('629 KB'), findsOneWidget);
    expect(find.text('Process: Job-A'), findsOneWidget);
    expect(find.text('PLACED'), findsOneWidget);
  });
}
