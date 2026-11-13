import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../../core/theme.dart';
import '../../application/vos_service.dart';
import '../../data/models/vm_model.dart';
import '../../widgets/vm_card.dart';
import '../../widgets/filter_bar.dart';
import '../../widgets/section_header.dart';
import '../../widgets/empty_state.dart';
import '../../widgets/confirmation_dialog.dart';
import '../../widgets/status_badge.dart';

class VmScreen extends StatefulWidget {
  final VosService service;

  const VmScreen({super.key, required this.service});

  @override
  State<VmScreen> createState() => _VmScreenState();
}

class _VmScreenState extends State<VmScreen> {
  final TextEditingController _searchCtrl = TextEditingController();
  String _selectedStateFilter = 'ALL';
  bool _isGridView = true;

  @override
  void dispose() {
    _searchCtrl.dispose();
    super.dispose();
  }

  void _showCreateVmWizard() {
    showDialog(
      context: context,
      builder: (context) => _VmCreationWizard(service: widget.service),
    );
  }

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);
    final allVms = widget.service.vms;

    final filteredVms = allVms.where((vm) {
      if (_selectedStateFilter != 'ALL' && vm.state.toUpperCase() != _selectedStateFilter) {
        return false;
      }
      final q = _searchCtrl.text.trim().toLowerCase();
      if (q.isNotEmpty) {
        final match = vm.name.toLowerCase().contains(q) ||
            vm.vmId.toLowerCase().contains(q) ||
            vm.guestArch.toLowerCase().contains(q);
        if (!match) return false;
      }
      return true;
    }).toList();

    return Scaffold(
      backgroundColor: colors.bg,
      body: SingleChildScrollView(
        padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Firmware & Hypervisor Notice Banner
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: VeyloraRadius.rLg,
                border: Border.all(color: colors.border),
                boxShadow: [
                  BoxShadow(
                    color: Colors.black.withValues(alpha: colors.isDark ? 0.2 : 0.02),
                    blurRadius: 6,
                    offset: const Offset(0, 2),
                  ),
                ],
              ),
              child: Row(
                children: [
                  Container(
                    padding: const EdgeInsets.all(6),
                    decoration: BoxDecoration(
                      color: colors.primary.withValues(alpha: 0.12),
                      borderRadius: VeyloraRadius.rMd,
                    ),
                    child: Icon(Icons.shield_outlined, size: 16, color: colors.primary),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'QEMU 11.1.2 Hypervisor · macOS HVF Acceleration · EDK2 UEFI Firmware',
                          style: TextStyle(
                            fontSize: 12.5,
                            fontWeight: FontWeight.w600,
                            color: colors.textPrimary,
                            letterSpacing: -0.1,
                          ),
                          overflow: TextOverflow.ellipsis,
                          maxLines: 1,
                        ),
                        const SizedBox(height: 2),
                        Text(
                          'Firmware: /opt/homebrew/share/qemu/edk2-aarch64-code.fd · Guest Kernel: Linux 6.6.31-0-lts (Alpine ARM64)',
                          style: TextStyle(
                            fontSize: 11,
                            color: colors.textSecondary,
                            fontFamily: VeyloraTypography.fontMono,
                          ),
                          overflow: TextOverflow.ellipsis,
                          maxLines: 1,
                        ),
                      ],
                    ),
                  ),
                  const StatusBadge(label: 'HVF ACCELERATED', isSmall: true),
                ],
              ),
            ),
            const SizedBox(height: 18),

            // Controls & Filter Bar
            FilterBar(
              searchController: _searchCtrl,
              searchHint: 'Filter by VM name, ID, or architecture...',
              onSearchChanged: (_) => setState(() {}),
              filterDropdowns: [
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
                      value: _selectedStateFilter,
                      dropdownColor: colors.card,
                      isDense: true,
                      style: TextStyle(fontSize: 12, color: colors.textPrimary),
                      items: const [
                        DropdownMenuItem(value: 'ALL', child: Text('All States')),
                        DropdownMenuItem(value: 'RUNNING', child: Text('Running')),
                        DropdownMenuItem(value: 'STOPPED', child: Text('Stopped')),
                      ],
                      onChanged: (val) {
                        if (val != null) setState(() => _selectedStateFilter = val);
                      },
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                IconButton(
                  icon: Icon(_isGridView ? Icons.view_list_rounded : Icons.grid_view_rounded, size: 18),
                  tooltip: _isGridView ? 'Switch to Table View' : 'Switch to Grid View',
                  onPressed: () => setState(() => _isGridView = !_isGridView),
                ),
              ],
              primaryAction: ElevatedButton.icon(
                icon: const Icon(Icons.add, size: 16),
                label: const Text('New Virtual Machine'),
                onPressed: _showCreateVmWizard,
              ),
            ),
            const SizedBox(height: 20),

            // Section Header
            SectionHeader(
              title: 'Virtual Machine Instances (${filteredVms.length} of ${allVms.length})',
              subtitle: 'Select an active virtual machine to inspect its telemetry and deploy guest applications',
              icon: Icons.dns_rounded,
            ),

            if (filteredVms.isEmpty)
              EmptyState(
                icon: Icons.dns_outlined,
                title: 'No Virtual Machines Match Filter',
                description: allVms.isEmpty
                    ? 'Get started by creating your first Alpine Linux virtual machine.'
                    : 'Try clearing your search query or changing the state filter.',
                actionLabel: allVms.isEmpty ? 'Create Virtual Machine' : null,
                onAction: allVms.isEmpty ? _showCreateVmWizard : null,
              )
            else if (_isGridView)
              LayoutBuilder(
                builder: (context, constraints) {
                  final crossAxisCount = constraints.maxWidth > 900 ? 2 : 1;
                  return GridView.builder(
                    shrinkWrap: true,
                    physics: const NeverScrollableScrollPhysics(),
                    gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                      crossAxisCount: crossAxisCount,
                      crossAxisSpacing: 14,
                      mainAxisSpacing: 14,
                      mainAxisExtent: 210,
                    ),
                    itemCount: filteredVms.length,
                    itemBuilder: (context, idx) {
                      final vm = filteredVms[idx];
                      return VMCard(
                        vm: vm,
                        isSelected: widget.service.selectedVmId == vm.vmId,
                        onSelect: () => widget.service.selectVm(vm.vmId),
                        onViewConsole: () => _showConsoleDialog(vm.vmId, vm.name),
                        onStart: () async {
                          final ok = await widget.service.startVm(vm.vmId);
                          if (!ok && context.mounted) {
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(content: Text(widget.service.lastError ?? 'Failed to start VM')),
                            );
                          }
                        },
                        onStop: () async {
                          await widget.service.stopVm(vm.vmId, force: false);
                        },
                        onForceStop: () async {
                          final confirm = await ConfirmationDialog.show(
                            context: context,
                            title: 'Force Stop VM',
                            message: 'Are you sure you want to send SIGKILL to ${vm.name}? Any unsaved in-guest data may be lost.',
                            confirmLabel: 'Force Stop',
                            isDestructive: true,
                          );
                          if (confirm == true) {
                            await widget.service.stopVm(vm.vmId, force: true);
                          }
                        },
                        onDelete: () async {
                          final confirm = await ConfirmationDialog.show(
                            context: context,
                            title: 'Delete Virtual Machine',
                            message: 'Are you sure you want to delete ${vm.name} (${vm.vmId})? This action cannot be undone.',
                            confirmLabel: 'Delete',
                            isDestructive: true,
                          );
                          if (confirm == true) {
                            final ok = await widget.service.deleteVm(vm.vmId);
                            if (!ok && context.mounted) {
                              ScaffoldMessenger.of(context).showSnackBar(
                                SnackBar(content: Text(widget.service.lastError ?? 'Failed to delete VM')),
                              );
                            }
                          }
                        },
                      );
                    },
                  );
                },
              )
            else
              _buildTableView(context, filteredVms),
          ],
        ),
      ),
    );
  }

  Widget _buildTableView(BuildContext context, List<VmModel> vms) {
    final colors = VosColors.of(context);

    return Container(
      decoration: BoxDecoration(
        color: colors.card,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: colors.border),
      ),
      child: Table(
        columnWidths: const {
          0: FlexColumnWidth(3),
          1: FlexColumnWidth(2),
          2: FlexColumnWidth(2),
          3: FlexColumnWidth(2),
          4: FlexColumnWidth(3),
        },
        children: [
          TableRow(
            decoration: BoxDecoration(
              color: colors.bg,
              borderRadius: const BorderRadius.vertical(top: Radius.circular(9)),
            ),
            children: [
              _cell(context, 'Name / Identifier', isHeader: true),
              _cell(context, 'Architecture', isHeader: true),
              _cell(context, 'vCPU & RAM', isHeader: true),
              _cell(context, 'State', isHeader: true),
              _cell(context, 'Actions', isHeader: true, alignRight: true),
            ],
          ),
          ...vms.map((vm) {
            final isSelected = widget.service.selectedVmId == vm.vmId;
            return TableRow(
              children: [
                Padding(
                  padding: const EdgeInsets.all(12),
                  child: Row(
                    children: [
                      Icon(
                        Icons.dns_rounded,
                        size: 16,
                        color: vm.isRunning ? colors.success : colors.textMuted,
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              vm.name.isNotEmpty ? vm.name : vm.vmId,
                              style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: colors.textPrimary),
                            ),
                            Text(
                              vm.vmId,
                              style: TextStyle(fontSize: 11, fontFamily: 'monospace', color: colors.textMuted),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
                Padding(
                  padding: const EdgeInsets.all(12),
                  child: Text(vm.guestArch, style: TextStyle(fontSize: 12, color: colors.textSecondary)),
                ),
                Padding(
                  padding: const EdgeInsets.all(12),
                  child: Text('${vm.vcpuCount} cores · ${vm.ramMb} MB', style: TextStyle(fontSize: 12, color: colors.textSecondary)),
                ),
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                  child: StatusBadge(label: vm.state, isSmall: true),
                ),
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.end,
                    children: [
                      TextButton(
                        onPressed: () => widget.service.selectVm(vm.vmId),
                        child: Text(isSelected ? 'Selected' : 'Select'),
                      ),
                      IconButton(
                        icon: Icon(Icons.terminal_rounded, color: colors.primary, size: 18),
                        tooltip: 'View Boot Console Log',
                        onPressed: () => _showConsoleDialog(vm.vmId, vm.name),
                      ),
                      if (!vm.isRunning)
                        IconButton(
                          icon: Icon(Icons.play_arrow_rounded, color: colors.success, size: 18),
                          tooltip: 'Start VM',
                          onPressed: () => widget.service.startVm(vm.vmId),
                        )
                      else
                        IconButton(
                          icon: Icon(Icons.stop_rounded, color: colors.warning, size: 18),
                          tooltip: 'Stop VM',
                          onPressed: () => widget.service.stopVm(vm.vmId),
                        ),
                    ],
                  ),
                ),
              ],
            );
          }),
        ],
      ),
    );
  }

  Widget _cell(BuildContext context, String title, {bool isHeader = false, bool alignRight = false}) {
    final colors = VosColors.of(context);
    return Padding(
      padding: const EdgeInsets.all(12),
      child: Text(
        title,
        textAlign: alignRight ? TextAlign.right : TextAlign.left,
        style: TextStyle(
          fontSize: isHeader ? 11 : 12,
          fontWeight: isHeader ? FontWeight.w600 : FontWeight.normal,
          color: isHeader ? colors.textSecondary : colors.textPrimary,
        ),
      ),
    );
  }

  void _showConsoleDialog(String vmId, String vmName) {
    showDialog(
      context: context,
      builder: (dialogCtx) => _VmConsoleDialog(
        service: widget.service,
        vmId: vmId,
        vmName: vmName,
      ),
    );
  }
}

/// Live In-App Boot Console Stream Dialog
class _VmConsoleDialog extends StatefulWidget {
  final VosService service;
  final String vmId;
  final String vmName;

  const _VmConsoleDialog({
    required this.service,
    required this.vmId,
    required this.vmName,
  });

  @override
  State<_VmConsoleDialog> createState() => _VmConsoleDialogState();
}

class _VmConsoleDialogState extends State<_VmConsoleDialog> {
  String _logContent = 'Connecting to guest boot stream...';
  bool _isLoading = true;
  Timer? _refreshTimer;
  final ScrollController _scrollCtrl = ScrollController();

  @override
  void initState() {
    super.initState();
    _fetchLog();
    _refreshTimer = Timer.periodic(const Duration(seconds: 2), (_) => _fetchLog(silent: true));
  }

  @override
  void dispose() {
    _refreshTimer?.cancel();
    _scrollCtrl.dispose();
    super.dispose();
  }

  Future<void> _fetchLog({bool silent = false}) async {
    if (!silent) setState(() => _isLoading = true);
    final log = await widget.service.getVmConsoleLog(widget.vmId);
    if (mounted) {
      setState(() {
        _logContent = log;
        _isLoading = false;
      });
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (_scrollCtrl.hasClients) {
          _scrollCtrl.jumpTo(_scrollCtrl.position.maxScrollExtent);
        }
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);

    return AlertDialog(
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
                child: Icon(Icons.terminal_rounded, size: 18, color: colors.primary),
              ),
              const SizedBox(width: 10),
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Boot Console · ${widget.vmName}',
                    style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: colors.textPrimary),
                  ),
                  Text(
                    'Live QEMU serial & Linux kernel boot logs (${widget.vmId})',
                    style: TextStyle(fontSize: 11, color: colors.textMuted),
                  ),
                ],
              ),
            ],
          ),
          Row(
            children: [
              IconButton(
                icon: const Icon(Icons.refresh, size: 18),
                tooltip: 'Refresh Console',
                onPressed: () => _fetchLog(),
              ),
              IconButton(
                icon: const Icon(Icons.copy_rounded, size: 18),
                tooltip: 'Copy Output to Clipboard',
                onPressed: () {
                  Clipboard.setData(ClipboardData(text: _logContent));
                  ScaffoldMessenger.of(context).showSnackBar(
                    const SnackBar(content: Text('Console output copied to clipboard')),
                  );
                },
              ),
              IconButton(
                icon: const Icon(Icons.close, size: 18),
                onPressed: () => Navigator.of(context).pop(),
              ),
            ],
          ),
        ],
      ),
      content: Container(
        width: 720,
        height: 420,
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: const Color(0xFF090D14),
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: colors.borderSubtle),
        ),
        child: _isLoading && _logContent.isEmpty
            ? const Center(child: CircularProgressIndicator())
            : SingleChildScrollView(
                controller: _scrollCtrl,
                child: SelectableText(
                  _logContent,
                  style: const TextStyle(
                    fontFamily: 'monospace',
                    fontSize: 11.5,
                    color: Color(0xFF34D399),
                    height: 1.4,
                  ),
                ),
              ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Close'),
        ),
      ],
    );
  }
}

/// Multi-step VM Creation Wizard Dialog
class _VmCreationWizard extends StatefulWidget {
  final VosService service;

  const _VmCreationWizard({required this.service});

  @override
  State<_VmCreationWizard> createState() => _VmCreationWizardState();
}

class _VmCreationWizardState extends State<_VmCreationWizard> {
  int _currentStep = 0;

  final _idCtrl = TextEditingController(text: 'alpine-vm-01');
  final _nameCtrl = TextEditingController(text: 'Alpine Linux Instance');
  int _vcpuCount = 2;
  int _ramMb = 1024;
  final _diskCtrl = TextEditingController(text: 'vm_images/alpine.qcow2');
  final _isoCtrl = TextEditingController(text: 'vm_images/alpine-arm64.iso');

  @override
  void dispose() {
    _idCtrl.dispose();
    _nameCtrl.dispose();
    _diskCtrl.dispose();
    _isoCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);

    return AlertDialog(
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
                child: Icon(Icons.add_to_queue_rounded, size: 18, color: colors.primary),
              ),
              const SizedBox(width: 10),
              Text(
                'Create Virtual Machine',
                style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: colors.textPrimary),
              ),
            ],
          ),
          Text(
            'Step ${_currentStep + 1} of 3',
            style: TextStyle(fontSize: 12, color: colors.textMuted),
          ),
        ],
      ),
      content: SizedBox(
        width: 500,
        child: _buildStepContent(context),
      ),
      actionsPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      actions: [
        if (_currentStep > 0)
          OutlinedButton(
            onPressed: () => setState(() => _currentStep--),
            child: const Text('Back'),
          )
        else
          OutlinedButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('Cancel'),
          ),
        if (_currentStep < 2)
          ElevatedButton(
            onPressed: () => setState(() => _currentStep++),
            child: const Text('Next'),
          )
        else
          ElevatedButton.icon(
            icon: const Icon(Icons.check, size: 16),
            label: const Text('Create VM'),
            onPressed: _submitVmCreation,
          ),
      ],
    );
  }

  Widget _buildStepContent(BuildContext context) {
    final colors = VosColors.of(context);

    switch (_currentStep) {
      case 0: // Step 1: Basic Identity
        return Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Step 1: Virtual Machine Identity',
              style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: colors.textPrimary),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _idCtrl,
              decoration: const InputDecoration(labelText: 'Unique VM Identifier (e.g., alpine-01)'),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _nameCtrl,
              decoration: const InputDecoration(labelText: 'Display Name (e.g., Production Alpine Node)'),
            ),
          ],
        );
      case 1: // Step 2: CPU & Memory Allocation
        return Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Step 2: Resource Allocation (QEMU SMP & RAM)',
              style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: colors.textPrimary),
            ),
            const SizedBox(height: 16),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text('vCPU Cores: $_vcpuCount', style: TextStyle(fontSize: 13, color: colors.textPrimary)),
                Text('Max 8 cores on Apple Silicon', style: TextStyle(fontSize: 11, color: colors.textMuted)),
              ],
            ),
            Slider(
              value: _vcpuCount.toDouble(),
              min: 1,
              max: 8,
              divisions: 7,
              activeColor: colors.primary,
              label: '$_vcpuCount Cores',
              onChanged: (v) => setState(() => _vcpuCount = v.toInt()),
            ),
            const SizedBox(height: 12),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text('Guest RAM: $_ramMb MB', style: TextStyle(fontSize: 13, color: colors.textPrimary)),
                Text('${(_ramMb / 1024).toStringAsFixed(1)} GB', style: TextStyle(fontSize: 11, color: colors.textMuted)),
              ],
            ),
            Slider(
              value: _ramMb.toDouble(),
              min: 512,
              max: 8192,
              divisions: 15,
              activeColor: colors.accent,
              label: '$_ramMb MB',
              onChanged: (v) => setState(() => _ramMb = v.toInt()),
            ),
          ],
        );
      case 2: // Step 3: Storage & Summary
      default:
        return Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Step 3: Storage Configuration & Review',
              style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: colors.textPrimary),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _diskCtrl,
              decoration: const InputDecoration(labelText: 'Primary Disk Image Path (.qcow2 / .raw)'),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _isoCtrl,
              decoration: const InputDecoration(labelText: 'Boot ISO / Installation Image (.iso)'),
            ),
            const SizedBox(height: 16),
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: colors.bg,
                borderRadius: BorderRadius.circular(6),
                border: Border.all(color: colors.borderSubtle),
              ),
              child: Column(
                children: [
                  _reviewRow(context, 'ID / Name', '${_idCtrl.text} (${_nameCtrl.text})'),
                  const SizedBox(height: 4),
                  _reviewRow(context, 'vCPUs / RAM', '$_vcpuCount cores / $_ramMb MB'),
                  const SizedBox(height: 4),
                  _reviewRow(context, 'Architecture', 'aarch64 (ARM64)'),
                  const SizedBox(height: 4),
                  _reviewRow(context, 'Hypervisor', 'macOS HVF Accelerated'),
                ],
              ),
            ),
          ],
        );
    }
  }

  Widget _reviewRow(BuildContext context, String label, String value) {
    final colors = VosColors.of(context);
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(label, style: TextStyle(fontSize: 11, color: colors.textMuted)),
        Text(value, style: TextStyle(fontSize: 11, fontWeight: FontWeight.w600, color: colors.textPrimary)),
      ],
    );
  }

  Future<void> _submitVmCreation() async {
    final messenger = ScaffoldMessenger.of(context);
    Navigator.of(context).pop();

    final success = await widget.service.createVm(
      vmId: _idCtrl.text.trim(),
      name: _nameCtrl.text.trim().isNotEmpty ? _nameCtrl.text.trim() : _idCtrl.text.trim(),
      vcpuCount: _vcpuCount,
      ramMb: _ramMb,
      diskPath: _diskCtrl.text.trim(),
      isoPath: _isoCtrl.text.trim(),
    );

    if (mounted) {
      if (success) {
        messenger.showSnackBar(
          const SnackBar(content: Text('Virtual Machine created successfully!'), backgroundColor: VosColors.success),
        );
      } else {
        messenger.showSnackBar(
          SnackBar(content: Text(widget.service.lastError ?? 'Failed to create VM'), backgroundColor: VosColors.danger),
        );
      }
    }
  }
}
