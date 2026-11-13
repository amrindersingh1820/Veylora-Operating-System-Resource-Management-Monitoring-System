import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../core/theme.dart';
import '../application/vos_service.dart';
import 'status_badge.dart';

class AppTopBar extends StatelessWidget {
  final String title;
  final String? subtitle;
  final String? category;
  final VosService service;
  final List<Widget>? actions;
  final VoidCallback? onReplaySplash;

  const AppTopBar({
    super.key,
    required this.title,
    this.subtitle,
    this.category,
    required this.service,
    this.actions,
    this.onReplaySplash,
  });

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);
    final activeVm = service.selectedVm;

    return Container(
      height: 56,
      padding: const EdgeInsets.symmetric(horizontal: 16),
      decoration: BoxDecoration(
        color: colors.surface,
        border: Border(bottom: BorderSide(color: colors.border, width: 1)),
      ),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final isNarrow = constraints.maxWidth < 780;
          final isVeryNarrow = constraints.maxWidth < 620;

          return Row(
            children: [
              // ==================================================================
              // Breadcrumbs & Screen Title
              // ==================================================================
              Expanded(
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    if (!isVeryNarrow && category != null) ...[
                      Text(
                        category!,
                        style: TextStyle(
                          fontSize: 12,
                          fontWeight: FontWeight.w500,
                          color: colors.textMuted,
                          letterSpacing: -0.1,
                        ),
                      ),
                      Padding(
                        padding: const EdgeInsets.symmetric(horizontal: 6),
                        child: Icon(Icons.chevron_right_rounded, size: 14, color: colors.textMuted),
                      ),
                    ],
                    Flexible(
                      child: Text(
                        title,
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
                    if (!isNarrow && subtitle != null && subtitle!.isNotEmpty) ...[
                      const SizedBox(width: 8),
                      Text(
                        '·',
                        style: TextStyle(color: colors.textMuted.withValues(alpha: 0.6), fontSize: 13),
                      ),
                      const SizedBox(width: 8),
                      Flexible(
                        child: Text(
                          subtitle!,
                          style: TextStyle(
                            fontSize: 11.5,
                            color: colors.textSecondary,
                            letterSpacing: -0.1,
                          ),
                          overflow: TextOverflow.ellipsis,
                          maxLines: 1,
                        ),
                      ),
                    ],
                  ],
                ),
              ),
              const SizedBox(width: 10),

              // ==================================================================
              // Right Status Badges & Controls
              // ==================================================================
              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  // Backend Connectivity Status Pill
                  Tooltip(
                    message: service.isInitialized ? 'C11 Mach Core Engine Active' : 'Backend Engine Offline',
                    child: Container(
                      padding: EdgeInsets.symmetric(
                        horizontal: isVeryNarrow ? 6 : 9,
                        vertical: 4.5,
                      ),
                      decoration: BoxDecoration(
                        color: (service.isInitialized ? colors.success : colors.danger).withValues(alpha: 0.1),
                        borderRadius: VeyloraRadius.rFull,
                        border: Border.all(
                          color: (service.isInitialized ? colors.success : colors.danger).withValues(alpha: 0.25),
                          width: 0.8,
                        ),
                      ),
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Container(
                            width: 6,
                            height: 6,
                            decoration: BoxDecoration(
                              shape: BoxShape.circle,
                              color: service.isInitialized ? colors.success : colors.danger,
                            ),
                          ),
                          if (!isVeryNarrow) ...[
                            const SizedBox(width: 6),
                            Text(
                              isNarrow ? 'C11' : (service.isInitialized ? 'C11 Mach Core' : 'Offline'),
                              style: TextStyle(
                                fontSize: 11,
                                fontWeight: FontWeight.w600,
                                color: service.isInitialized ? colors.success : colors.danger,
                                fontFamily: VeyloraTypography.fontMono,
                              ),
                            ),
                          ],
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),

                  // Active VM Selector & Status
                  if (service.vms.isNotEmpty) ...[
                    Container(
                      height: 32,
                      padding: const EdgeInsets.symmetric(horizontal: 8),
                      decoration: BoxDecoration(
                        color: colors.card,
                        borderRadius: VeyloraRadius.rMd,
                        border: Border.all(color: colors.border),
                      ),
                      child: DropdownButtonHideUnderline(
                        child: DropdownButton<String>(
                          value: service.selectedVmId,
                          dropdownColor: colors.card,
                          isDense: true,
                          icon: Icon(Icons.keyboard_arrow_down_rounded, size: 16, color: colors.textSecondary),
                          items: service.vms.map((vm) {
                            return DropdownMenuItem<String>(
                              value: vm.vmId,
                              child: Row(
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  Container(
                                    width: 6,
                                    height: 6,
                                    decoration: BoxDecoration(
                                      shape: BoxShape.circle,
                                      color: vm.isRunning ? colors.success : colors.textMuted,
                                    ),
                                  ),
                                  const SizedBox(width: 6),
                                  Text(
                                    isNarrow
                                        ? (vm.name.length > 8 ? '${vm.name.substring(0, 8)}…' : vm.name)
                                        : (vm.name.isNotEmpty ? vm.name : vm.vmId),
                                    style: TextStyle(
                                      fontSize: 11.5,
                                      fontWeight: FontWeight.w500,
                                      color: colors.textPrimary,
                                    ),
                                  ),
                                ],
                              ),
                            );
                          }).toList(),
                          onChanged: (id) {
                            if (id != null) service.selectVm(id);
                          },
                        ),
                      ),
                    ),
                    const SizedBox(width: 8),
                  ],

                  // Guest Agent Connectivity Badge
                  if (activeVm != null && !isNarrow) ...[
                    StatusBadge(
                      label: activeVm.guestReady == 1 ? 'AGENT READY' : 'STANDBY',
                      color: activeVm.guestReady == 1 ? colors.success : colors.warning,
                      isSmall: true,
                    ),
                    const SizedBox(width: 8),
                  ],

                  // Custom Actions
                  ...?actions,

                  // Replay Boot Console
                  if (onReplaySplash != null && !isNarrow)
                    IconButton(
                      icon: const Icon(Icons.terminal_rounded, size: 16),
                      tooltip: 'Veylora Architecture Boot Console',
                      style: IconButton.styleFrom(
                        foregroundColor: colors.textSecondary,
                        padding: const EdgeInsets.all(6),
                        minimumSize: const Size(28, 28),
                      ),
                      onPressed: onReplaySplash,
                    ),

                  // Theme Toggle
                  IconButton(
                    icon: Icon(
                      colors.isDark ? Icons.light_mode_outlined : Icons.dark_mode_outlined,
                      size: 16,
                      color: colors.textSecondary,
                    ),
                    tooltip: colors.isDark ? 'Switch to Light Theme' : 'Switch to Dark Theme',
                    style: IconButton.styleFrom(
                      padding: const EdgeInsets.all(6),
                      minimumSize: const Size(28, 28),
                    ),
                    onPressed: () => service.toggleTheme(),
                  ),

                  // Full Resolution Window Toggle
                  IconButton(
                    icon: const Icon(Icons.fullscreen_rounded, size: 17),
                    tooltip: 'Full Resolution Window',
                    style: IconButton.styleFrom(
                      foregroundColor: colors.textSecondary,
                      padding: const EdgeInsets.all(6),
                      minimumSize: const Size(28, 28),
                    ),
                    onPressed: () {
                      try {
                        const MethodChannel('com.veylora.window').invokeMethod('fullResolution');
                      } catch (_) {}
                    },
                  ),

                  // Refresh Button
                  IconButton(
                    icon: Icon(Icons.refresh_rounded, size: 16, color: colors.textSecondary),
                    tooltip: 'Refresh Telemetry & Records',
                    style: IconButton.styleFrom(
                      padding: const EdgeInsets.all(6),
                      minimumSize: const Size(28, 28),
                    ),
                    onPressed: () => service.refreshAll(),
                  ),
                ],
              ),
            ],
          );
        },
      ),
    );
  }
}
