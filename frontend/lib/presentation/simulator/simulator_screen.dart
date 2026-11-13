import 'package:flutter/material.dart';
import '../../core/theme.dart';
import '../../core/constants.dart';
import '../../application/vos_service.dart';
import '../../data/models/simulation_models.dart';
import '../../widgets/metric_card.dart';
import '../../widgets/status_badge.dart';
import '../../widgets/section_header.dart';
import '../../widgets/gantt_chart_widget.dart';
import '../../widgets/memory_map_widget.dart';

class SimulatorScreen extends StatefulWidget {
  final VosService service;

  const SimulatorScreen({super.key, required this.service});

  @override
  State<SimulatorScreen> createState() => _SimulatorScreenState();
}

class _SimulatorScreenState extends State<SimulatorScreen> with SingleTickerProviderStateMixin {
  late TabController _tabController;
  int _selectedTabIndex = 0;

  // --- CPU Sched State ---
  int _selectedSchedAlgo = VosConstants.schedAlgoAuto;
  final TextEditingController _quantumCtrl = TextEditingController(text: '2');
  List<SimProcess> _schedProcesses = [];

  // --- Memory State ---
  int _selectedMemStrategy = VosConstants.memStrategyAuto;
  final TextEditingController _blocksCtrl = TextEditingController(text: '100, 500, 200, 300, 600');
  List<SimProcess> _memProcesses = [];

  // --- Banker's State ---
  final TextEditingController _availableCtrl = TextEditingController(text: '3, 3, 2');
  List<SimProcess> _bankerProcesses = [];

  @override
  void initState() {
    super.initState();
    _tabController = TabController(length: 3, vsync: this);
    _tabController.addListener(() {
      if (_tabController.index != _selectedTabIndex && !_tabController.indexIsChanging) {
        setState(() => _selectedTabIndex = _tabController.index);
      }
    });
    _resetSchedProcesses();
    _resetMemProcesses();
    _resetBankerProcesses();
  }

  void _resetSchedProcesses() {
    _schedProcesses = [
      SimProcess(pid: 1, name: 'P1', arrival: 0, burst: 5, priority: 2),
      SimProcess(pid: 2, name: 'P2', arrival: 1, burst: 3, priority: 1),
      SimProcess(pid: 3, name: 'P3', arrival: 2, burst: 8, priority: 4),
      SimProcess(pid: 4, name: 'P4', arrival: 3, burst: 6, priority: 3),
    ];
  }

  void _resetMemProcesses() {
    _memProcesses = [
      SimProcess(pid: 1, name: 'Job-A', memKb: 212),
      SimProcess(pid: 2, name: 'Job-B', memKb: 417),
      SimProcess(pid: 3, name: 'Job-C', memKb: 112),
      SimProcess(pid: 4, name: 'Job-D', memKb: 426),
    ];
  }

  void _resetBankerProcesses() {
    _bankerProcesses = [
      SimProcess(pid: 0, name: 'P0', maxResources: [7, 5, 3], heldResources: [0, 1, 0]),
      SimProcess(pid: 1, name: 'P1', maxResources: [3, 2, 2], heldResources: [2, 0, 0]),
      SimProcess(pid: 2, name: 'P2', maxResources: [9, 0, 2], heldResources: [3, 0, 2]),
      SimProcess(pid: 3, name: 'P3', maxResources: [2, 2, 2], heldResources: [2, 1, 1]),
      SimProcess(pid: 4, name: 'P4', maxResources: [4, 3, 3], heldResources: [0, 0, 2]),
    ];
  }

  @override
  void dispose() {
    _tabController.dispose();
    _quantumCtrl.dispose();
    _blocksCtrl.dispose();
    _availableCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);

    return Scaffold(
      backgroundColor: colors.bg,
      body: Column(
        children: [
          // Banner: Isolated Simulation Notice
          Container(
            padding: const EdgeInsets.symmetric(
              horizontal: VeyloraSpacing.xxl,
              vertical: VeyloraSpacing.sm + 2,
            ),
            decoration: BoxDecoration(
              color: colors.card,
              border: Border(bottom: BorderSide(color: colors.border)),
            ),
            child: Row(
              children: [
                Container(
                  padding: const EdgeInsets.all(7),
                  decoration: BoxDecoration(
                    color: colors.primary.withValues(alpha: 0.12),
                    borderRadius: BorderRadius.circular(VeyloraRadius.sm),
                  ),
                  child: Icon(Icons.school_outlined, color: colors.primary, size: 18),
                ),
                const SizedBox(width: VeyloraSpacing.sm),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Theoretical Operating System Simulation Sandbox',
                        style: TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: colors.textPrimary),
                      ),
                      const SizedBox(height: 1),
                      Text(
                        'Executes internal pure C theoretical scheduling, memory partitioning, and deadlock algorithms. Segregated from live QEMU HVF and Linux guest scheduling.',
                        style: TextStyle(fontSize: 11, color: colors.textSecondary),
                      ),
                    ],
                  ),
                ),
                const StatusBadge(label: 'THEORETICAL SANDBOX', color: VosColors.info, isSmall: true),
              ],
            ),
          ),

          // Tab Bar
          Container(
            color: colors.surface,
            child: TabBar(
              controller: _tabController,
              indicatorColor: colors.primary,
              indicatorWeight: 2.5,
              labelColor: colors.primary,
              unselectedLabelColor: colors.textSecondary,
              labelStyle: const TextStyle(fontWeight: FontWeight.w600, fontSize: 13),
              onTap: (idx) => setState(() => _selectedTabIndex = idx),
              tabs: const [
                Tab(icon: Icon(Icons.access_time_rounded, size: 15), text: 'CPU Scheduling'),
                Tab(icon: Icon(Icons.memory_rounded, size: 15), text: 'Memory Allocation'),
                Tab(icon: Icon(Icons.lock_clock_rounded, size: 15), text: 'Banker\'s Algorithm'),
              ],
            ),
          ),

          // Tab Views
          Expanded(
            child: IndexedStack(
              index: _selectedTabIndex,
              children: [
                _buildCpuSchedulingTab(colors),
                _buildMemoryTab(colors),
                _buildBankersTab(colors),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // ==========================================
  // TAB 1: CPU SCHEDULING
  // ==========================================
  Widget _buildCpuSchedulingTab(VosColorPalette colors) {
    final res = widget.service.lastSchedResult;

    return SingleChildScrollView(
      padding: const EdgeInsets.symmetric(horizontal: 28, vertical: 24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Controls Card
          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: colors.card,
              borderRadius: BorderRadius.circular(10),
              border: Border.all(color: colors.border),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SectionHeader(
                  title: 'Scheduling Algorithm & Parameters',
                  subtitle: 'Select an algorithm or use AUTO to evaluate all algorithms concurrently',
                  icon: Icons.tune_rounded,
                  trailing: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      OutlinedButton.icon(
                        icon: const Icon(Icons.refresh_rounded, size: 14),
                        label: const Text('Reset Defaults'),
                        onPressed: () => setState(() => _resetSchedProcesses()),
                      ),
                      const SizedBox(width: 12),
                      ElevatedButton.icon(
                        icon: const Icon(Icons.play_arrow_rounded, size: 16),
                        label: const Text('Run CPU Simulation'),
                        onPressed: _runCpuSimulation,
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(
                      flex: 3,
                      child: DropdownButtonFormField<int>(
                        initialValue: _selectedSchedAlgo,
                        style: TextStyle(fontSize: 13, color: colors.textPrimary),
                        dropdownColor: colors.card,
                        decoration: const InputDecoration(
                          labelText: 'Algorithm',
                          prefixIcon: Icon(Icons.alt_route_rounded, size: 18),
                        ),
                        items: const [
                          DropdownMenuItem(value: VosConstants.schedAlgoAuto, child: Text('AUTO (Optimal Selector — Lowest TAT)')),
                          DropdownMenuItem(value: VosConstants.schedAlgoFcfs, child: Text('FCFS (First-Come First-Served)')),
                          DropdownMenuItem(value: VosConstants.schedAlgoSjf, child: Text('SJF (Shortest Job First — Non-Preemptive)')),
                          DropdownMenuItem(value: VosConstants.schedAlgoSrtf, child: Text('SRTF (Shortest Remaining Time First)')),
                          DropdownMenuItem(value: VosConstants.schedAlgoPrioNp, child: Text('Priority (Non-Preemptive)')),
                          DropdownMenuItem(value: VosConstants.schedAlgoPrioP, child: Text('Priority (Preemptive)')),
                          DropdownMenuItem(value: VosConstants.schedAlgoRr, child: Text('Round Robin')),
                        ],
                        onChanged: (val) {
                          if (val != null) setState(() => _selectedSchedAlgo = val);
                        },
                      ),
                    ),
                    if (_selectedSchedAlgo == VosConstants.schedAlgoRr || _selectedSchedAlgo == VosConstants.schedAlgoAuto) ...[
                      const SizedBox(width: 14),
                      Expanded(
                        flex: 1,
                        child: TextFormField(
                          controller: _quantumCtrl,
                          keyboardType: TextInputType.number,
                          style: TextStyle(fontSize: 13, color: colors.textPrimary),
                          decoration: const InputDecoration(
                            labelText: 'Quantum',
                            prefixIcon: Icon(Icons.timer_outlined, size: 18),
                          ),
                        ),
                      ),
                    ],
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),

          // Process Table Card
          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: colors.card,
              borderRadius: BorderRadius.circular(10),
              border: Border.all(color: colors.border),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SectionHeader(
                  title: 'Process Workload Definition',
                  subtitle: 'Specify arrival time, burst duration, and priority level for each process',
                  icon: Icons.list_alt_rounded,
                  trailing: ElevatedButton.icon(
                    style: ElevatedButton.styleFrom(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
                    ),
                    icon: const Icon(Icons.add, size: 14),
                    label: const Text('Add Process', style: TextStyle(fontSize: 12)),
                    onPressed: () {
                      setState(() {
                        final nextPid = _schedProcesses.length + 1;
                        _schedProcesses.add(SimProcess(
                          pid: nextPid,
                          name: 'P$nextPid',
                          arrival: nextPid - 1,
                          burst: 4,
                          priority: 1,
                        ));
                      });
                    },
                  ),
                ),
                Table(
                  columnWidths: const {
                    0: FlexColumnWidth(1),
                    1: FlexColumnWidth(2),
                    2: FlexColumnWidth(2),
                    3: FlexColumnWidth(2),
                    4: FlexColumnWidth(2),
                    5: FlexColumnWidth(1),
                  },
                  children: [
                    TableRow(
                      decoration: BoxDecoration(
                        color: colors.bg,
                        borderRadius: BorderRadius.circular(6),
                      ),
                      children: [
                        _th(colors, 'PID'),
                        _th(colors, 'Name'),
                        _th(colors, 'Arrival Time'),
                        _th(colors, 'Burst Time'),
                        _th(colors, 'Priority (1=High)'),
                        _th(colors, 'Remove', alignRight: true),
                      ],
                    ),
                    ..._schedProcesses.asMap().entries.map((entry) {
                      final idx = entry.key;
                      final p = entry.value;
                      return TableRow(
                        children: [
                          Padding(padding: const EdgeInsets.all(10), child: Text('${p.pid}', style: TextStyle(fontSize: 12, color: colors.textPrimary))),
                          Padding(
                            padding: const EdgeInsets.symmetric(horizontal: 6),
                            child: TextFormField(
                              initialValue: p.name,
                              style: TextStyle(fontSize: 12, color: colors.textPrimary),
                              decoration: const InputDecoration(border: InputBorder.none, isDense: true, filled: false),
                              onChanged: (val) {
                                _schedProcesses[idx] = SimProcess(pid: p.pid, name: val, arrival: p.arrival, burst: p.burst, priority: p.priority);
                              },
                            ),
                          ),
                          Padding(
                            padding: const EdgeInsets.symmetric(horizontal: 6),
                            child: TextFormField(
                              initialValue: '${p.arrival}',
                              keyboardType: TextInputType.number,
                              style: TextStyle(fontSize: 12, color: colors.textPrimary),
                              decoration: const InputDecoration(border: InputBorder.none, isDense: true, filled: false),
                              onChanged: (val) {
                                _schedProcesses[idx] = SimProcess(pid: p.pid, name: p.name, arrival: int.tryParse(val) ?? 0, burst: p.burst, priority: p.priority);
                              },
                            ),
                          ),
                          Padding(
                            padding: const EdgeInsets.symmetric(horizontal: 6),
                            child: TextFormField(
                              initialValue: '${p.burst}',
                              keyboardType: TextInputType.number,
                              style: TextStyle(fontSize: 12, color: colors.textPrimary),
                              decoration: const InputDecoration(border: InputBorder.none, isDense: true, filled: false),
                              onChanged: (val) {
                                _schedProcesses[idx] = SimProcess(pid: p.pid, name: p.name, arrival: p.arrival, burst: int.tryParse(val) ?? 1, priority: p.priority);
                              },
                            ),
                          ),
                          Padding(
                            padding: const EdgeInsets.symmetric(horizontal: 6),
                            child: TextFormField(
                              initialValue: '${p.priority}',
                              keyboardType: TextInputType.number,
                              style: TextStyle(fontSize: 12, color: colors.textPrimary),
                              decoration: const InputDecoration(border: InputBorder.none, isDense: true, filled: false),
                              onChanged: (val) {
                                _schedProcesses[idx] = SimProcess(pid: p.pid, name: p.name, arrival: p.arrival, burst: p.burst, priority: int.tryParse(val) ?? 1);
                              },
                            ),
                          ),
                          Padding(
                            padding: const EdgeInsets.all(4),
                            child: Align(
                              alignment: Alignment.centerRight,
                              child: IconButton(
                                icon: Icon(Icons.close_rounded, size: 16, color: colors.danger),
                                onPressed: () {
                                  if (_schedProcesses.length > 1) {
                                    setState(() => _schedProcesses.removeAt(idx));
                                  }
                                },
                              ),
                            ),
                          ),
                        ],
                      );
                    }),
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 24),

          // Simulation Results Display
          if (res != null) ...[
            if (res.reason.isNotEmpty)
              Container(
                margin: const EdgeInsets.only(bottom: 20),
                padding: const EdgeInsets.all(16),
                decoration: BoxDecoration(
                  color: colors.success.withValues(alpha: 0.1),
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(color: colors.success.withValues(alpha: 0.3)),
                ),
                child: Row(
                  children: [
                    Icon(Icons.auto_awesome_rounded, color: colors.success, size: 20),
                    const SizedBox(width: 12),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            'Optimal Algorithm Selected: ${res.algoName}',
                            style: TextStyle(fontWeight: FontWeight.bold, color: colors.success, fontSize: 13),
                          ),
                          const SizedBox(height: 2),
                          Text(res.reason, style: TextStyle(color: colors.textPrimary, fontSize: 12)),
                        ],
                      ),
                    ),
                  ],
                ),
              ),

            // Performance KPIs
            GridView.count(
              crossAxisCount: 4,
              crossAxisSpacing: 14,
              mainAxisSpacing: 14,
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              childAspectRatio: 1.8,
              children: [
                MetricCard(
                  title: 'CPU Utilization',
                  value: '${res.cpuUtil.toStringAsFixed(1)}%',
                  subtitle: 'Busy: ${res.busyTime} / Total: ${res.totalTime}',
                  icon: Icons.speed_rounded,
                  accentColor: colors.primary,
                ),
                MetricCard(
                  title: 'Avg Turnaround',
                  value: '${res.avgTat.toStringAsFixed(2)} ms',
                  subtitle: 'Throughput: ${res.throughput.toStringAsFixed(2)}/u',
                  icon: Icons.timelapse_rounded,
                  accentColor: colors.accent,
                ),
                MetricCard(
                  title: 'Avg Waiting Time',
                  value: '${res.avgWt.toStringAsFixed(2)} ms',
                  subtitle: 'Avg Response: ${res.avgRt.toStringAsFixed(2)} ms',
                  icon: Icons.hourglass_bottom_rounded,
                  accentColor: colors.warning,
                ),
                MetricCard(
                  title: 'Context Switches',
                  value: '${res.ctxSwitches}',
                  subtitle: 'Total Runtime: ${res.totalTime} units',
                  icon: Icons.swap_horiz_rounded,
                  accentColor: colors.info,
                ),
              ],
            ),
            const SizedBox(height: 20),

            // Gantt Chart Visualization
            Container(
              padding: const EdgeInsets.all(20),
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: BorderRadius.circular(10),
                border: Border.all(color: colors.border),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text('Execution Gantt Timeline (${res.algoName})',
                          style: TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: colors.textPrimary)),
                      Text('Total Units: ${res.totalTime}', style: TextStyle(fontSize: 12, color: colors.textMuted)),
                    ],
                  ),
                  const SizedBox(height: 16),
                  GanttChartWidget(segments: res.gantt, totalTime: res.totalTime),
                ],
              ),
            ),
            const SizedBox(height: 20),

            // Process Metrics Table
            Container(
              padding: const EdgeInsets.all(20),
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: BorderRadius.circular(10),
                border: Border.all(color: colors.border),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('Process Performance Ledger', style: TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: colors.textPrimary)),
                  const SizedBox(height: 12),
                  Table(
                    children: [
                      TableRow(
                        decoration: BoxDecoration(color: colors.bg, borderRadius: BorderRadius.circular(6)),
                        children: [
                          _th(colors, 'PID'),
                          _th(colors, 'Process Name'),
                          _th(colors, 'Completion Time'),
                          _th(colors, 'Turnaround Time (TAT)'),
                          _th(colors, 'Waiting Time (WT)'),
                          _th(colors, 'Response Time (RT)'),
                        ],
                      ),
                      ...res.processes.map((p) {
                        return TableRow(
                          children: [
                            Padding(padding: const EdgeInsets.all(10), child: Text('P${p.pid}', style: TextStyle(fontSize: 12, color: colors.textPrimary))),
                            Padding(padding: const EdgeInsets.all(10), child: Text(p.name, style: TextStyle(fontSize: 12, color: colors.textPrimary))),
                            Padding(padding: const EdgeInsets.all(10), child: Text('${p.completion}', style: TextStyle(fontSize: 12, color: colors.textSecondary))),
                            Padding(padding: const EdgeInsets.all(10), child: Text('${p.tat}', style: TextStyle(fontSize: 12, color: colors.textSecondary))),
                            Padding(padding: const EdgeInsets.all(10), child: Text('${p.wt}', style: TextStyle(fontSize: 12, color: colors.textSecondary))),
                            Padding(padding: const EdgeInsets.all(10), child: Text('${p.rt}', style: TextStyle(fontSize: 12, color: colors.textSecondary))),
                          ],
                        );
                      }),
                    ],
                  ),
                ],
              ),
            ),
          ],
        ],
      ),
    );
  }

  void _runCpuSimulation() {
    final quantum = int.tryParse(_quantumCtrl.text) ?? 2;
    widget.service.runSchedulerSimulation(
      algo: _selectedSchedAlgo,
      processes: _schedProcesses,
      quantum: quantum,
    );
    setState(() {});
  }

  // ==========================================
  // TAB 2: MEMORY MANAGEMENT
  // ==========================================
  Widget _buildMemoryTab(VosColorPalette colors) {
    final res = widget.service.lastMemResult;

    return SingleChildScrollView(
      padding: const EdgeInsets.symmetric(horizontal: 28, vertical: 24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: colors.card,
              borderRadius: BorderRadius.circular(10),
              border: Border.all(color: colors.border),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SectionHeader(
                  title: 'Memory Partition Configuration & Strategy',
                  subtitle: 'Specify static or dynamic block partitions and select allocation strategy',
                  icon: Icons.layers_rounded,
                  trailing: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      OutlinedButton.icon(
                        icon: const Icon(Icons.refresh_rounded, size: 14),
                        label: const Text('Reset Defaults'),
                        onPressed: () {
                          setState(() {
                            _blocksCtrl.text = '100, 500, 200, 300, 600';
                            _resetMemProcesses();
                          });
                        },
                      ),
                      const SizedBox(width: 12),
                      ElevatedButton.icon(
                        icon: const Icon(Icons.play_arrow_rounded, size: 16),
                        label: const Text('Run Memory Simulation'),
                        onPressed: _runMemSimulation,
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(
                      flex: 2,
                      child: DropdownButtonFormField<int>(
                        initialValue: _selectedMemStrategy,
                        dropdownColor: colors.card,
                        style: TextStyle(fontSize: 13, color: colors.textPrimary),
                        decoration: const InputDecoration(labelText: 'Strategy', prefixIcon: Icon(Icons.tune_rounded, size: 18)),
                        items: const [
                          DropdownMenuItem(value: VosConstants.memStrategyAuto, child: Text('AUTO (Lowest Fragmentation)')),
                          DropdownMenuItem(value: VosConstants.memStrategyFirst, child: Text('First Fit')),
                          DropdownMenuItem(value: VosConstants.memStrategyBest, child: Text('Best Fit')),
                          DropdownMenuItem(value: VosConstants.memStrategyWorst, child: Text('Worst Fit')),
                        ],
                        onChanged: (val) {
                          if (val != null) setState(() => _selectedMemStrategy = val);
                        },
                      ),
                    ),
                    const SizedBox(width: 14),
                    Expanded(
                      flex: 3,
                      child: TextFormField(
                        controller: _blocksCtrl,
                        style: TextStyle(fontSize: 13, color: colors.textPrimary),
                        decoration: const InputDecoration(
                          labelText: 'Memory Partitions (KB, comma-separated)',
                          prefixIcon: Icon(Icons.view_column_rounded, size: 18),
                        ),
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),

          // Job Requests Card
          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: colors.card,
              borderRadius: BorderRadius.circular(10),
              border: Border.all(color: colors.border),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SectionHeader(
                  title: 'Process Memory Request Workload',
                  subtitle: 'Process memory allocation requests to map into partition blocks',
                  icon: Icons.memory_rounded,
                  trailing: ElevatedButton.icon(
                    style: ElevatedButton.styleFrom(padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8)),
                    icon: const Icon(Icons.add, size: 14),
                    label: const Text('Add Job', style: TextStyle(fontSize: 12)),
                    onPressed: () {
                      setState(() {
                        final nextPid = _memProcesses.length + 1;
                        _memProcesses.add(SimProcess(pid: nextPid, name: 'Job-${String.fromCharCode(64 + nextPid)}', memKb: 150));
                      });
                    },
                  ),
                ),
                Table(
                  columnWidths: const {
                    0: FlexColumnWidth(1),
                    1: FlexColumnWidth(3),
                    2: FlexColumnWidth(3),
                    3: FlexColumnWidth(1),
                  },
                  children: [
                    TableRow(
                      decoration: BoxDecoration(color: colors.bg, borderRadius: BorderRadius.circular(6)),
                      children: [
                        _th(colors, 'PID'),
                        _th(colors, 'Process Name'),
                        _th(colors, 'Requested Memory (KB)'),
                        _th(colors, 'Remove', alignRight: true),
                      ],
                    ),
                    ..._memProcesses.asMap().entries.map((entry) {
                      final idx = entry.key;
                      final p = entry.value;
                      return TableRow(
                        children: [
                          Padding(padding: const EdgeInsets.all(10), child: Text('${p.pid}', style: TextStyle(fontSize: 12, color: colors.textPrimary))),
                          Padding(
                            padding: const EdgeInsets.symmetric(horizontal: 6),
                            child: TextFormField(
                              initialValue: p.name,
                              style: TextStyle(fontSize: 12, color: colors.textPrimary),
                              decoration: const InputDecoration(border: InputBorder.none, isDense: true, filled: false),
                              onChanged: (val) {
                                _memProcesses[idx] = SimProcess(pid: p.pid, name: val, memKb: p.memKb);
                              },
                            ),
                          ),
                          Padding(
                            padding: const EdgeInsets.symmetric(horizontal: 6),
                            child: TextFormField(
                              initialValue: '${p.memKb}',
                              keyboardType: TextInputType.number,
                              style: TextStyle(fontSize: 12, color: colors.textPrimary),
                              decoration: const InputDecoration(border: InputBorder.none, isDense: true, filled: false),
                              onChanged: (val) {
                                _memProcesses[idx] = SimProcess(pid: p.pid, name: p.name, memKb: int.tryParse(val) ?? 64);
                              },
                            ),
                          ),
                          Padding(
                            padding: const EdgeInsets.all(4),
                            child: Align(
                              alignment: Alignment.centerRight,
                              child: IconButton(
                                icon: Icon(Icons.close_rounded, size: 16, color: colors.danger),
                                onPressed: () {
                                  if (_memProcesses.length > 1) {
                                    setState(() => _memProcesses.removeAt(idx));
                                  }
                                },
                              ),
                            ),
                          ),
                        ],
                      );
                    }),
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),

          // Memory Results
          if (res != null) ...[
            Container(
              padding: const EdgeInsets.all(20),
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: BorderRadius.circular(10),
                border: Border.all(color: colors.border),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('Visual Memory Map (${res.strategyName})', style: TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: colors.textPrimary)),
                  const SizedBox(height: 16),
                  MemoryMapWidget(memResult: res),
                ],
              ),
            ),
            const SizedBox(height: 20),

            // Allocations Status Table
            Container(
              padding: const EdgeInsets.all(20),
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: BorderRadius.circular(10),
                border: Border.all(color: colors.border),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('Partition Block Allocations Ledger', style: TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: colors.textPrimary)),
                  const SizedBox(height: 12),
                  Table(
                    children: [
                      TableRow(
                        decoration: BoxDecoration(color: colors.bg, borderRadius: BorderRadius.circular(6)),
                        children: [
                          _th(colors, 'PID'),
                          _th(colors, 'Process Name'),
                          _th(colors, 'Requested'),
                          _th(colors, 'Block #'),
                          _th(colors, 'Block Size'),
                          _th(colors, 'Internal Frag'),
                          _th(colors, 'Status', alignRight: true),
                        ],
                      ),
                      ...res.allocations.map((alloc) {
                        return TableRow(
                          children: [
                            Padding(padding: const EdgeInsets.all(10), child: Text('P${alloc.pid}', style: TextStyle(fontSize: 12, color: colors.textPrimary))),
                            Padding(padding: const EdgeInsets.all(10), child: Text(alloc.name, style: TextStyle(fontSize: 12, color: colors.textPrimary))),
                            Padding(padding: const EdgeInsets.all(10), child: Text('${alloc.size} KB', style: TextStyle(fontSize: 12, color: colors.textSecondary))),
                            Padding(padding: const EdgeInsets.all(10), child: Text(alloc.allocated ? 'Block ${alloc.block}' : '-', style: TextStyle(fontSize: 12, color: colors.textSecondary))),
                            Padding(padding: const EdgeInsets.all(10), child: Text(alloc.allocated ? '${alloc.blockSize} KB' : '-', style: TextStyle(fontSize: 12, color: colors.textSecondary))),
                            Padding(padding: const EdgeInsets.all(10), child: Text(alloc.allocated ? '${alloc.frag} KB' : '-', style: TextStyle(fontSize: 12, color: colors.warning))),
                            Padding(
                              padding: const EdgeInsets.all(8),
                              child: Align(
                                alignment: Alignment.centerRight,
                                child: StatusBadge(
                                  label: alloc.allocated ? 'PLACED' : 'FAILED',
                                  color: alloc.allocated ? colors.success : colors.danger,
                                  isSmall: true,
                                ),
                              ),
                            ),
                          ],
                        );
                      }),
                    ],
                  ),
                ],
              ),
            ),
          ],
        ],
      ),
    );
  }

  void _runMemSimulation() {
    widget.service.runMemorySimulation(
      strategy: _selectedMemStrategy,
      processes: _memProcesses,
      blocksText: _blocksCtrl.text,
    );
    setState(() {});
  }

  // ==========================================
  // TAB 3: BANKER'S ALGORITHM
  // ==========================================
  Widget _buildBankersTab(VosColorPalette colors) {
    final res = widget.service.lastBankerResult;

    return SingleChildScrollView(
      padding: const EdgeInsets.symmetric(horizontal: 28, vertical: 24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: colors.card,
              borderRadius: BorderRadius.circular(10),
              border: Border.all(color: colors.border),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SectionHeader(
                  title: 'Available Resources Vector & Deadlock Avoidance',
                  subtitle: 'Banker\'s Algorithm verifies whether granting resources leads to an unsafe deadlock state',
                  icon: Icons.security_rounded,
                  trailing: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      OutlinedButton.icon(
                        icon: const Icon(Icons.refresh_rounded, size: 14),
                        label: const Text('Reset Defaults'),
                        onPressed: () {
                          setState(() {
                            _availableCtrl.text = '3, 3, 2';
                            _resetBankerProcesses();
                          });
                        },
                      ),
                      const SizedBox(width: 12),
                      ElevatedButton.icon(
                        icon: const Icon(Icons.shield_rounded, size: 16),
                        label: const Text('Evaluate Banker\'s Safety'),
                        onPressed: _runBankerSimulation,
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 12),
                TextFormField(
                  controller: _availableCtrl,
                  style: TextStyle(fontSize: 13, color: colors.textPrimary),
                  decoration: const InputDecoration(
                    labelText: 'Currently Available Resources Vector [R0, R1, R2...]',
                    prefixIcon: Icon(Icons.dataset_rounded, size: 18),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),

          // Processes Matrices Card
          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: colors.card,
              borderRadius: BorderRadius.circular(10),
              border: Border.all(color: colors.border),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('Process Allocation & Maximum Demand Matrices',
                    style: TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: colors.textPrimary)),
                const SizedBox(height: 4),
                Text('Comma-separated integers matching available resource vector dimensions',
                    style: TextStyle(fontSize: 11, color: colors.textSecondary)),
                const SizedBox(height: 14),
                Table(
                  columnWidths: const {
                    0: FlexColumnWidth(1),
                    1: FlexColumnWidth(2),
                    2: FlexColumnWidth(3),
                    3: FlexColumnWidth(3),
                    4: FlexColumnWidth(3),
                  },
                  children: [
                    TableRow(
                      decoration: BoxDecoration(color: colors.bg, borderRadius: BorderRadius.circular(6)),
                      children: [
                        _th(colors, 'PID'),
                        _th(colors, 'Name'),
                        _th(colors, 'Allocated [R0, R1, R2]'),
                        _th(colors, 'Maximum Demand'),
                        _th(colors, 'Current Need [Max-Alloc]'),
                      ],
                    ),
                    ..._bankerProcesses.asMap().entries.map((entry) {
                      final idx = entry.key;
                      final p = entry.value;

                      final need = <int>[];
                      for (int i = 0; i < p.maxResources.length; i++) {
                        final maxV = i < p.maxResources.length ? p.maxResources[i] : 0;
                        final heldV = i < p.heldResources.length ? p.heldResources[i] : 0;
                        need.add((maxV - heldV).clamp(0, 9999));
                      }

                      return TableRow(
                        children: [
                          Padding(padding: const EdgeInsets.all(10), child: Text('P${p.pid}', style: TextStyle(fontSize: 12, color: colors.textPrimary))),
                          Padding(padding: const EdgeInsets.all(10), child: Text(p.name, style: TextStyle(fontSize: 12, color: colors.textPrimary))),
                          Padding(
                            padding: const EdgeInsets.symmetric(horizontal: 6),
                            child: TextFormField(
                              initialValue: p.heldResources.join(', '),
                              style: TextStyle(fontSize: 12, color: colors.textPrimary),
                              decoration: const InputDecoration(border: InputBorder.none, isDense: true, filled: false),
                              onChanged: (val) {
                                final parts = val.split(',').map((e) => int.tryParse(e.trim()) ?? 0).toList();
                                _bankerProcesses[idx] = SimProcess(pid: p.pid, name: p.name, maxResources: p.maxResources, heldResources: parts);
                              },
                            ),
                          ),
                          Padding(
                            padding: const EdgeInsets.symmetric(horizontal: 6),
                            child: TextFormField(
                              initialValue: p.maxResources.join(', '),
                              style: TextStyle(fontSize: 12, color: colors.textPrimary),
                              decoration: const InputDecoration(border: InputBorder.none, isDense: true, filled: false),
                              onChanged: (val) {
                                final parts = val.split(',').map((e) => int.tryParse(e.trim()) ?? 0).toList();
                                _bankerProcesses[idx] = SimProcess(pid: p.pid, name: p.name, maxResources: parts, heldResources: p.heldResources);
                              },
                            ),
                          ),
                          Padding(
                            padding: const EdgeInsets.all(10),
                            child: Text(
                              '[${need.join(", ")}]',
                              style: TextStyle(color: colors.accent, fontWeight: FontWeight.bold, fontSize: 12),
                            ),
                          ),
                        ],
                      );
                    }),
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),

          // Results
          if (res != null) ...[
            Container(
              padding: const EdgeInsets.all(20),
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: BorderRadius.circular(10),
                border: Border.all(color: colors.border),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      StatusBadge(
                        label: res.safe ? 'SYSTEM IS IN A SAFE STATE' : 'DEADLOCK RISK: UNSAFE STATE',
                        color: res.safe ? colors.success : colors.danger,
                      ),
                      const SizedBox(width: 12),
                      Text(
                        res.valid ? 'Mathematical Proof Complete' : 'Vector Dimension Mismatch',
                        style: TextStyle(fontSize: 12, color: colors.textSecondary),
                      ),
                    ],
                  ),
                  const SizedBox(height: 14),
                  Text(res.message, style: TextStyle(fontSize: 13, color: colors.textPrimary)),
                  if (res.safe && res.sequence.isNotEmpty) ...[
                    const SizedBox(height: 16),
                    Text('Safe Execution Sequence:', style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: colors.textSecondary)),
                    const SizedBox(height: 10),
                    Wrap(
                      spacing: 8,
                      crossAxisAlignment: WrapCrossAlignment.center,
                      children: [
                        for (int i = 0; i < res.sequence.length; i++) ...[
                          Container(
                            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 7),
                            decoration: BoxDecoration(
                              color: colors.primary.withValues(alpha: 0.12),
                              borderRadius: BorderRadius.circular(6),
                              border: Border.all(color: colors.primary.withValues(alpha: 0.4)),
                            ),
                            child: Text(
                              'Process P${res.sequence[i]}',
                              style: TextStyle(fontWeight: FontWeight.bold, color: colors.primary, fontSize: 13),
                            ),
                          ),
                          if (i < res.sequence.length - 1)
                            Icon(Icons.arrow_forward_rounded, color: colors.textMuted, size: 14),
                        ],
                      ],
                    ),
                  ],
                ],
              ),
            ),
          ],
        ],
      ),
    );
  }

  void _runBankerSimulation() {
    widget.service.runBankersSimulation(
      processes: _bankerProcesses,
      availableText: _availableCtrl.text,
    );
    setState(() {});
  }

  Widget _th(VosColorPalette colors, String text, {bool alignRight = false}) {
    return Padding(
      padding: const EdgeInsets.all(10),
      child: Text(
        text,
        textAlign: alignRight ? TextAlign.right : TextAlign.left,
        style: TextStyle(fontSize: 11, fontWeight: FontWeight.w600, color: colors.textSecondary),
      ),
    );
  }
}
