import 'package:flutter/material.dart';

import '../../core/theme.dart';
import '../../application/vos_service.dart';
import '../../data/models/profile_model.dart';
import '../../widgets/resource_profile_card.dart';
import '../../widgets/section_header.dart';
import '../../widgets/status_badge.dart';
import '../../widgets/confirmation_dialog.dart';

class ProfileScreen extends StatefulWidget {
  final VosService service;

  const ProfileScreen({super.key, required this.service});

  @override
  State<ProfileScreen> createState() => _ProfileScreenState();
}

class _ProfileScreenState extends State<ProfileScreen> {
  // Active Interactive Console State
  double _activeCpuCores = 1.0;
  int _activeCpuWeight = 100;
  double _activeMemMb = 256;
  int _activePidsMax = 50;
  double _activeIoMb = 10;
  String _selectedPresetName = 'Balanced';
  bool _isVerified = true;

  @override
  void initState() {
    super.initState();
    _applyPreset('Balanced');
  }

  void _applyPreset(String preset) {
    setState(() {
      _selectedPresetName = preset;
      switch (preset) {
        case 'Lightweight':
          _activeCpuCores = 0.5;
          _activeCpuWeight = 50;
          _activeMemMb = 128;
          _activePidsMax = 25;
          _activeIoMb = 5;
          break;
        case 'Balanced':
          _activeCpuCores = 1.0;
          _activeCpuWeight = 100;
          _activeMemMb = 256;
          _activePidsMax = 50;
          _activeIoMb = 10;
          break;
        case 'High Performance':
          _activeCpuCores = 2.0;
          _activeCpuWeight = 200;
          _activeMemMb = 512;
          _activePidsMax = 100;
          _activeIoMb = 50;
          break;
      }
      _isVerified = true;
    });
  }

  void _showProfileDialog({ResourceProfile? existing}) {
    final colors = VosColors.of(context);
    final idController = TextEditingController(text: existing?.profileId ?? '');
    final nameController = TextEditingController(text: existing?.name ?? '');
    final descController = TextEditingController(
      text: existing?.description ?? '',
    );
    double cpuCores = existing?.cpuCores ?? _activeCpuCores;
    int cpuWeight = existing?.cpuWeight ?? _activeCpuWeight;
    int memMb = existing != null
        ? (existing.memoryMaxBytes / (1024 * 1024)).round()
        : _activeMemMb.toInt();
    int pidsMax = existing?.pidsMax ?? _activePidsMax;
    int ioMb = existing != null
        ? (existing.ioReadBps / (1024 * 1024)).round()
        : _activeIoMb.toInt();

    final isEdit = existing != null;

    showDialog(
      context: context,
      builder: (context) {
        return StatefulBuilder(
          builder: (context, setDialogState) {
            return AlertDialog(
              backgroundColor: colors.card,
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(VeyloraRadius.lg),
                side: BorderSide(color: colors.border),
              ),
              title: Row(
                children: [
                  Container(
                    padding: const EdgeInsets.all(VeyloraSpacing.xs + 2),
                    decoration: BoxDecoration(
                      color: colors.primary.withValues(alpha: 0.15),
                      borderRadius: BorderRadius.circular(VeyloraRadius.sm),
                    ),
                    child: Icon(
                      Icons.tune_rounded,
                      size: 18,
                      color: colors.primary,
                    ),
                  ),
                  const SizedBox(width: VeyloraSpacing.sm),
                  Text(
                    isEdit ? 'Edit Resource Profile' : 'Create Custom Profile',
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w600,
                      color: colors.textPrimary,
                    ),
                  ),
                ],
              ),
              content: SizedBox(
                width: 520,
                child: SingleChildScrollView(
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      TextField(
                        controller: idController,
                        enabled: !isEdit,
                        style: TextStyle(
                          fontSize: 13,
                          fontFamily: VeyloraTypography.fontMono,
                          color: colors.textPrimary,
                        ),
                        decoration: InputDecoration(
                          labelText: 'Profile Identifier (e.g., worker_limited)',
                          labelStyle: TextStyle(fontSize: 12, color: colors.textMuted),
                        ),
                      ),
                      const SizedBox(height: VeyloraSpacing.sm),
                      TextField(
                        controller: nameController,
                        style: TextStyle(
                          fontSize: 13,
                          color: colors.textPrimary,
                        ),
                        decoration: InputDecoration(
                          labelText: 'Profile Display Name',
                          labelStyle: TextStyle(fontSize: 12, color: colors.textMuted),
                        ),
                      ),
                      const SizedBox(height: VeyloraSpacing.sm),
                      TextField(
                        controller: descController,
                        style: TextStyle(
                          fontSize: 13,
                          color: colors.textPrimary,
                        ),
                        decoration: InputDecoration(
                          labelText: 'Description (Optional)',
                          labelStyle: TextStyle(fontSize: 12, color: colors.textMuted),
                        ),
                      ),
                      const SizedBox(height: VeyloraSpacing.md),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Text(
                            'CPU Quota (cpu.max):',
                            style: TextStyle(
                              fontSize: 12,
                              fontWeight: FontWeight.w600,
                              color: colors.primary,
                            ),
                          ),
                          Text(
                            '${cpuCores.toStringAsFixed(2)} Cores (CFS Weight: $cpuWeight)',
                            style: TextStyle(
                              fontSize: 12,
                              fontFamily: VeyloraTypography.fontMono,
                              color: colors.textSecondary,
                            ),
                          ),
                        ],
                      ),
                      Slider(
                        value: cpuCores,
                        min: 0.1,
                        max: 4.0,
                        divisions: 39,
                        activeColor: colors.primary,
                        onChanged: (v) => setDialogState(() => cpuCores = v),
                      ),
                      const SizedBox(height: VeyloraSpacing.xs),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Text(
                            'Memory Limit (memory.max):',
                            style: TextStyle(
                              fontSize: 12,
                              fontWeight: FontWeight.w600,
                              color: colors.accent,
                            ),
                          ),
                          Text(
                            '$memMb MB',
                            style: TextStyle(
                              fontSize: 12,
                              fontFamily: VeyloraTypography.fontMono,
                              color: colors.textSecondary,
                            ),
                          ),
                        ],
                      ),
                      Slider(
                        value: memMb.toDouble(),
                        min: 32,
                        max: 2048,
                        divisions: 63,
                        activeColor: colors.accent,
                        onChanged: (v) =>
                            setDialogState(() => memMb = v.toInt()),
                      ),
                      const SizedBox(height: VeyloraSpacing.xs),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Text(
                            'Process Ceiling (pids.max):',
                            style: TextStyle(
                              fontSize: 12,
                              fontWeight: FontWeight.w600,
                              color: colors.warning,
                            ),
                          ),
                          Text(
                            '$pidsMax Tasks',
                            style: TextStyle(
                              fontSize: 12,
                              fontFamily: VeyloraTypography.fontMono,
                              color: colors.textSecondary,
                            ),
                          ),
                        ],
                      ),
                      Slider(
                        value: pidsMax.toDouble(),
                        min: 5,
                        max: 500,
                        divisions: 99,
                        activeColor: colors.warning,
                        onChanged: (v) =>
                            setDialogState(() => pidsMax = v.toInt()),
                      ),
                      const SizedBox(height: VeyloraSpacing.xs),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Text(
                            'Disk Throughput (io.max):',
                            style: TextStyle(
                              fontSize: 12,
                              fontWeight: FontWeight.w600,
                              color: colors.layerGuest,
                            ),
                          ),
                          Text(
                            '$ioMb MB/s Read & Write',
                            style: TextStyle(
                              fontSize: 12,
                              fontFamily: VeyloraTypography.fontMono,
                              color: colors.textSecondary,
                            ),
                          ),
                        ],
                      ),
                      Slider(
                        value: ioMb.toDouble(),
                        min: 1,
                        max: 100,
                        divisions: 99,
                        activeColor: colors.layerGuest,
                        onChanged: (v) =>
                            setDialogState(() => ioMb = v.toInt()),
                      ),
                    ],
                  ),
                ),
              ),
              actionsPadding: const EdgeInsets.symmetric(
                horizontal: VeyloraSpacing.lg,
                vertical: VeyloraSpacing.md,
              ),
              actions: [
                OutlinedButton(
                  onPressed: () => Navigator.of(context).pop(),
                  child: const Text('Cancel'),
                ),
                ElevatedButton(
                  onPressed: () async {
                    final pid = idController.text.trim();
                    final name = nameController.text.trim();
                    if (pid.isEmpty || name.isEmpty) return;

                    final messenger = ScaffoldMessenger.of(context);
                    Navigator.of(context).pop();

                    final success = await widget.service.saveProfile(
                      profileId: pid,
                      name: name,
                      description: descController.text.trim(),
                      cpuCores: cpuCores,
                      cpuWeight: cpuWeight,
                      memoryMaxBytes: memMb * 1024 * 1024,
                      pidsMax: pidsMax,
                      ioReadBps: ioMb * 1024 * 1024,
                      ioWriteBps: ioMb * 1024 * 1024,
                    );

                    if (mounted) {
                      if (success) {
                        messenger.showSnackBar(
                          SnackBar(
                            content: Text(
                              'Profile ${isEdit ? "updated" : "saved"} successfully!',
                            ),
                            backgroundColor: VosColors.success,
                          ),
                        );
                      } else {
                        messenger.showSnackBar(
                          SnackBar(
                            content: Text(
                              widget.service.lastError ??
                                  'Failed to save profile',
                            ),
                            backgroundColor: VosColors.danger,
                          ),
                        );
                      }
                    }
                  },
                  child: Text(isEdit ? 'Save Changes' : 'Create Profile'),
                ),
              ],
            );
          },
        );
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);
    final profiles = widget.service.profiles;

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
            // Top Section Header & Status Badges
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Resource Governance & Limits',
                        style: TextStyle(
                          fontSize: 20,
                          fontWeight: FontWeight.w700,
                          color: colors.textPrimary,
                          letterSpacing: -0.4,
                        ),
                      ),
                      const SizedBox(height: 2),
                      Text(
                        'Linux cgroups v2 kernel controllers · Hardware limit envelopes & runtime throttling',
                        style: TextStyle(fontSize: 12, color: colors.textSecondary),
                        overflow: TextOverflow.ellipsis,
                        maxLines: 1,
                      ),
                    ],
                  ),
                ),
                const SizedBox(width: 12),
                Row(
                  children: [
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                      decoration: BoxDecoration(
                        color: colors.card,
                        borderRadius: BorderRadius.circular(VeyloraRadius.sm),
                        border: Border.all(color: colors.border),
                      ),
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(Icons.terminal_rounded, size: 14, color: colors.primary),
                          const SizedBox(width: 6),
                          Text(
                            '/sys/fs/cgroup/vos/',
                            style: TextStyle(
                              fontSize: 11,
                              fontFamily: VeyloraTypography.fontMono,
                              color: colors.textSecondary,
                            ),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(width: 8),
                    const StatusBadge(
                      label: 'CGROUPS V2 LIVE',
                      color: VosColors.success,
                    ),
                  ],
                ),
              ],
            ),
            const SizedBox(height: VeyloraSpacing.md),

            // Hardware Architecture & Subsystem Banner
            Container(
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
                      color: colors.primary.withValues(alpha: 0.12),
                      borderRadius: BorderRadius.circular(VeyloraRadius.sm),
                    ),
                    child: Icon(Icons.tune_rounded, size: 16, color: colors.primary),
                  ),
                  const SizedBox(width: VeyloraSpacing.sm),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'Hierarchical Linux Cgroups v2 Kernel Subsystem',
                          style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: colors.textPrimary,
                          ),
                        ),
                        const SizedBox(height: 1),
                        Text(
                          'Active controllers: cpu, memory, pids, io · Kernel readback validation enforced on every profile execution.',
                          style: TextStyle(
                            fontSize: 11,
                            color: colors.textSecondary,
                          ),
                        ),
                      ],
                    ),
                  ),
                  // GPU Status Badge
                  Tooltip(
                    message:
                        'macOS Hypervisor Framework (HVF) runs headless guest kernels without hardware 3D acceleration. Workloads automatically use CPU software rendering (llvmpipe).',
                    child: Container(
                      padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 4),
                      decoration: BoxDecoration(
                        color: colors.warning.withValues(alpha: 0.12),
                        borderRadius: BorderRadius.circular(VeyloraRadius.full),
                        border: Border.all(
                          color: colors.warning.withValues(alpha: 0.3),
                        ),
                      ),
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(
                            Icons.videogame_asset_off_rounded,
                            size: 13,
                            color: colors.warning,
                          ),
                          const SizedBox(width: 5),
                          Text(
                            'VGPU: UNSUPPORTED (CPU FALLBACK)',
                            style: TextStyle(
                              fontSize: 10,
                              fontFamily: VeyloraTypography.fontMono,
                              fontWeight: FontWeight.w700,
                              color: colors.warning,
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: VeyloraSpacing.lg),

            // Section 1: Preset Profiles Catalog
            SectionHeader(
              title: 'Resource Profiles Catalog (${profiles.length})',
              subtitle: 'Pre-configured resource envelopes with verified kernel constraints',
              icon: Icons.layers_rounded,
              trailing: ElevatedButton.icon(
                icon: const Icon(Icons.add, size: 15),
                label: const Text('New Custom Profile'),
                onPressed: () => _showProfileDialog(),
              ),
            ),
            const SizedBox(height: VeyloraSpacing.xs),
            LayoutBuilder(
              builder: (context, constraints) {
                final count = constraints.maxWidth > 900 ? 3 : 1;
                return GridView.builder(
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                    crossAxisCount: count,
                    crossAxisSpacing: VeyloraSpacing.md,
                    mainAxisSpacing: VeyloraSpacing.md,
                    mainAxisExtent: 315,
                  ),
                  itemCount: profiles.length,
                  itemBuilder: (context, idx) {
                    final prof = profiles[idx];
                    return ResourceProfileCard(
                      profile: prof,
                      onEdit: () => _showProfileDialog(existing: prof),
                      onDelete: prof.isDefault
                          ? null
                          : () async {
                              final confirm = await ConfirmationDialog.show(
                                context: context,
                                title: 'Delete Profile',
                                message:
                                    'Are you sure you want to delete profile "${prof.name}"? Applications referencing it will be unassigned.',
                                confirmLabel: 'Delete',
                                isDestructive: true,
                              );
                              if (confirm == true) {
                                final ok = await widget.service.deleteProfile(
                                  prof.profileId,
                                );
                                if (!ok && context.mounted) {
                                  ScaffoldMessenger.of(context).showSnackBar(
                                    SnackBar(
                                      content: Text(
                                        widget.service.lastError ??
                                            'Cannot delete profile',
                                      ),
                                    ),
                                  );
                                }
                              }
                            },
                    );
                  },
                );
              },
            ),
            const SizedBox(height: VeyloraSpacing.xxl),

            // Section 2: Interactive Fine-Grained Resource Control Console
            SectionHeader(
              title: 'Interactive Resource Control Console',
              subtitle: 'Fine-tune cgroups v2 parameters with direct kernel parameter mapping',
              icon: Icons.dashboard_customize_rounded,
            ),
            const SizedBox(height: VeyloraSpacing.xs),
            Container(
              padding: const EdgeInsets.all(VeyloraSpacing.lg),
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: BorderRadius.circular(VeyloraRadius.lg),
                border: Border.all(color: colors.border),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  // Presets Quick-Bar
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    crossAxisAlignment: WrapCrossAlignment.center,
                    children: [
                      Text(
                        'Preset Templates:',
                        style: TextStyle(
                          fontSize: 12,
                          fontWeight: FontWeight.w600,
                          color: colors.textSecondary,
                        ),
                      ),
                      ...['Lightweight', 'Balanced', 'High Performance'].map((
                        preset,
                      ) {
                        final isSel = _selectedPresetName == preset;
                        return ChoiceChip(
                          label: Text(
                            preset,
                            style: TextStyle(
                              fontSize: 11,
                              fontWeight: isSel ? FontWeight.w600 : FontWeight.w500,
                              color: isSel
                                  ? colors.primary
                                  : colors.textSecondary,
                            ),
                          ),
                          selected: isSel,
                          selectedColor: colors.primary.withValues(
                            alpha: 0.15,
                          ),
                          backgroundColor: colors.bg,
                          side: BorderSide(
                            color: isSel ? colors.primary : colors.border,
                          ),
                          onSelected: (_) => _applyPreset(preset),
                        );
                      }),
                    ],
                  ),
                  const SizedBox(height: VeyloraSpacing.lg),

                  // 4 Control Columns (CPU, Memory, Process, I/O)
                  LayoutBuilder(
                    builder: (context, constraints) {
                      final isWide = constraints.maxWidth > 800;
                      return GridView.count(
                        crossAxisCount: isWide ? 2 : 1,
                        crossAxisSpacing: VeyloraSpacing.md,
                        mainAxisSpacing: VeyloraSpacing.md,
                        shrinkWrap: true,
                        physics: const NeverScrollableScrollPhysics(),
                        childAspectRatio: isWide ? 2.5 : 2.0,
                        children: [
                          // 1. CPU Control
                          _controlCard(
                            context,
                            title: 'CPU Control (cpu.max & cpu.weight)',
                            subtitle:
                                'Quota: ${(_activeCpuCores * 100000).round()}µs / 100000µs · Weight: $_activeCpuWeight',
                            icon: Icons.speed_rounded,
                            accentColor: colors.layerHost,
                            valueDisplay: '${_activeCpuCores.toStringAsFixed(2)} Cores',
                            child: Slider(
                              value: _activeCpuCores,
                              min: 0.25,
                              max: 4.0,
                              divisions: 15,
                              activeColor: colors.layerHost,
                              onChanged: (v) => setState(() {
                                _activeCpuCores = v;
                                _selectedPresetName = 'Custom';
                              }),
                            ),
                          ),

                          // 2. Memory Control
                          _controlCard(
                            context,
                            title: 'Memory Control (memory.max)',
                            subtitle:
                                'Hard limit: ${_activeMemMb.toInt()} MB (OOM killer armed on overflow)',
                            icon: Icons.storage_rounded,
                            accentColor: colors.accent,
                            valueDisplay: '${_activeMemMb.toInt()} MB',
                            child: Slider(
                              value: _activeMemMb,
                              min: 64,
                              max: 2048,
                              divisions: 31,
                              activeColor: colors.accent,
                              onChanged: (v) => setState(() {
                                _activeMemMb = v;
                                _selectedPresetName = 'Custom';
                              }),
                            ),
                          ),

                          // 3. Process Control
                          _controlCard(
                            context,
                            title: 'Process Control (pids.max)',
                            subtitle:
                                'Task ceiling: $_activePidsMax (Protects against fork-bombs)',
                            icon: Icons.list_alt_rounded,
                            accentColor: colors.warning,
                            valueDisplay: '$_activePidsMax Tasks',
                            child: Slider(
                              value: _activePidsMax.toDouble(),
                              min: 10,
                              max: 200,
                              divisions: 19,
                              activeColor: colors.warning,
                              onChanged: (v) => setState(() {
                                _activePidsMax = v.toInt();
                                _selectedPresetName = 'Custom';
                              }),
                            ),
                          ),

                          // 4. Disk I/O Control
                          _controlCard(
                            context,
                            title: 'Disk I/O Control (io.max)',
                            subtitle:
                                'Throughput: ${_activeIoMb.toInt()} MB/s Read & Write (device 8:0)',
                            icon: Icons.album_rounded,
                            accentColor: colors.layerGuest,
                            valueDisplay: '${_activeIoMb.toInt()} MB/s',
                            child: Slider(
                              value: _activeIoMb,
                              min: 1,
                              max: 100,
                              divisions: 99,
                              activeColor: colors.layerGuest,
                              onChanged: (v) => setState(() {
                                _activeIoMb = v;
                                _selectedPresetName = 'Custom';
                              }),
                            ),
                          ),
                        ],
                      );
                    },
                  ),
                  const SizedBox(height: VeyloraSpacing.md),

                  // Bottom Verification Status & Apply Bar
                  Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: VeyloraSpacing.md,
                      vertical: VeyloraSpacing.sm + 2,
                    ),
                    decoration: BoxDecoration(
                      color: colors.bg,
                      borderRadius: BorderRadius.circular(VeyloraRadius.md),
                      border: Border.all(color: colors.borderSubtle),
                    ),
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        Expanded(
                          child: Row(
                            children: [
                              Icon(
                                _isVerified
                                    ? Icons.verified_user_rounded
                                    : Icons.info_outline,
                                size: 16,
                                color: _isVerified
                                    ? colors.success
                                    : colors.warning,
                              ),
                              const SizedBox(width: VeyloraSpacing.sm),
                              Expanded(
                                child: Text(
                                  _isVerified
                                      ? 'Configuration verified against Linux cgroups v2 kernel interface.'
                                      : 'Configuration modified. Save to persist profile in SQLite.',
                                  style: TextStyle(
                                    fontSize: 12,
                                    color: colors.textSecondary,
                                  ),
                                  overflow: TextOverflow.ellipsis,
                                  maxLines: 1,
                                ),
                              ),
                            ],
                          ),
                        ),
                        const SizedBox(width: 12),
                        ElevatedButton.icon(
                          icon: const Icon(Icons.save_rounded, size: 14),
                          label: const Text('Save as Profile'),
                          onPressed: () {
                            _showProfileDialog();
                          },
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _controlCard(
    BuildContext context, {
    required String title,
    required String subtitle,
    required IconData icon,
    required Color accentColor,
    required String valueDisplay,
    required Widget child,
  }) {
    final colors = VosColors.of(context);
    return Container(
      padding: const EdgeInsets.all(VeyloraSpacing.md),
      decoration: BoxDecoration(
        color: colors.bg,
        borderRadius: BorderRadius.circular(VeyloraRadius.md),
        border: Border.all(color: colors.borderSubtle),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Row(
                children: [
                  Container(
                    padding: const EdgeInsets.all(6),
                    decoration: BoxDecoration(
                      color: accentColor.withValues(alpha: 0.12),
                      borderRadius: BorderRadius.circular(VeyloraRadius.sm),
                    ),
                    child: Icon(icon, size: 15, color: accentColor),
                  ),
                  const SizedBox(width: 8),
                  Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        title,
                        style: TextStyle(
                          fontSize: 13,
                          fontWeight: FontWeight.w600,
                          color: colors.textPrimary,
                        ),
                      ),
                      Text(
                        subtitle,
                        style: TextStyle(fontSize: 10.5, color: colors.textMuted),
                        overflow: TextOverflow.ellipsis,
                      ),
                    ],
                  ),
                ],
              ),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                decoration: BoxDecoration(
                  color: colors.card,
                  borderRadius: BorderRadius.circular(VeyloraRadius.xs),
                  border: Border.all(color: colors.border),
                ),
                child: Text(
                  valueDisplay,
                  style: TextStyle(
                    fontSize: 11,
                    fontFamily: VeyloraTypography.fontMono,
                    fontWeight: FontWeight.w600,
                    color: accentColor,
                  ),
                ),
              ),
            ],
          ),
          child,
        ],
      ),
    );
  }
}
