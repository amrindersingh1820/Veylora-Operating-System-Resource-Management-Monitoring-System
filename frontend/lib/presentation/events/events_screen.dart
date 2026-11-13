import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../../core/theme.dart';
import '../../core/formatting.dart';
import '../../application/vos_service.dart';
import '../../data/models/event_model.dart';
import '../../widgets/metric_card.dart';
import '../../widgets/status_badge.dart';
import '../../widgets/filter_bar.dart';
import '../../widgets/section_header.dart';
import '../../widgets/empty_state.dart';

class EventsScreen extends StatefulWidget {
  final VosService service;

  const EventsScreen({super.key, required this.service});

  @override
  State<EventsScreen> createState() => _EventsScreenState();
}

class _EventsScreenState extends State<EventsScreen> {
  String? _selectedVmFilter;
  String _selectedTypeFilter = 'ALL';
  final TextEditingController _searchCtrl = TextEditingController();

  @override
  void dispose() {
    _searchCtrl.dispose();
    super.dispose();
  }

  void _showEventDetail(EventLogEntry entry) {
    final colors = VosColors.of(context);
    final isTestEvent = entry.eventType.toUpperCase().contains('TEST') ||
        entry.eventType.toUpperCase().contains('BENCHMARK') ||
        entry.eventType.toUpperCase().contains('LIVE');

    String? foundReportPath;
    if (entry.eventDetails.contains('Reports:')) {
      final parts = entry.eventDetails.split('Reports:').last.trim().split(',');
      for (final p in parts) {
        final trimmed = p.trim();
        if (File(trimmed).existsSync()) {
          foundReportPath = trimmed;
          break;
        }
      }
      foundReportPath ??= parts.firstOrNull?.trim();
    }

    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: colors.card,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
          side: BorderSide(color: colors.border),
        ),
        title: Row(
          children: [
            Container(
              padding: const EdgeInsets.all(6),
              decoration: BoxDecoration(
                color: isTestEvent ? colors.primary.withValues(alpha: 0.15) : colors.bg,
                borderRadius: BorderRadius.circular(6),
              ),
              child: Icon(
                isTestEvent ? Icons.speed_rounded : Icons.history_rounded,
                size: 18,
                color: isTestEvent ? colors.primary : colors.textPrimary,
              ),
            ),
            const SizedBox(width: 10),
            Text(
              isTestEvent ? 'Test Audit Record #${entry.id}' : 'Audit Record #${entry.id}',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: colors.textPrimary),
            ),
          ],
        ),
        content: SizedBox(
          width: 540,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              if (isTestEvent) ...[
                Container(
                  padding: const EdgeInsets.all(12),
                  margin: const EdgeInsets.only(bottom: 12),
                  decoration: BoxDecoration(
                    color: colors.primary.withValues(alpha: 0.1),
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: colors.primary.withValues(alpha: 0.25)),
                  ),
                  child: Row(
                    children: [
                      Icon(
                        entry.eventType.contains('LIVE') ? Icons.camera_alt_outlined : Icons.bolt_rounded,
                        size: 20,
                        color: colors.primary,
                      ),
                      const SizedBox(width: 10),
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              entry.eventType.contains('LIVE')
                                  ? 'Real-World Hands-On Test Session'
                                  : 'Automated Dual Benchmark Test',
                              style: TextStyle(fontSize: 13, fontWeight: FontWeight.w700, color: colors.textPrimary),
                            ),
                            const SizedBox(height: 2),
                            Text(
                              'Persistently stored in SQLite DBMS (table application_events)',
                              style: TextStyle(fontSize: 11, color: colors.textSecondary),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
              ],
              _detailRow(ctx, 'Event Type', entry.eventType),
              _detailRow(ctx, 'Timestamp', '${VosFormat.formatTimestamp(entry.createdAt)} (${entry.createdAt} ms)'),
              _detailRow(ctx, 'Target Environment', entry.vmId.isNotEmpty ? entry.vmId : 'host_darwin'),
              _detailRow(ctx, 'Application ID', entry.appId.isNotEmpty ? entry.appId : '-'),
              _detailRow(ctx, 'Instance ID', entry.instanceId.isNotEmpty ? entry.instanceId : '-'),
              const SizedBox(height: 12),
              Text(
                isTestEvent ? 'Test Metrics & Payload (Stored in SQLite):' : 'Event Details / Payload:',
                style: TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: colors.textSecondary),
              ),
              const SizedBox(height: 6),
              Container(
                width: double.infinity,
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: colors.bg,
                  borderRadius: BorderRadius.circular(6),
                  border: Border.all(color: colors.borderSubtle),
                ),
                child: SelectableText(
                  entry.eventDetails,
                  style: TextStyle(fontFamily: 'monospace', fontSize: 11, height: 1.4, color: colors.textPrimary),
                ),
              ),
            ],
          ),
        ),
        actions: [
          if (foundReportPath != null)
            ElevatedButton.icon(
              style: ElevatedButton.styleFrom(
                backgroundColor: colors.primary,
                foregroundColor: Colors.white,
              ),
              icon: const Icon(Icons.description_outlined, size: 15),
              label: const Text('Read Test Report (.txt)'),
              onPressed: () {
                Navigator.of(ctx).pop();
                _openReportFromFile(foundReportPath!);
              },
            ),
          OutlinedButton(
            onPressed: () => Navigator.of(ctx).pop(),
            child: const Text('Close'),
          ),
        ],
      ),
    );
  }

  void _openReportFromFile(String filePath) async {
    final colors = VosColors.of(context);
    String content = '';
    try {
      final file = File(filePath);
      if (file.existsSync()) {
        content = await file.readAsString();
      } else {
        content = 'Report file not found on disk at: $filePath\n(It may have been moved or generated in another session)';
      }
    } catch (e) {
      content = 'Error reading report file: $e';
    }

    if (!mounted) return;

    showDialog(
      context: context,
      builder: (dialogCtx) => AlertDialog(
        backgroundColor: colors.card,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
          side: BorderSide(color: colors.border),
        ),
        title: Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Row(
              children: [
                Container(
                  padding: const EdgeInsets.all(6),
                  decoration: BoxDecoration(
                    color: colors.primary.withValues(alpha: 0.15),
                    borderRadius: BorderRadius.circular(6),
                  ),
                  child: Icon(Icons.description_rounded, size: 18, color: colors.primary),
                ),
                const SizedBox(width: 10),
                Text(
                  'Saved Audit Test Report',
                  style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: colors.textPrimary),
                ),
              ],
            ),
            IconButton(
              icon: const Icon(Icons.close, size: 18),
              onPressed: () => Navigator.of(dialogCtx).pop(),
            ),
          ],
        ),
        content: SizedBox(
          width: 760,
          height: 520,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
                decoration: BoxDecoration(
                  color: colors.bg,
                  borderRadius: BorderRadius.circular(6),
                  border: Border.all(color: colors.borderSubtle),
                ),
                child: Row(
                  children: [
                    Icon(Icons.folder_outlined, size: 14, color: colors.textSecondary),
                    const SizedBox(width: 6),
                    Expanded(
                      child: Text(
                        filePath,
                        style: TextStyle(fontSize: 11, fontFamily: 'monospace', color: colors.textSecondary),
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 10),
              Expanded(
                child: Container(
                  width: double.infinity,
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: colors.bg,
                    borderRadius: BorderRadius.circular(6),
                    border: Border.all(color: colors.border),
                  ),
                  child: SingleChildScrollView(
                    child: SelectableText(
                      content,
                      style: TextStyle(
                        fontFamily: 'monospace',
                        fontSize: 11,
                        height: 1.45,
                        color: colors.textPrimary,
                      ),
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
        actions: [
          OutlinedButton.icon(
            icon: const Icon(Icons.copy_rounded, size: 15),
            label: const Text('Copy Report Text'),
            onPressed: () {
              Clipboard.setData(ClipboardData(text: content));
              ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(content: Text('Report text copied to clipboard!')),
              );
            },
          ),
          ElevatedButton(
            onPressed: () => Navigator.of(dialogCtx).pop(),
            child: const Text('Close'),
          ),
        ],
      ),
    );
  }

  Widget _detailRow(BuildContext context, String label, String value) {
    final colors = VosColors.of(context);
    return Padding(
      padding: const EdgeInsets.only(bottom: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 140,
            child: Text(label, style: TextStyle(fontSize: 12, color: colors.textMuted)),
          ),
          Expanded(
            child: Text(value, style: TextStyle(fontSize: 12, fontWeight: FontWeight.w500, color: colors.textPrimary)),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);
    final allEvents = widget.service.events;

    // Filter events
    final filtered = allEvents.where((e) {
      if (_selectedVmFilter != null && _selectedVmFilter!.isNotEmpty && e.vmId != _selectedVmFilter) {
        return false;
      }
      if (_selectedTypeFilter != 'ALL') {
        if (_selectedTypeFilter == 'TEST') {
          if (!e.eventType.toUpperCase().contains('TEST') && !e.eventType.toUpperCase().contains('BENCHMARK')) {
            return false;
          }
        } else if (!e.eventType.toUpperCase().contains(_selectedTypeFilter)) {
          return false;
        }
      }
      final q = _searchCtrl.text.trim().toLowerCase();
      if (q.isNotEmpty) {
        final match = e.instanceId.toLowerCase().contains(q) ||
            e.appId.toLowerCase().contains(q) ||
            e.vmId.toLowerCase().contains(q) ||
            e.eventType.toLowerCase().contains(q) ||
            e.eventDetails.toLowerCase().contains(q);
        if (!match) return false;
      }
      return true;
    }).toList();

    // Summary counts
    final totalCount = allEvents.length;
    final testCount = allEvents.where((e) => e.eventType.toUpperCase().contains('TEST') || e.eventType.toUpperCase().contains('BENCHMARK')).length;
    final launchCount = allEvents.where((e) => e.eventType.toUpperCase().contains('LAUNCH')).length;
    final stopCount = allEvents.where((e) => e.eventType.toUpperCase().contains('STOP') || e.eventType.toUpperCase().contains('EXIT')).length;
    final limitCount = allEvents.where((e) => e.eventType.toUpperCase().contains('LIMIT') || e.eventType.toUpperCase().contains('CGROUP')).length;

    return Scaffold(
      backgroundColor: colors.bg,
      body: SingleChildScrollView(
        padding: const EdgeInsets.symmetric(
          horizontal: VeyloraSpacing.xxl,
          vertical: VeyloraSpacing.xl,
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Top Section Header
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Audit & Event History',
                      style: TextStyle(
                        fontSize: 20,
                        fontWeight: FontWeight.w700,
                        color: colors.textPrimary,
                        letterSpacing: -0.4,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      'Immutable audit log stored in SQLite WAL mode (table application_events)',
                      style: TextStyle(fontSize: 12, color: colors.textSecondary),
                    ),
                  ],
                ),
                ElevatedButton.icon(
                  icon: const Icon(Icons.refresh_rounded, size: 15),
                  label: const Text('Refresh Log'),
                  onPressed: () => widget.service.refreshEvents(),
                ),
              ],
            ),
            const SizedBox(height: VeyloraSpacing.lg),

            // Statistics Summary Cards (5 Cards Layout)
            LayoutBuilder(
              builder: (context, constraints) {
                final count = constraints.maxWidth > 1100 ? 5 : (constraints.maxWidth > 700 ? 3 : 2);
                return GridView.count(
                  crossAxisCount: count,
                  crossAxisSpacing: VeyloraSpacing.md,
                  mainAxisSpacing: VeyloraSpacing.md,
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  childAspectRatio: count >= 4 ? 1.8 : 1.6,
                  children: [
                    MetricCard(
                      title: 'Total Audit Records',
                      value: '$totalCount',
                      subtitle: 'SQLite WAL application_events',
                      icon: Icons.history_rounded,
                      accentColor: colors.primary,
                    ),
                    MetricCard(
                      title: 'Benchmark & Tests',
                      value: '$testCount',
                      subtitle: 'Live tests & benchmarks',
                      icon: Icons.speed_rounded,
                      accentColor: colors.success,
                    ),
                    MetricCard(
                      title: 'App Launches',
                      value: '$launchCount',
                      subtitle: 'cgroups attached & execve',
                      icon: Icons.play_arrow_rounded,
                      accentColor: colors.info,
                    ),
                    MetricCard(
                      title: 'Stops & Exits',
                      value: '$stopCount',
                      subtitle: 'Verified exit codes & signals',
                      icon: Icons.stop_rounded,
                      accentColor: colors.warning,
                    ),
                    MetricCard(
                      title: 'Resource Enforcements',
                      value: '$limitCount',
                      subtitle: 'cgroups v2 limits applied',
                      icon: Icons.tune_rounded,
                      accentColor: colors.accent,
                    ),
                  ],
                );
              },
            ),
            const SizedBox(height: 20),

            // Filter Bar
            FilterBar(
              searchController: _searchCtrl,
              searchHint: 'Search instance IDs, app names, or event details...',
              onSearchChanged: (_) => setState(() {}),
              filterDropdowns: [
                // VM Dropdown
                Container(
                  height: 36,
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                  decoration: BoxDecoration(
                    color: colors.bg,
                    borderRadius: BorderRadius.circular(6),
                    border: Border.all(color: colors.border),
                  ),
                  child: DropdownButtonHideUnderline(
                    child: DropdownButton<String?>(
                      value: _selectedVmFilter,
                      dropdownColor: colors.card,
                      isDense: true,
                      style: TextStyle(fontSize: 12, color: colors.textPrimary),
                      items: [
                        const DropdownMenuItem(value: null, child: Text('All Environments')),
                        ...widget.service.vms.map((vm) => DropdownMenuItem(
                              value: vm.vmId,
                              child: Text(vm.name.isNotEmpty ? vm.name : vm.vmId),
                            )),
                      ],
                      onChanged: (val) => setState(() => _selectedVmFilter = val),
                    ),
                  ),
                ),
                const SizedBox(width: 8),

                // Event Type Dropdown
                Container(
                  height: 36,
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                  decoration: BoxDecoration(
                    color: colors.bg,
                    borderRadius: BorderRadius.circular(6),
                    border: Border.all(color: colors.border),
                  ),
                  child: DropdownButtonHideUnderline(
                    child: DropdownButton<String>(
                      value: _selectedTypeFilter,
                      dropdownColor: colors.card,
                      isDense: true,
                      style: TextStyle(fontSize: 12, color: colors.textPrimary),
                      items: const [
                        DropdownMenuItem(value: 'ALL', child: Text('All Events')),
                        DropdownMenuItem(value: 'TEST', child: Text('🎯 Test Runs & Baselines (All)')),
                        DropdownMenuItem(value: 'TEST_ISSUE', child: Text('⚠️ Performance Issues & Regressions')),
                        DropdownMenuItem(value: 'TEST_LIVE', child: Text('📸 Live Hands-On Tests')),
                        DropdownMenuItem(value: 'TEST_BENCHMARK', child: Text('⚡ Synthetic Benchmarks')),
                        DropdownMenuItem(value: 'LAUNCH', child: Text('App Launch')),
                        DropdownMenuItem(value: 'STOP', child: Text('App Stop / Exit')),
                        DropdownMenuItem(value: 'CGROUP', child: Text('cgroup Limit')),
                        DropdownMenuItem(value: 'RECONCILE', child: Text('Reconcile')),
                      ],
                      onChanged: (val) {
                        if (val != null) setState(() => _selectedTypeFilter = val);
                      },
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 20),

            // Main Records Table
            SectionHeader(
              title: 'Event Records (${filtered.length} of $totalCount)',
              subtitle: 'Click any row or inspect button to view payload and test reports',
              icon: Icons.receipt_long_rounded,
            ),
            if (filtered.isEmpty)
              const EmptyState(
                icon: Icons.history_toggle_off_rounded,
                title: 'No Matching Events',
                description: 'No audit records match the selected filters.',
              )
            else
              LayoutBuilder(
                builder: (context, constraints) {
                  const minWidth = 980.0;
                  return Container(
                    decoration: BoxDecoration(
                      color: colors.card,
                      borderRadius: BorderRadius.circular(10),
                      border: Border.all(color: colors.border),
                    ),
                    clipBehavior: Clip.antiAlias,
                    child: SingleChildScrollView(
                      scrollDirection: Axis.horizontal,
                      child: ConstrainedBox(
                        constraints: BoxConstraints(
                          minWidth: constraints.maxWidth < minWidth
                              ? minWidth
                              : constraints.maxWidth,
                        ),
                        child: Table(
                          columnWidths: const {
                            0: FlexColumnWidth(0.8),
                            1: FlexColumnWidth(1.8),
                            2: FlexColumnWidth(1.6),
                            3: FlexColumnWidth(2.0),
                            4: FlexColumnWidth(2.6),
                            5: FlexColumnWidth(4.2),
                            6: FlexColumnWidth(1.0),
                          },
                          children: [
                            TableRow(
                              decoration: BoxDecoration(
                                color: colors.bg,
                                borderRadius: const BorderRadius.vertical(top: Radius.circular(9)),
                              ),
                              children: [
                                _headerCell(context, '#'),
                                _headerCell(context, 'Timestamp'),
                                _headerCell(context, 'Environment'),
                                _headerCell(context, 'App / Instance'),
                                _headerCell(context, 'Type'),
                                _headerCell(context, 'Details'),
                                _headerCell(context, 'Inspect', alignRight: true),
                              ],
                            ),
                            ...filtered.map((entry) {
                              return TableRow(
                                children: [
                                  Padding(
                                    padding: const EdgeInsets.all(12),
                                    child: Text('${entry.id}', style: TextStyle(fontSize: 12, color: colors.textMuted)),
                                  ),
                                  Padding(
                                    padding: const EdgeInsets.all(12),
                                    child: Text(
                                      VosFormat.formatTimestamp(entry.createdAt),
                                      style: TextStyle(fontSize: 11, color: colors.textSecondary),
                                    ),
                                  ),
                                  Padding(
                                    padding: const EdgeInsets.all(12),
                                    child: Text(entry.vmId, style: TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: colors.textPrimary)),
                                  ),
                                  Padding(
                                    padding: const EdgeInsets.all(12),
                                    child: Text(
                                      entry.instanceId.isNotEmpty ? entry.instanceId : (entry.appId.isNotEmpty ? entry.appId : '-'),
                                      style: TextStyle(fontSize: 11, fontFamily: 'monospace', color: colors.textSecondary),
                                      overflow: TextOverflow.ellipsis,
                                    ),
                                  ),
                                  Padding(
                                    padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 10),
                                    child: StatusBadge(
                                      label: entry.eventType,
                                      color: _badgeColor(colors, entry.eventType),
                                      isSmall: true,
                                    ),
                                  ),
                                  Padding(
                                    padding: const EdgeInsets.all(12),
                                    child: Text(
                                      entry.eventDetails,
                                      style: TextStyle(fontSize: 12, color: colors.textPrimary),
                                      maxLines: 2,
                                      overflow: TextOverflow.ellipsis,
                                    ),
                                  ),
                                  Padding(
                                    padding: const EdgeInsets.all(8),
                                    child: Align(
                                      alignment: Alignment.centerRight,
                                      child: IconButton(
                                        icon: Icon(Icons.remove_red_eye_outlined, size: 16, color: colors.primary),
                                        tooltip: 'Inspect Record',
                                        onPressed: () => _showEventDetail(entry),
                                      ),
                                    ),
                                  ),
                                ],
                              );
                            }),
                          ],
                        ),
                      ),
                    ),
                  );
                },
              ),
          ],
        ),
      ),
    );
  }

  Widget _headerCell(BuildContext context, String text, {bool alignRight = false}) {
    final colors = VosColors.of(context);
    return Padding(
      padding: const EdgeInsets.all(12),
      child: Text(
        text,
        textAlign: alignRight ? TextAlign.right : TextAlign.left,
        style: TextStyle(fontSize: 11, fontWeight: FontWeight.w600, color: colors.textSecondary),
      ),
    );
  }

  Color _badgeColor(VosColorPalette colors, String type) {
    final t = type.toUpperCase();
    if (t.contains('ISSUE')) return colors.warning;
    if (t.contains('BASELINE') || t.contains('VERIFIED')) return colors.success;
    if (t.contains('LIVE')) return colors.success;
    if (t.contains('BENCHMARK') || t.contains('TEST')) return colors.primary;
    if (t.contains('LAUNCH') || t.contains('START')) return colors.success;
    if (t.contains('STOP') || t.contains('EXIT') || t.contains('TERM')) return colors.warning;
    if (t.contains('FAIL') || t.contains('ERROR') || t.contains('KILL')) return colors.danger;
    if (t.contains('CGROUP') || t.contains('LIMIT')) return colors.accent;
    return colors.info;
  }
}
