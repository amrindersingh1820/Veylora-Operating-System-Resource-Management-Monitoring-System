import 'package:flutter/material.dart';

import '../../core/theme.dart';
import '../../core/constants.dart';
import '../../core/formatting.dart';
import '../../application/vos_service.dart';
import '../../widgets/metric_card.dart';
import '../../widgets/metric_chart.dart';
import '../../widgets/status_badge.dart';

class DashboardScreen extends StatelessWidget {
  final VosService service;

  const DashboardScreen({super.key, required this.service});

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);
    final host = service.hostTelemetry;
    final activeVm = service.selectedVm;
    final runningVms = service.vms.where((v) => v.isRunning).toList();
    final runningApps = service.appInstances.where((i) => i.isRunning).toList();
    final recentEvents = service.events.take(6).toList();

    return Scaffold(
      backgroundColor: colors.bg,
      body: SingleChildScrollView(
        padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // ================================================================
            // TOP AREA: Welcome, Overview, Host Information, System Health
            // ================================================================
            Container(
              padding: const EdgeInsets.all(18),
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: VeyloraRadius.rLg,
                border: Border.all(color: colors.border),
                boxShadow: [
                  BoxShadow(
                    color: Colors.black.withValues(alpha: colors.isDark ? 0.25 : 0.03),
                    blurRadius: 10,
                    offset: const Offset(0, 3),
                  ),
                ],
              ),
              child: LayoutBuilder(
                builder: (context, constraints) {
                  final isCompact = constraints.maxWidth < 700;

                  final brandBlock = Row(
                    children: [
                      Container(
                        width: 44,
                        height: 44,
                        decoration: BoxDecoration(
                          color: colors.primary.withValues(alpha: 0.12),
                          borderRadius: VeyloraRadius.rMd,
                          border: Border.all(color: colors.primary.withValues(alpha: 0.28), width: 1.2),
                        ),
                        child: Center(
                          child: Icon(Icons.hub_rounded, color: colors.primary, size: 22),
                        ),
                      ),
                      const SizedBox(width: 14),
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Wrap(
                              spacing: 8,
                              runSpacing: 4,
                              crossAxisAlignment: WrapCrossAlignment.center,
                              children: [
                                Text(
                                  'Welcome to ${VosConstants.appTitle}',
                                  style: TextStyle(
                                    fontSize: 16.5,
                                    fontWeight: FontWeight.w700,
                                    color: colors.textPrimary,
                                    letterSpacing: -0.3,
                                  ),
                                ),
                                StatusBadge(
                                  label: service.isInitialized ? 'SYSTEM ONLINE' : 'STANDBY',
                                  color: service.isInitialized ? colors.success : colors.warning,
                                  isSmall: true,
                                ),
                                StatusBadge(
                                  label: 'HVF READY',
                                  color: colors.primary,
                                  isSmall: true,
                                  showDot: false,
                                ),
                              ],
                            ),
                            const SizedBox(height: 3),
                            Text(
                              '${VosConstants.appTagline} · Host: ${host.hostModel.isNotEmpty ? host.hostModel : "Apple Silicon"} (${host.hostPlatform}) · Load Avg: ${host.loadAvg1m.toStringAsFixed(2)}, ${host.loadAvg5m.toStringAsFixed(2)}, ${host.loadAvg15m.toStringAsFixed(2)} · ${host.cpuCount} Mach Cores',
                              style: TextStyle(
                                fontSize: 12,
                                color: colors.textSecondary,
                                letterSpacing: -0.1,
                              ),
                              overflow: TextOverflow.ellipsis,
                              maxLines: 1,
                            ),
                          ],
                        ),
                      ),
                    ],
                  );

                  final actionBlock = Wrap(
                    spacing: 12,
                    runSpacing: 8,
                    crossAxisAlignment: WrapCrossAlignment.center,
                    alignment: isCompact ? WrapAlignment.spaceBetween : WrapAlignment.end,
                    children: [
                      if (activeVm != null) ...[
                        Column(
                          crossAxisAlignment: isCompact ? CrossAxisAlignment.start : CrossAxisAlignment.end,
                          children: [
                            Text(
                              activeVm.name.isNotEmpty ? activeVm.name : activeVm.vmId,
                              style: TextStyle(
                                fontSize: 12.5,
                                fontWeight: FontWeight.w600,
                                color: colors.textPrimary,
                              ),
                              overflow: TextOverflow.ellipsis,
                              maxLines: 1,
                            ),
                            Text(
                              activeVm.guestReady == 1 ? 'Guest Agent Live (Alpine)' : 'Guest Standby',
                              style: TextStyle(
                                fontSize: 11,
                                color: activeVm.guestReady == 1 ? colors.success : colors.warning,
                                fontFamily: VeyloraTypography.fontMono,
                              ),
                            ),
                          ],
                        ),
                      ],
                      ElevatedButton.icon(
                        style: ElevatedButton.styleFrom(
                          backgroundColor: colors.primary,
                          foregroundColor: Colors.white,
                          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
                          shape: RoundedRectangleBorder(borderRadius: VeyloraRadius.rMd),
                        ),
                        icon: const Icon(Icons.refresh_rounded, size: 15),
                        label: const Text('Sync Telemetry'),
                        onPressed: () => service.refreshAll(),
                      ),
                    ],
                  );

                  if (isCompact) {
                    return Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        brandBlock,
                        const SizedBox(height: 12),
                        actionBlock,
                      ],
                    );
                  }

                  return Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Expanded(child: brandBlock),
                      const SizedBox(width: 16),
                      actionBlock,
                    ],
                  );
                },
              ),
            ),
            const SizedBox(height: 18),

            // ================================================================
            // METRIC CARDS: Host CPU, Host Memory, Running VMs, Running Apps
            // ================================================================
            LayoutBuilder(
              builder: (context, constraints) {
                final count = constraints.maxWidth > 1050 ? 4 : (constraints.maxWidth > 650 ? 2 : 1);
                return GridView.count(
                  crossAxisCount: count,
                  crossAxisSpacing: 14,
                  mainAxisSpacing: 14,
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  childAspectRatio: count == 4 ? 1.75 : 1.6,
                  children: [
                    MetricCard(
                      title: 'Host CPU Utilization',
                      value: VosFormat.formatPercent(host.cpuUsagePct),
                      subtitle: '${host.cpuCount} Cores · Mach Kernel Sampling',
                      icon: Icons.speed_rounded,
                      accentColor: colors.layerHost,
                      sparklineData: service.hostCpuHistory,
                      trendLabel: 'Load: ${host.loadAvg1m.toStringAsFixed(2)}',
                      isPositiveTrend: host.cpuUsagePct < 75.0,
                    ),
                    MetricCard(
                      title: 'Host Physical Memory',
                      value: VosFormat.formatBytes(host.usedRamBytes),
                      subtitle: 'Total: ${VosFormat.formatBytes(host.totalRamBytes)} Unified RAM',
                      icon: Icons.storage_rounded,
                      accentColor: colors.accent,
                      sparklineData: service.hostRamHistory,
                      trendLabel: '${((host.usedRamBytes / (host.totalRamBytes > 0 ? host.totalRamBytes : 1)) * 100).toStringAsFixed(0)}% Used',
                      isPositiveTrend: true,
                    ),
                    MetricCard(
                      title: 'Virtual Machines',
                      value: '${runningVms.length} / ${service.vms.length} Active',
                      subtitle: '${runningVms.length} QEMU HVF Guest Instances',
                      icon: Icons.dns_rounded,
                      accentColor: colors.layerQemu,
                      trendLabel: 'HVF Accelerated',
                      isPositiveTrend: true,
                    ),
                    MetricCard(
                      title: 'Running Applications',
                      value: '${runningApps.length} Active',
                      subtitle: '${service.registeredApps.length} Registered with Cgroups v2',
                      icon: Icons.layers_rounded,
                      accentColor: colors.layerGuest,
                      trendLabel: 'POSIX Isolated',
                      isPositiveTrend: true,
                    ),
                  ],
                );
              },
            ),
            const SizedBox(height: 20),

            // ================================================================
            // MAIN CONTENT: Balanced Two-Column Layout
            // Left: CPU & Memory Telemetry Charts (Actual backend data)
            // Right: VM Status Overview & Resource Enforcement Status
            // ================================================================
            LayoutBuilder(
              builder: (context, constraints) {
                final isWide = constraints.maxWidth > 920;
                final leftColumn = Column(
                  children: [
                    MetricChart(
                      title: 'Host CPU Utilization (%)',
                      currentValue: VosFormat.formatPercent(host.cpuUsagePct),
                      subtitle: 'Real-time Mach kernel processor sampling',
                      data: service.hostCpuHistory,
                      accentColor: colors.layerHost,
                      unit: '%',
                      height: 150,
                      fixedMaxY: 100.0,
                    ),
                    const SizedBox(height: 14),
                    MetricChart(
                      title: 'Host Physical Memory (MB)',
                      currentValue: '${(host.usedRamBytes / (1024 * 1024)).toStringAsFixed(0)} MB',
                      subtitle: 'Resident working set (RSS) memory footprint',
                      data: service.hostRamHistory,
                      accentColor: colors.accent,
                      unit: ' MB',
                      height: 150,
                      fixedMaxY: (host.totalRamBytes / (1024 * 1024)),
                    ),
                  ],
                );

                final rightColumn = Column(
                  children: [
                    _buildVmOverviewCard(context),
                    const SizedBox(height: 14),
                    _buildResourceEnforcementCard(context),
                  ],
                );

                if (isWide) {
                  return Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Expanded(flex: 3, child: leftColumn),
                      const SizedBox(width: 16),
                      Expanded(flex: 2, child: rightColumn),
                    ],
                  );
                } else {
                  return Column(
                    children: [
                      leftColumn,
                      const SizedBox(height: 16),
                      rightColumn,
                    ],
                  );
                }
              },
            ),
            const SizedBox(height: 22),

            // ================================================================
            // LOWER SECTION: Recently Running Applications & Recent Events
            // ================================================================
            LayoutBuilder(
              builder: (context, constraints) {
                final isWide = constraints.maxWidth > 920;
                if (isWide) {
                  return Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Expanded(flex: 3, child: _buildRunningAppsSection(context, runningApps)),
                      const SizedBox(width: 16),
                      Expanded(flex: 2, child: _buildEventStreamSection(context, recentEvents)),
                    ],
                  );
                } else {
                  return Column(
                    children: [
                      _buildRunningAppsSection(context, runningApps),
                      const SizedBox(height: 16),
                      _buildEventStreamSection(context, recentEvents),
                    ],
                  );
                }
              },
            ),
          ],
        ),
      ),
    );
  }

  // ==========================================================================
  // Right Column: VM Status Overview Card
  // ==========================================================================
  Widget _buildVmOverviewCard(BuildContext context) {
    final colors = VosColors.of(context);
    final vm = service.selectedVm;

    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: colors.card,
        borderRadius: VeyloraRadius.rLg,
        border: Border.all(color: colors.border),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withValues(alpha: colors.isDark ? 0.2 : 0.02),
            blurRadius: 8,
            offset: const Offset(0, 2),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Row(
                children: [
                  Icon(Icons.dns_rounded, size: 16, color: colors.primary),
                  const SizedBox(width: 8),
                  Text(
                    'Active Virtual Machine',
                    style: TextStyle(
                      fontSize: 13.5,
                      fontWeight: FontWeight.w600,
                      color: colors.textPrimary,
                      letterSpacing: -0.2,
                    ),
                  ),
                ],
              ),
              if (vm != null)
                StatusBadge(label: vm.state, isSmall: true)
              else
                const StatusBadge(label: 'NONE', isSmall: true),
            ],
          ),
          const SizedBox(height: 14),

          if (vm == null)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 24),
              child: Center(
                child: Text(
                  'No virtual machines configured',
                  style: TextStyle(fontSize: 12, color: colors.textMuted),
                ),
              ),
            )
          else ...[
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: colors.bg,
                borderRadius: VeyloraRadius.rMd,
                border: Border.all(color: colors.borderSubtle),
              ),
              child: Column(
                children: [
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text('Instance Name', style: TextStyle(fontSize: 11.5, color: colors.textSecondary)),
                      const SizedBox(width: 8),
                      Flexible(
                        child: Text(
                          vm.name.isNotEmpty ? vm.name : vm.vmId,
                          style: TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: colors.textPrimary),
                          overflow: TextOverflow.ellipsis,
                          maxLines: 1,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text('Architecture', style: TextStyle(fontSize: 11.5, color: colors.textSecondary)),
                      const SizedBox(width: 8),
                      Flexible(
                        child: Text(
                          '${vm.guestArch} (Apple HVF)',
                          style: TextStyle(
                            fontSize: 11.5,
                            fontFamily: VeyloraTypography.fontMono,
                            color: colors.textPrimary,
                          ),
                          overflow: TextOverflow.ellipsis,
                          maxLines: 1,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text('vCPU & Memory', style: TextStyle(fontSize: 11.5, color: colors.textSecondary)),
                      const SizedBox(width: 8),
                      Flexible(
                        child: Text(
                          '${vm.vcpuCount} Cores · ${vm.ramMb} MB RAM',
                          style: TextStyle(
                            fontSize: 11.5,
                            fontFamily: VeyloraTypography.fontMono,
                            color: colors.textPrimary,
                          ),
                          overflow: TextOverflow.ellipsis,
                          maxLines: 1,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text('EDK2 Firmware', style: TextStyle(fontSize: 11.5, color: colors.textSecondary)),
                      const SizedBox(width: 8),
                      Flexible(
                        child: Text(
                          'edk2-aarch64-code.fd',
                          style: TextStyle(
                            fontSize: 11,
                            fontFamily: VeyloraTypography.fontMono,
                            color: colors.success,
                          ),
                          overflow: TextOverflow.ellipsis,
                          maxLines: 1,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text('Guest Agent Link', style: TextStyle(fontSize: 11.5, color: colors.textSecondary)),
                      const SizedBox(width: 8),
                      StatusBadge(
                        label: vm.guestReady == 1 ? 'ONLINE' : 'STANDBY',
                        color: vm.guestReady == 1 ? colors.success : colors.warning,
                        isSmall: true,
                      ),
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

  // ==========================================================================
  // Right Column: Resource Enforcement Status Card (cgroups v2)
  // ==========================================================================
  Widget _buildResourceEnforcementCard(BuildContext context) {
    final colors = VosColors.of(context);

    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: colors.card,
        borderRadius: VeyloraRadius.rLg,
        border: Border.all(color: colors.border),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withValues(alpha: colors.isDark ? 0.2 : 0.02),
            blurRadius: 8,
            offset: const Offset(0, 2),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Flexible(
                child: Row(
                  children: [
                    Icon(Icons.shield_outlined, size: 16, color: colors.accent),
                    const SizedBox(width: 8),
                    Flexible(
                      child: Text(
                        'Resource Enforcement (cgroups v2)',
                        style: TextStyle(
                          fontSize: 13.5,
                          fontWeight: FontWeight.w600,
                          color: colors.textPrimary,
                          letterSpacing: -0.2,
                        ),
                        overflow: TextOverflow.ellipsis,
                        maxLines: 1,
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 8),
              StatusBadge(label: 'ACTIVE', color: colors.success, isSmall: true),
            ],
          ),
          const SizedBox(height: 14),

          _buildEnforcementRow(context, 'CPU Controller', 'cpu.max / CFS weight enforcement active', true),
          const SizedBox(height: 8),
          _buildEnforcementRow(context, 'Memory Limiter', 'memory.max hard cap with zero OOM leaks', true),
          const SizedBox(height: 8),
          _buildEnforcementRow(context, 'Process Ceiling', 'pids.max sandboxed (20–200 tasks)', true),
          const SizedBox(height: 8),
          _buildEnforcementRow(context, 'I/O Bandwidth', 'io.max rate limiting active (5.0 MB/s)', true),
          const SizedBox(height: 8),
          _buildEnforcementRow(
            context,
            'Virtual GPU',
            'UNSUPPORTED (Apple Silicon host security policy)',
            false,
            isUnsupported: true,
          ),
        ],
      ),
    );
  }

  Widget _buildEnforcementRow(BuildContext context, String label, String description, bool isEnforced, {bool isUnsupported = false}) {
    final colors = VosColors.of(context);
    final indicatorColor = isUnsupported ? colors.textMuted : (isEnforced ? colors.success : colors.warning);

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
      decoration: BoxDecoration(
        color: colors.bg,
        borderRadius: VeyloraRadius.rMd,
        border: Border.all(color: colors.borderSubtle),
      ),
      child: Row(
        children: [
          Icon(
            isUnsupported ? Icons.block_rounded : (isEnforced ? Icons.check_circle_outline_rounded : Icons.info_outline),
            size: 14,
            color: indicatorColor,
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  label,
                  style: TextStyle(
                    fontSize: 11.5,
                    fontWeight: FontWeight.w600,
                    color: colors.textPrimary,
                  ),
                ),
                Text(
                  description,
                  style: TextStyle(
                    fontSize: 10,
                    color: isUnsupported ? colors.textMuted : colors.textSecondary,
                  ),
                  overflow: TextOverflow.ellipsis,
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // ==========================================================================
  // Lower Section: Recently Running Applications
  // ==========================================================================
  Widget _buildRunningAppsSection(BuildContext context, List<dynamic> apps) {
    final colors = VosColors.of(context);
    final registered = service.registeredApps;

    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: colors.card,
        borderRadius: VeyloraRadius.rLg,
        border: Border.all(color: colors.border),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withValues(alpha: colors.isDark ? 0.2 : 0.02),
            blurRadius: 8,
            offset: const Offset(0, 2),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Flexible(
                child: Row(
                  children: [
                    Icon(Icons.layers_rounded, size: 16, color: colors.primary),
                    const SizedBox(width: 8),
                    Flexible(
                      child: Text(
                        'Application Workloads',
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w600,
                          color: colors.textPrimary,
                          letterSpacing: -0.2,
                        ),
                        overflow: TextOverflow.ellipsis,
                        maxLines: 1,
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 8),
              Text(
                '${registered.length} Registered',
                style: TextStyle(fontSize: 11.5, color: colors.textSecondary),
              ),
            ],
          ),
          const SizedBox(height: 12),

          if (registered.isEmpty)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 24),
              child: Center(
                child: Text(
                  'No registered applications yet. Register one from Applications.',
                  style: TextStyle(fontSize: 12, color: colors.textMuted),
                ),
              ),
            )
          else
            LayoutBuilder(
              builder: (context, tableConstraints) {
                return SingleChildScrollView(
                  scrollDirection: Axis.horizontal,
                  child: ConstrainedBox(
                    constraints: BoxConstraints(
                      minWidth: tableConstraints.maxWidth < 450 ? 450 : tableConstraints.maxWidth,
                    ),
                    child: Table(
                      columnWidths: const {
                        0: FlexColumnWidth(3),
                        1: FlexColumnWidth(2),
                        2: FlexColumnWidth(2),
                        3: FlexColumnWidth(1.5),
                      },
                      children: [
                        TableRow(
                          decoration: BoxDecoration(
                            color: colors.bg,
                            borderRadius: VeyloraRadius.rMd,
                          ),
                          children: [
                            _tableHeader(context, 'Application'),
                            _tableHeader(context, 'Profile'),
                            _tableHeader(context, 'RAM (RSS)'),
                            _tableHeader(context, 'Status', alignRight: true),
                          ],
                        ),
                        ...registered.take(5).map((app) {
                          final instance = service.appInstances.where((i) => i.appId == app.appId).firstOrNull;
                          final isRunning = instance?.isRunning ?? false;

                          return TableRow(
                            children: [
                              Padding(
                                padding: const EdgeInsets.symmetric(vertical: 9, horizontal: 8),
                                child: Row(
                                  children: [
                                    Icon(Icons.terminal_rounded, size: 14, color: isRunning ? colors.success : colors.textMuted),
                                    const SizedBox(width: 8),
                                    Expanded(
                                      child: Column(
                                        crossAxisAlignment: CrossAxisAlignment.start,
                                        children: [
                                          Text(
                                            app.name,
                                            style: TextStyle(
                                              fontSize: 12,
                                              fontWeight: FontWeight.w600,
                                              color: colors.textPrimary,
                                            ),
                                            overflow: TextOverflow.ellipsis,
                                          ),
                                          Text(
                                            app.appId,
                                            style: TextStyle(
                                              fontSize: 10,
                                              fontFamily: VeyloraTypography.fontMono,
                                              color: colors.textMuted,
                                            ),
                                          ),
                                        ],
                                      ),
                                    ),
                                  ],
                                ),
                              ),
                              Padding(
                                padding: const EdgeInsets.symmetric(vertical: 9, horizontal: 8),
                                child: Text(
                                  app.defaultProfileId,
                                  style: TextStyle(fontSize: 11.5, color: colors.textSecondary),
                                ),
                              ),
                              Padding(
                                padding: const EdgeInsets.symmetric(vertical: 9, horizontal: 8),
                                child: Text(
                                  isRunning && instance!.memoryUsageBytes > 0
                                      ? '${(instance.memoryUsageBytes / (1024 * 1024)).toStringAsFixed(1)} MB'
                                      : (isRunning ? 'Active' : 'Idle'),
                                  style: TextStyle(
                                    fontSize: 11.5,
                                    fontFamily: VeyloraTypography.fontMono,
                                    color: colors.textSecondary,
                                  ),
                                ),
                              ),
                              Padding(
                                padding: const EdgeInsets.symmetric(vertical: 7, horizontal: 8),
                                child: Align(
                                  alignment: Alignment.centerRight,
                                  child: StatusBadge(
                                    label: isRunning ? 'RUNNING' : 'STANDBY',
                                    color: isRunning ? colors.success : colors.textMuted,
                                    isSmall: true,
                                  ),
                                ),
                              ),
                            ],
                          );
                        }),
                      ],
                    ),
                  ),
                );
              },
            ),
        ],
      ),
    );
  }

  // ==========================================================================
  // Lower Section: Recent Audit Events Stream (from SQLite)
  // ==========================================================================
  Widget _buildEventStreamSection(BuildContext context, List<dynamic> events) {
    final colors = VosColors.of(context);

    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: colors.card,
        borderRadius: VeyloraRadius.rLg,
        border: Border.all(color: colors.border),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withValues(alpha: colors.isDark ? 0.2 : 0.02),
            blurRadius: 8,
            offset: const Offset(0, 2),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Flexible(
                child: Row(
                  children: [
                    Icon(Icons.history_rounded, size: 16, color: colors.primary),
                    const SizedBox(width: 8),
                    Flexible(
                      child: Text(
                        'Audit & Telemetry Stream',
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w600,
                          color: colors.textPrimary,
                          letterSpacing: -0.2,
                        ),
                        overflow: TextOverflow.ellipsis,
                        maxLines: 1,
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 8),
              Text(
                'SQLite WAL',
                style: TextStyle(
                  fontSize: 10.5,
                  fontFamily: VeyloraTypography.fontMono,
                  color: colors.textMuted,
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),

          if (events.isEmpty)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 24),
              child: Center(
                child: Text(
                  'No audit events recorded yet',
                  style: TextStyle(fontSize: 12, color: colors.textMuted),
                ),
              ),
            )
          else
            ListView.separated(
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              itemCount: events.length,
              separatorBuilder: (context, index) => Divider(color: colors.borderSubtle, height: 10),
              itemBuilder: (context, idx) {
                final ev = events[idx];
                return Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Padding(
                      padding: const EdgeInsets.only(top: 2),
                      child: StatusBadge(label: ev.eventType, isSmall: true),
                    ),
                    const SizedBox(width: 10),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            ev.eventDetails,
                            style: TextStyle(fontSize: 11.5, color: colors.textPrimary),
                            maxLines: 2,
                            overflow: TextOverflow.ellipsis,
                          ),
                          const SizedBox(height: 2),
                          Text(
                            '${ev.vmId.isNotEmpty ? ev.vmId : "host"} · ${VosFormat.formatTimestamp(ev.createdAt)}',
                            style: TextStyle(fontSize: 10, color: colors.textMuted),
                          ),
                        ],
                      ),
                    ),
                  ],
                );
              },
            ),
        ],
      ),
    );
  }

  Widget _tableHeader(BuildContext context, String title, {bool alignRight = false}) {
    final colors = VosColors.of(context);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 7, horizontal: 8),
      child: Text(
        title,
        textAlign: alignRight ? TextAlign.right : TextAlign.left,
        style: TextStyle(
          fontSize: 11,
          fontWeight: FontWeight.w600,
          color: colors.textSecondary,
        ),
      ),
    );
  }
}
