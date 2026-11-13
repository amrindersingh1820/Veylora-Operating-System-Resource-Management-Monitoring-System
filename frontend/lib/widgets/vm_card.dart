import 'package:flutter/material.dart';
import '../core/theme.dart';
import '../data/models/vm_model.dart';
import 'status_badge.dart';

class VMCard extends StatefulWidget {
  final VmModel vm;
  final bool isSelected;
  final VoidCallback onSelect;
  final VoidCallback onStart;
  final VoidCallback onStop;
  final VoidCallback onForceStop;
  final VoidCallback onDelete;
  final VoidCallback? onViewConsole;

  const VMCard({
    super.key,
    required this.vm,
    required this.isSelected,
    required this.onSelect,
    required this.onStart,
    required this.onStop,
    required this.onForceStop,
    required this.onDelete,
    this.onViewConsole,
  });

  @override
  State<VMCard> createState() => _VMCardState();
}

class _VMCardState extends State<VMCard> {
  bool _isHovered = false;

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);
    final vm = widget.vm;

    return MouseRegion(
      onEnter: (_) => setState(() => _isHovered = true),
      onExit: (_) => setState(() => _isHovered = false),
      child: AnimatedContainer(
        duration: VeyloraMotion.fast,
        decoration: BoxDecoration(
          color: _isHovered ? colors.cardHover : colors.card,
          borderRadius: VeyloraRadius.rLg,
          border: Border.all(
            color: widget.isSelected
                ? colors.primary.withValues(alpha: 0.7)
                : (_isHovered ? colors.primary.withValues(alpha: 0.3) : colors.border),
            width: widget.isSelected ? 1.5 : 1.0,
          ),
          boxShadow: [
            BoxShadow(
              color: Colors.black.withValues(alpha: colors.isDark ? (_isHovered ? 0.3 : 0.15) : 0.03),
              blurRadius: _isHovered ? 12 : 6,
              offset: Offset(0, _isHovered ? 4 : 2),
            ),
          ],
        ),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              // Top Row: VM Icon + Name / ID + Status & Menu
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Row(
                    children: [
                      Container(
                        padding: const EdgeInsets.all(7.5),
                        decoration: BoxDecoration(
                          color: (vm.isRunning ? colors.success : colors.textMuted).withValues(alpha: 0.12),
                          borderRadius: VeyloraRadius.rMd,
                          border: Border.all(
                            color: (vm.isRunning ? colors.success : colors.textMuted).withValues(alpha: 0.25),
                            width: 0.8,
                          ),
                        ),
                        child: Icon(
                          Icons.dns_rounded,
                          size: 16,
                          color: vm.isRunning ? colors.success : colors.textMuted,
                        ),
                      ),
                      const SizedBox(width: 10),
                      Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Row(
                            children: [
                              Text(
                                vm.name.isNotEmpty ? vm.name : vm.vmId,
                                style: TextStyle(
                                  fontSize: 13.5,
                                  fontWeight: FontWeight.w600,
                                  color: colors.textPrimary,
                                  letterSpacing: -0.2,
                                ),
                              ),
                              if (widget.isSelected) ...[
                                const SizedBox(width: 6),
                                Container(
                                  padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 1),
                                  decoration: BoxDecoration(
                                    color: colors.primary.withValues(alpha: 0.15),
                                    borderRadius: VeyloraRadius.rFull,
                                    border: Border.all(color: colors.primary.withValues(alpha: 0.3), width: 0.8),
                                  ),
                                  child: Text(
                                    'ACTIVE',
                                    style: TextStyle(
                                      color: colors.primary,
                                      fontSize: 8.5,
                                      fontWeight: FontWeight.w700,
                                      fontFamily: VeyloraTypography.fontMono,
                                    ),
                                  ),
                                ),
                              ],
                            ],
                          ),
                          Text(
                            vm.vmId,
                            style: TextStyle(
                              fontSize: 10.5,
                              fontFamily: VeyloraTypography.fontMono,
                              color: colors.textMuted,
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                  Row(
                    children: [
                      StatusBadge(label: vm.state, isSmall: true),
                      const SizedBox(width: 4),
                      PopupMenuButton<String>(
                        icon: Icon(Icons.more_vert_rounded, size: 16, color: colors.textSecondary),
                        tooltip: 'VM Options',
                        color: colors.card,
                        shape: RoundedRectangleBorder(
                          borderRadius: VeyloraRadius.rMd,
                          side: BorderSide(color: colors.border),
                        ),
                        onSelected: (val) {
                          if (val == 'console' && widget.onViewConsole != null) widget.onViewConsole!();
                          if (val == 'force_stop') widget.onForceStop();
                          if (val == 'delete') widget.onDelete();
                        },
                        itemBuilder: (ctx) => [
                          if (widget.onViewConsole != null)
                            PopupMenuItem(
                              value: 'console',
                              child: Row(
                                children: [
                                  Icon(Icons.terminal_rounded, size: 15, color: colors.primary),
                                  const SizedBox(width: 8),
                                  const Text('Boot Console Log', style: TextStyle(fontSize: 12)),
                                ],
                              ),
                            ),
                          if (vm.isRunning)
                            PopupMenuItem(
                              value: 'force_stop',
                              child: Row(
                                children: [
                                  Icon(Icons.power_settings_new_rounded, size: 15, color: colors.danger),
                                  const SizedBox(width: 8),
                                  const Text('Force Stop (SIGKILL)', style: TextStyle(fontSize: 12)),
                                ],
                              ),
                            ),
                          PopupMenuItem(
                            value: 'delete',
                            child: Row(
                              children: [
                                Icon(Icons.delete_outline, size: 15, color: colors.danger),
                                const SizedBox(width: 8),
                                const Text('Delete Virtual Machine', style: TextStyle(fontSize: 12)),
                              ],
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                ],
              ),
              const SizedBox(height: 10),

              // Specs Grid: vCPUs, RAM, Arch, Guest Agent
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                decoration: BoxDecoration(
                  color: colors.bg,
                  borderRadius: VeyloraRadius.rMd,
                  border: Border.all(color: colors.borderSubtle),
                ),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.spaceAround,
                  children: [
                    _specItem(context, 'vCPU', '${vm.vcpuCount} cores'),
                    _specDivider(context),
                    _specItem(context, 'RAM', '${vm.ramMb} MB'),
                    _specDivider(context),
                    _specItem(context, 'Arch', vm.guestArch),
                    _specDivider(context),
                    _specItem(
                      context,
                      'Agent',
                      vm.guestReady == 1 ? 'Ready' : 'Standby',
                      valueColor: vm.guestReady == 1 ? colors.success : colors.warning,
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 10),

              // Bottom Actions: Select vs Start / Stop
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  OutlinedButton.icon(
                    style: OutlinedButton.styleFrom(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
                      side: BorderSide(
                        color: widget.isSelected ? colors.primary : colors.border,
                      ),
                    ),
                    icon: Icon(
                      widget.isSelected ? Icons.check_circle_rounded : Icons.radio_button_unchecked,
                      size: 13,
                      color: widget.isSelected ? colors.primary : colors.textSecondary,
                    ),
                    label: Text(
                      widget.isSelected ? 'Selected' : 'Select Target',
                      style: TextStyle(
                        fontSize: 11,
                        color: widget.isSelected ? colors.primary : colors.textSecondary,
                        fontWeight: FontWeight.w500,
                      ),
                    ),
                    onPressed: widget.onSelect,
                  ),
                  Row(
                    children: [
                      if (widget.onViewConsole != null) ...[
                        IconButton(
                          icon: Icon(Icons.terminal_rounded, size: 16, color: colors.textSecondary),
                          tooltip: 'Boot Console',
                          onPressed: widget.onViewConsole,
                        ),
                        const SizedBox(width: 4),
                      ],
                      if (!vm.isRunning)
                        ElevatedButton.icon(
                          style: ElevatedButton.styleFrom(
                            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                            backgroundColor: colors.success,
                            foregroundColor: Colors.white,
                          ),
                          icon: const Icon(Icons.play_arrow_rounded, size: 14),
                          label: const Text('Start VM', style: TextStyle(fontSize: 11.5, fontWeight: FontWeight.w600)),
                          onPressed: widget.onStart,
                        )
                      else
                        OutlinedButton.icon(
                          style: OutlinedButton.styleFrom(
                            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                            foregroundColor: colors.warning,
                            side: BorderSide(color: colors.warning.withValues(alpha: 0.5)),
                          ),
                          icon: const Icon(Icons.stop_rounded, size: 14),
                          label: const Text('Stop VM', style: TextStyle(fontSize: 11.5, fontWeight: FontWeight.w600)),
                          onPressed: widget.onStop,
                        ),
                    ],
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _specItem(BuildContext context, String label, String value, {Color? valueColor}) {
    final colors = VosColors.of(context);
    return Column(
      children: [
        Text(label, style: TextStyle(fontSize: 9.5, color: colors.textMuted)),
        const SizedBox(height: 2),
        Text(
          value,
          style: TextStyle(
            fontSize: 11,
            fontWeight: FontWeight.w600,
            color: valueColor ?? colors.textPrimary,
            fontFamily: VeyloraTypography.fontMono,
          ),
        ),
      ],
    );
  }

  Widget _specDivider(BuildContext context) {
    final colors = VosColors.of(context);
    return Container(
      width: 1,
      height: 18,
      color: colors.borderSubtle,
    );
  }
}
