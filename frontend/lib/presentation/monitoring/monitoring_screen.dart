import 'package:flutter/material.dart';

import '../../core/theme.dart';
import '../../core/formatting.dart';
import '../../application/vos_service.dart';
import '../../widgets/metric_card.dart';
import '../../widgets/metric_chart.dart';
import '../../widgets/status_badge.dart';
import '../../widgets/section_header.dart';

class MonitoringScreen extends StatefulWidget {
  final VosService service;

  const MonitoringScreen({super.key, required this.service});

  @override
  State<MonitoringScreen> createState() => _MonitoringScreenState();
}

class _MonitoringScreenState extends State<MonitoringScreen>
    with SingleTickerProviderStateMixin {
  late TabController _tabController;
  int _selectedTabIndex = 0;
  String _selectedRange = '60s';

  @override
  void initState() {
    super.initState();
    _tabController = TabController(length: 3, vsync: this);
    _tabController.addListener(() {
      if (_tabController.index != _selectedTabIndex &&
          !_tabController.indexIsChanging) {
        setState(() => _selectedTabIndex = _tabController.index);
      }
    });
  }

  @override
  void dispose() {
    _tabController.dispose();
    super.dispose();
  }

  List<double> _filterData(List<double> original) {
    if (_selectedRange == '10s') {
      return original.length > 10
          ? original.sublist(original.length - 10)
          : original;
    } else if (_selectedRange == '30s') {
      return original.length > 30
          ? original.sublist(original.length - 30)
          : original;
    }
    return original; // 60s
  }

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);

    return Scaffold(
      backgroundColor: colors.bg,
      body: Column(
        children: [
          // Top Control Header: Title, Segmented Tabs, and Time-Range Window
          LayoutBuilder(
            builder: (context, constraints) {
              final isNarrow = constraints.maxWidth < 960;
              return Container(
                padding: const EdgeInsets.symmetric(
                  horizontal: VeyloraSpacing.xxl,
                  vertical: VeyloraSpacing.md,
                ),
                decoration: BoxDecoration(
                  color: colors.surface,
                  border: Border(bottom: BorderSide(color: colors.border)),
                ),
                child: isNarrow
                    ? Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          _buildTitleSection(colors),
                          const SizedBox(height: 10),
                          Wrap(
                            spacing: 10,
                            runSpacing: 8,
                            crossAxisAlignment: WrapCrossAlignment.center,
                            children: [
                              _buildTabSelector(colors),
                              _buildRangeSelector(colors),
                            ],
                          ),
                        ],
                      )
                    : Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Expanded(child: _buildTitleSection(colors)),
                          const SizedBox(width: 12),
                          Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              _buildTabSelector(colors),
                              const SizedBox(width: VeyloraSpacing.md),
                              _buildRangeSelector(colors),
                            ],
                          ),
                        ],
                      ),
              );
            },
          ),

          // Main Tab View Area
          Expanded(
            child: TabBarView(
              controller: _tabController,
              children: [
                _buildHostTab(context),
                _buildVmTab(context),
                _buildAppsTab(context),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildTitleSection(VosColorPalette colors) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              'Observability & Telemetry',
              style: TextStyle(
                fontSize: 18,
                fontWeight: FontWeight.w700,
                color: colors.textPrimary,
                letterSpacing: -0.4,
              ),
            ),
            const SizedBox(width: VeyloraSpacing.sm),
            Container(
              padding: const EdgeInsets.symmetric(
                horizontal: 7,
                vertical: 3,
              ),
              decoration: BoxDecoration(
                color: colors.success.withValues(alpha: 0.12),
                borderRadius: BorderRadius.circular(VeyloraRadius.xs),
              ),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Container(
                    width: 6,
                    height: 6,
                    decoration: BoxDecoration(
                      color: colors.success,
                      shape: BoxShape.circle,
                    ),
                  ),
                  const SizedBox(width: 5),
                  Text(
                    'SAMPLING 1Hz',
                    style: TextStyle(
                      fontSize: 10,
                      fontFamily: VeyloraTypography.fontMono,
                      fontWeight: FontWeight.w700,
                      color: colors.success,
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
        const SizedBox(height: 2),
        Text(
          'Multi-layer real-time telemetry · Mach kernel, hypervisor process, and Linux procfs',
          style: TextStyle(fontSize: 11.5, color: colors.textSecondary),
          overflow: TextOverflow.ellipsis,
          maxLines: 1,
        ),
      ],
    );
  }

  Widget _buildTabSelector(VosColorPalette colors) {
    return Container(
      padding: const EdgeInsets.all(3),
      decoration: BoxDecoration(
        color: colors.card,
        borderRadius: BorderRadius.circular(VeyloraRadius.md),
        border: Border.all(color: colors.border),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          _buildTabButton(0, 'Host System', Icons.laptop_mac_rounded),
          _buildTabButton(1, 'Virtual Machines', Icons.dns_rounded),
          _buildTabButton(2, 'Applications', Icons.apps_rounded),
        ],
      ),
    );
  }

  Widget _buildRangeSelector(VosColorPalette colors) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 3),
      decoration: BoxDecoration(
        color: colors.card,
        borderRadius: BorderRadius.circular(VeyloraRadius.md),
        border: Border.all(color: colors.border),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: ['10s', '30s', '60s'].map((range) {
          final isSel = _selectedRange == range;
          return GestureDetector(
            onTap: () => setState(() => _selectedRange = range),
            child: AnimatedContainer(
              duration: VeyloraMotion.fast,
              padding: const EdgeInsets.symmetric(
                horizontal: 10,
                vertical: 5,
              ),
              decoration: BoxDecoration(
                color: isSel ? colors.primary : Colors.transparent,
                borderRadius: BorderRadius.circular(VeyloraRadius.sm),
              ),
              child: Text(
                range,
                style: TextStyle(
                  fontSize: 11,
                  fontFamily: VeyloraTypography.fontMono,
                  fontWeight: isSel ? FontWeight.w700 : FontWeight.w500,
                  color: isSel ? Colors.white : colors.textSecondary,
                ),
              ),
            ),
          );
        }).toList(),
      ),
    );
  }

  Widget _buildTabButton(int index, String label, IconData icon) {
    final colors = VosColors.of(context);
    final isSelected = _selectedTabIndex == index;

    return GestureDetector(
      onTap: () {
        setState(() => _selectedTabIndex = index);
        _tabController.animateTo(index);
      },
      child: AnimatedContainer(
        duration: VeyloraMotion.fast,
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
        decoration: BoxDecoration(
          color: isSelected
              ? colors.primary.withValues(alpha: 0.15)
              : Colors.transparent,
          borderRadius: BorderRadius.circular(VeyloraRadius.sm),
        ),
        child: Row(
          children: [
            Icon(
              icon,
              size: 14,
              color: isSelected ? colors.primary : colors.textSecondary,
            ),
            const SizedBox(width: 6),
            Text(
              label,
              style: TextStyle(
                fontSize: 12,
                fontWeight: isSelected ? FontWeight.w600 : FontWeight.w500,
                color: isSelected ? colors.primary : colors.textSecondary,
              ),
            ),
          ],
        ),
      ),
    );
  }

  // TAB 1: Host Telemetry
  Widget _buildHostTab(BuildContext context) {
    final colors = VosColors.of(context);
    final host = widget.service.hostTelemetry;

    return SingleChildScrollView(
      padding: const EdgeInsets.all(VeyloraSpacing.xxl),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Banner
          _buildIntegrityBanner(
            context,
            title: 'Layer 1: Host macOS Mach Kernel Telemetry',
            description:
                'Samples Darwin mach_host_statistics64() and sysctl() directly from macOS kernel. Not affected by guest workload state.',
            sourceTag: 'MACH_HOST_STATISTICS (LIVE)',
            statusColor: colors.layerHost,
          ),
          const SizedBox(height: VeyloraSpacing.lg),

          // Top Metrics Grid
          LayoutBuilder(
            builder: (context, constraints) {
              final count = constraints.maxWidth > 900 ? 3 : 1;
              return GridView.count(
                crossAxisCount: count,
                crossAxisSpacing: VeyloraSpacing.md,
                mainAxisSpacing: VeyloraSpacing.md,
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                childAspectRatio: count == 3 ? 1.8 : 2.2,
                children: [
                  MetricCard(
                    title: 'Host CPU Utilization',
                    value: VosFormat.formatPercent(host.cpuUsagePct),
                    subtitle:
                        'User: ${VosFormat.formatPercent(host.cpuUserPct)} · Sys: ${VosFormat.formatPercent(host.cpuSystemPct)}',
                    icon: Icons.speed_rounded,
                    accentColor: colors.layerHost,
                    sparklineData: widget.service.hostCpuHistory,
                  ),
                  MetricCard(
                    title: 'Physical Memory (RAM)',
                    value: VosFormat.formatBytes(host.usedRamBytes),
                    subtitle:
                        'Free: ${VosFormat.formatBytes(host.freeRamBytes)} · Avail: ${VosFormat.formatBytes(host.availableRamBytes)}',
                    icon: Icons.storage_rounded,
                    accentColor: colors.layerHost,
                    sparklineData: widget.service.hostRamHistory,
                  ),
                  MetricCard(
                    title: 'System Load Average',
                    value:
                        '${host.loadAvg1m.toStringAsFixed(2)} / ${host.loadAvg5m.toStringAsFixed(2)}',
                    subtitle: '1-min / 5-min Run Queue Depth',
                    icon: Icons.show_chart_rounded,
                    accentColor: colors.layerHost,
                  ),
                ],
              );
            },
          ),
          const SizedBox(height: VeyloraSpacing.xl),

          // Live Time-Series Charts
          SectionHeader(
            title: 'Real-Time Host Performance Trends ($_selectedRange Window)',
            subtitle: 'Continuous 1Hz Mach kernel time-series with automatic peak detection',
            icon: Icons.trending_up_rounded,
          ),
          const SizedBox(height: VeyloraSpacing.xs),
          LayoutBuilder(
            builder: (context, constraints) {
              final isWide = constraints.maxWidth > 860;
              final cpuChart = MetricChart(
                title: 'Host CPU Utilization (%)',
                currentValue: VosFormat.formatPercent(host.cpuUsagePct),
                subtitle: 'Mach Processor Cores Aggregated Load',
                data: _filterData(widget.service.hostCpuHistory),
                accentColor: colors.layerHost,
                unit: '%',
                fixedMaxY: 100.0,
              );
              final ramChart = MetricChart(
                title: 'Physical RAM Allocation (MB)',
                currentValue: VosFormat.formatBytes(host.usedRamBytes),
                subtitle: 'Total Capacity: ${VosFormat.formatBytes(host.totalRamBytes)}',
                data: _filterData(widget.service.hostRamHistory),
                accentColor: colors.primary,
                unit: ' MB',
                fixedMaxY: (host.totalRamBytes / (1024 * 1024)),
              );

              if (isWide) {
                return Row(
                  children: [
                    Expanded(child: cpuChart),
                    const SizedBox(width: VeyloraSpacing.md),
                    Expanded(child: ramChart),
                  ],
                );
              } else {
                return Column(
                  children: [
                    cpuChart,
                    const SizedBox(height: VeyloraSpacing.md),
                    ramChart,
                  ],
                );
              }
            },
          ),
        ],
      ),
    );
  }

  // TAB 2: Virtual Machines
  Widget _buildVmTab(BuildContext context) {
    final colors = VosColors.of(context);
    final activeVm = widget.service.selectedVm;

    return SingleChildScrollView(
      padding: const EdgeInsets.all(VeyloraSpacing.xxl),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _buildIntegrityBanner(
            context,
            title: 'Layer 2: Hypervisor & Virtual Machine Metrics',
            description:
                'Samples macOS task_info() for the QEMU process and reads guest kernel status via Virtio-serial RPC channel.',
            sourceTag: 'TASK_BASIC_INFO_64 & GUEST_RPC',
            statusColor: colors.layerQemu,
          ),
          const SizedBox(height: VeyloraSpacing.lg),

          LayoutBuilder(
            builder: (context, constraints) {
              final count = constraints.maxWidth > 900 ? 3 : 1;
              return GridView.count(
                crossAxisCount: count,
                crossAxisSpacing: VeyloraSpacing.md,
                mainAxisSpacing: VeyloraSpacing.md,
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                childAspectRatio: count == 3 ? 1.8 : 2.2,
                children: [
                  MetricCard(
                    title: 'Hypervisor Host RSS',
                    value: activeVm != null
                        ? VosFormat.formatBytes(activeVm.qemuRssBytes)
                        : '0 B',
                    subtitle: 'Real memory wired to QEMU on host macOS',
                    icon: Icons.memory_rounded,
                    accentColor: colors.layerQemu,
                    sparklineData: widget.service.qemuRssHistory,
                  ),
                  MetricCard(
                    title: 'Hypervisor CPU Load',
                    value: activeVm != null
                        ? VosFormat.formatPercent(activeVm.qemuCpuPct)
                        : '0.0%',
                    subtitle: 'QEMU thread CPU consumed on host',
                    icon: Icons.speed_rounded,
                    accentColor: colors.layerQemu,
                    sparklineData: widget.service.qemuCpuHistory,
                  ),
                  MetricCard(
                    title: 'Guest Virtio State',
                    value: activeVm != null && activeVm.guestReady == 1
                        ? 'CONNECTED'
                        : 'STANDBY',
                    subtitle: '/dev/virtio-ports/vos_agent transport',
                    icon: Icons.cable_rounded,
                    accentColor: activeVm != null && activeVm.guestReady == 1
                        ? colors.success
                        : colors.warning,
                  ),
                ],
              );
            },
          ),
          const SizedBox(height: VeyloraSpacing.xl),

          SectionHeader(
            title: 'Hypervisor Process Telemetry ($_selectedRange Window)',
            subtitle: 'QEMU Host RSS memory and Hypervisor CPU consumption on Apple Silicon',
            icon: Icons.show_chart_rounded,
          ),
          const SizedBox(height: VeyloraSpacing.xs),
          LayoutBuilder(
            builder: (context, constraints) {
              final isWide = constraints.maxWidth > 860;
              final qemuCpuChart = MetricChart(
                title: 'QEMU Hypervisor CPU (%)',
                currentValue: activeVm != null
                    ? VosFormat.formatPercent(activeVm.qemuCpuPct)
                    : '0.0%',
                subtitle: 'HVF Acceleration Thread Consumption',
                data: _filterData(widget.service.qemuCpuHistory),
                accentColor: colors.layerQemu,
                unit: '%',
                fixedMaxY: 100.0,
              );
              final qemuRssChart = MetricChart(
                title: 'QEMU Wired RSS (MB)',
                currentValue: activeVm != null
                    ? VosFormat.formatBytes(activeVm.qemuRssBytes)
                    : '0 B',
                subtitle: 'Physical Memory Allocated to Guest Address Space',
                data: _filterData(widget.service.qemuRssHistory),
                accentColor: colors.accent,
                unit: ' MB',
              );

              if (isWide) {
                return Row(
                  children: [
                    Expanded(child: qemuCpuChart),
                    const SizedBox(width: VeyloraSpacing.md),
                    Expanded(child: qemuRssChart),
                  ],
                );
              } else {
                return Column(
                  children: [
                    qemuCpuChart,
                    const SizedBox(height: VeyloraSpacing.md),
                    qemuRssChart,
                  ],
                );
              }
            },
          ),
        ],
      ),
    );
  }

  // TAB 3: Applications & Cgroups
  Widget _buildAppsTab(BuildContext context) {
    final colors = VosColors.of(context);
    final runningApps = widget.service.appInstances.where((i) => i.isRunning).toList();

    return SingleChildScrollView(
      padding: const EdgeInsets.all(VeyloraSpacing.xxl),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _buildIntegrityBanner(
            context,
            title: 'Layer 3 & 4: Application Cgroup Isolation & Guest Execution',
            description:
                'Workloads are executed via direct execve() within isolated Linux cgroups v2 hierarchies (/sys/fs/cgroup/vos/).',
            sourceTag: 'CGROUPS_V2_CONTROLLER',
            statusColor: colors.layerCgroup,
          ),
          const SizedBox(height: VeyloraSpacing.lg),

          LayoutBuilder(
            builder: (context, constraints) {
              final count = constraints.maxWidth > 900 ? 3 : 1;
              return GridView.count(
                crossAxisCount: count,
                crossAxisSpacing: VeyloraSpacing.md,
                mainAxisSpacing: VeyloraSpacing.md,
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                childAspectRatio: count == 3 ? 1.8 : 2.2,
                children: [
                  MetricCard(
                    title: 'Active Workloads',
                    value: '${runningApps.length} Running',
                    subtitle: 'Attached to cgroups v2 resource controllers',
                    icon: Icons.apps_rounded,
                    accentColor: colors.layerCgroup,
                  ),
                  MetricCard(
                    title: 'Registered Applications',
                    value: '${widget.service.registeredApps.length} Apps',
                    subtitle: 'Configured in SQLite catalog',
                    icon: Icons.list_alt_rounded,
                    accentColor: colors.info,
                  ),
                  MetricCard(
                    title: 'Virtual GPU Limits',
                    value: 'UNSUPPORTED',
                    subtitle: 'Headless Linux guest on Apple Silicon',
                    icon: Icons.videogame_asset_off_rounded,
                    accentColor: colors.warning,
                  ),
                ],
              );
            },
          ),
          const SizedBox(height: VeyloraSpacing.xl),

          // Workload table
          SectionHeader(
            title: 'Live Workload Instances (${runningApps.length})',
            subtitle: 'Process isolation parameters and kernel enforcement status',
            icon: Icons.terminal_rounded,
          ),
          const SizedBox(height: VeyloraSpacing.xs),
          if (runningApps.isEmpty)
            Container(
              padding: const EdgeInsets.all(VeyloraSpacing.xl),
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: BorderRadius.circular(VeyloraRadius.lg),
                border: Border.all(color: colors.border),
              ),
              child: Center(
                child: Column(
                  children: [
                    Icon(Icons.inbox_rounded, size: 36, color: colors.textMuted),
                    const SizedBox(height: VeyloraSpacing.sm),
                    Text(
                      'No Active Workload Instances',
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w600,
                        color: colors.textPrimary,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      'Launch an application from the Applications view or execute a benchmark test to observe real-time cgroup telemetry.',
                      style: TextStyle(fontSize: 12, color: colors.textSecondary),
                    ),
                  ],
                ),
              ),
            )
          else
            Container(
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: BorderRadius.circular(VeyloraRadius.lg),
                border: Border.all(color: colors.border),
              ),
              child: ListView.separated(
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                itemCount: runningApps.length,
                separatorBuilder: (ctx, i) => Divider(height: 1, color: colors.borderSubtle),
                itemBuilder: (ctx, i) {
                  final inst = runningApps[i];
                  return ListTile(
                    contentPadding: const EdgeInsets.symmetric(
                      horizontal: VeyloraSpacing.md,
                      vertical: VeyloraSpacing.xs,
                    ),
                    leading: Container(
                      padding: const EdgeInsets.all(8),
                      decoration: BoxDecoration(
                        color: colors.primary.withValues(alpha: 0.12),
                        borderRadius: BorderRadius.circular(VeyloraRadius.sm),
                      ),
                      child: Icon(Icons.bolt_rounded, size: 16, color: colors.primary),
                    ),
                    title: Text(
                      'Instance ${inst.instanceId}',
                      style: TextStyle(
                        fontSize: 13,
                        fontFamily: VeyloraTypography.fontMono,
                        fontWeight: FontWeight.w600,
                        color: colors.textPrimary,
                      ),
                    ),
                    subtitle: Text(
                      'PID: ${inst.pid} · Cgroup: ${inst.cgroup} · Profile: ${inst.profileId}',
                      style: TextStyle(fontSize: 11, color: colors.textSecondary),
                    ),
                    trailing: const StatusBadge(label: 'ENFORCED', color: VosColors.success),
                  );
                },
              ),
            ),
        ],
      ),
    );
  }

  Widget _buildIntegrityBanner(
    BuildContext context, {
    required String title,
    required String description,
    required String sourceTag,
    required Color statusColor,
  }) {
    final colors = VosColors.of(context);
    return Container(
      padding: const EdgeInsets.symmetric(
        horizontal: VeyloraSpacing.md,
        vertical: VeyloraSpacing.sm + 2,
      ),
      decoration: BoxDecoration(
        color: colors.card,
        borderRadius: BorderRadius.circular(VeyloraRadius.md),
        border: Border.all(color: colors.border),
      ),
      child: Row(
        children: [
          Container(
            padding: const EdgeInsets.all(7),
            decoration: BoxDecoration(
              color: statusColor.withValues(alpha: 0.12),
              borderRadius: BorderRadius.circular(VeyloraRadius.sm),
            ),
            child: Icon(Icons.verified_rounded, size: 16, color: statusColor),
          ),
          const SizedBox(width: VeyloraSpacing.sm),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    color: colors.textPrimary,
                  ),
                ),
                const SizedBox(height: 1),
                Text(
                  description,
                  style: TextStyle(fontSize: 11, color: colors.textSecondary),
                ),
              ],
            ),
          ),
          StatusBadge(label: sourceTag, color: statusColor, isSmall: true),
        ],
      ),
    );
  }
}
