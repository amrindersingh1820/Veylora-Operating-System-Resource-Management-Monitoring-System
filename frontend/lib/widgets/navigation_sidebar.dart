import 'package:flutter/material.dart';
import '../core/theme.dart';
import '../core/constants.dart';
import '../application/vos_service.dart';

class NavigationSidebar extends StatefulWidget {
  final int selectedIndex;
  final ValueChanged<int> onDestinationSelected;
  final VosService service;

  const NavigationSidebar({
    super.key,
    required this.selectedIndex,
    required this.onDestinationSelected,
    required this.service,
  });

  @override
  State<NavigationSidebar> createState() => _NavigationSidebarState();
}

class _NavigationSidebarState extends State<NavigationSidebar> {
  bool _isCollapsed = false;

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);
    final width = _isCollapsed ? 72.0 : 244.0;
    final runningVmCount = widget.service.vms.where((v) => v.isRunning).length;
    final runningAppCount = widget.service.appInstances.where((a) => a.isRunning).length;

    return AnimatedContainer(
      duration: VeyloraMotion.normal,
      curve: VeyloraMotion.ease,
      width: width,
      decoration: BoxDecoration(
        color: colors.surface,
        border: Border(right: BorderSide(color: colors.border, width: 1)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // ==================================================================
          // Top Brand Header
          // ==================================================================
          Container(
            padding: EdgeInsets.symmetric(
              horizontal: _isCollapsed ? 16 : 18,
              vertical: 18,
            ),
            child: Row(
              children: [
                // Veylora Metallic Logo with subtle glow
                Container(
                  width: 36,
                  height: 36,
                  decoration: BoxDecoration(
                    borderRadius: VeyloraRadius.rMd,
                    border: Border.all(
                      color: colors.primary.withValues(alpha: 0.35),
                      width: 1.2,
                    ),
                    boxShadow: [
                      BoxShadow(
                        color: colors.primary.withValues(alpha: 0.2),
                        blurRadius: 10,
                        spreadRadius: 0,
                      ),
                    ],
                  ),
                  child: ClipRRect(
                    borderRadius: BorderRadius.circular(7),
                    child: Image.asset(
                      'assets/icons/logo.png',
                      fit: BoxFit.cover,
                      errorBuilder: (context, error, stackTrace) => Image.asset(
                        'assets/images/veylora_logo.jpg',
                        fit: BoxFit.cover,
                        errorBuilder: (context, error, stackTrace) => Container(
                          color: colors.primary.withValues(alpha: 0.15),
                          child: Icon(Icons.memory_rounded, color: colors.primary, size: 20),
                        ),
                      ),
                    ),
                  ),
                ),
                if (!_isCollapsed) ...[
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Text(
                              VosConstants.appTitle.toUpperCase(),
                              style: TextStyle(
                                color: colors.textPrimary,
                                fontWeight: FontWeight.w800,
                                fontSize: 13,
                                letterSpacing: 1.2,
                              ),
                              overflow: TextOverflow.ellipsis,
                            ),
                            const SizedBox(width: 6),
                            Container(
                              padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 1.5),
                              decoration: BoxDecoration(
                                color: colors.primary.withValues(alpha: 0.15),
                                borderRadius: VeyloraRadius.rXs,
                                border: Border.all(color: colors.primary.withValues(alpha: 0.3), width: 0.8),
                              ),
                              child: Text(
                                'PRO',
                                style: TextStyle(
                                  color: colors.primary,
                                  fontSize: 8.5,
                                  fontWeight: FontWeight.w700,
                                  letterSpacing: 0.6,
                                ),
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 2),
                        Row(
                          children: [
                            Container(
                              width: 6,
                              height: 6,
                              decoration: BoxDecoration(
                                shape: BoxShape.circle,
                                color: widget.service.isInitialized ? colors.success : colors.warning,
                                boxShadow: [
                                  BoxShadow(
                                    color: (widget.service.isInitialized ? colors.success : colors.warning)
                                        .withValues(alpha: 0.5),
                                    blurRadius: 4,
                                  ),
                                ],
                              ),
                            ),
                            const SizedBox(width: 6),
                            Expanded(
                              child: Text(
                                widget.service.isInitialized ? 'Engine Online' : 'Connecting...',
                                style: TextStyle(
                                  color: colors.textMuted,
                                  fontSize: 10.5,
                                  fontWeight: FontWeight.w500,
                                ),
                                overflow: TextOverflow.ellipsis,
                              ),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ],
              ],
            ),
          ),

          const Divider(height: 1),

          // ==================================================================
          // Categorized Sectioned Navigation Items
          // ==================================================================
          Expanded(
            child: ListView(
              padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 10),
              children: [
                _buildSectionHeader('OVERVIEW'),
                _buildNavItem(0, Icons.space_dashboard_outlined, Icons.space_dashboard_rounded, 'Dashboard'),

                const SizedBox(height: 12),
                _buildSectionHeader('MANAGEMENT'),
                _buildNavItem(
                  1,
                  Icons.dns_outlined,
                  Icons.dns_rounded,
                  'Virtual Machines',
                  badgeText: runningVmCount > 0 ? '$runningVmCount' : null,
                  badgeColor: colors.success,
                ),
                _buildNavItem(
                  2,
                  Icons.layers_outlined,
                  Icons.layers_rounded,
                  'Applications',
                  badgeText: runningAppCount > 0 ? '$runningAppCount' : null,
                  badgeColor: colors.primary,
                ),
                _buildNavItem(3, Icons.tune_outlined, Icons.tune_rounded, 'Resource Management'),

                const SizedBox(height: 12),
                _buildSectionHeader('OBSERVABILITY'),
                _buildNavItem(4, Icons.show_chart_rounded, Icons.ssid_chart_rounded, 'Monitoring'),
                _buildNavItem(6, Icons.data_object_rounded, Icons.receipt_long_rounded, 'Event Logs'),

                const SizedBox(height: 12),
                _buildSectionHeader('TOOLS'),
                _buildNavItem(5, Icons.memory_outlined, Icons.memory_rounded, 'OS Algorithms'),

                const SizedBox(height: 12),
                _buildSectionHeader('PREFERENCES'),
                _buildNavItem(7, Icons.settings_outlined, Icons.settings_rounded, 'Settings'),
              ],
            ),
          ),

          const Divider(height: 1),

          // ==================================================================
          // Sidebar Footer & Collapse Toggle
          // ==================================================================
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
            child: Row(
              mainAxisAlignment: _isCollapsed ? MainAxisAlignment.center : MainAxisAlignment.spaceBetween,
              children: [
                if (!_isCollapsed) ...[
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'Veylora Hypervisor',
                          style: TextStyle(
                            color: colors.textSecondary,
                            fontSize: 11,
                            fontWeight: FontWeight.w600,
                          ),
                          overflow: TextOverflow.ellipsis,
                        ),
                        Text(
                          '${VosConstants.appVersion} · Mach HVF',
                          style: TextStyle(
                            color: colors.textMuted,
                            fontSize: 10,
                            fontFamily: VeyloraTypography.fontMono,
                          ),
                          overflow: TextOverflow.ellipsis,
                        ),
                      ],
                    ),
                  ),
                ],
                IconButton(
                  icon: Icon(
                    _isCollapsed ? Icons.chevron_right_rounded : Icons.chevron_left_rounded,
                    size: 18,
                    color: colors.textSecondary,
                  ),
                  tooltip: _isCollapsed ? 'Expand sidebar' : 'Collapse sidebar',
                  onPressed: () {
                    setState(() => _isCollapsed = !_isCollapsed);
                  },
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSectionHeader(String title) {
    if (_isCollapsed) {
      return const SizedBox(height: 6);
    }
    final colors = VosColors.of(context);
    return Padding(
      padding: const EdgeInsets.fromLTRB(8, 6, 8, 4),
      child: Text(
        title,
        style: TextStyle(
          color: colors.textMuted.withValues(alpha: 0.8),
          fontSize: 9.5,
          fontWeight: FontWeight.w700,
          letterSpacing: 0.9,
        ),
      ),
    );
  }

  Widget _buildNavItem(
    int index,
    IconData icon,
    IconData activeIcon,
    String label, {
    String? badgeText,
    Color? badgeColor,
  }) {
    final colors = VosColors.of(context);
    final isSelected = widget.selectedIndex == index;

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 1.5),
      child: Tooltip(
        message: _isCollapsed ? label : '',
        waitDuration: const Duration(milliseconds: 300),
        child: InkWell(
          borderRadius: VeyloraRadius.rMd,
          onTap: () => widget.onDestinationSelected(index),
          child: AnimatedContainer(
            duration: VeyloraMotion.fast,
            padding: EdgeInsets.symmetric(
              horizontal: _isCollapsed ? 0 : 10,
              vertical: 8.5,
            ),
            decoration: BoxDecoration(
              color: isSelected ? colors.primary.withValues(alpha: 0.12) : Colors.transparent,
              borderRadius: VeyloraRadius.rMd,
              border: Border.all(
                color: isSelected ? colors.primary.withValues(alpha: 0.28) : Colors.transparent,
                width: 1,
              ),
            ),
            child: Row(
              mainAxisAlignment: _isCollapsed ? MainAxisAlignment.center : MainAxisAlignment.start,
              children: [
                // Left active accent bar
                if (!_isCollapsed && isSelected)
                  Container(
                    width: 3,
                    height: 14,
                    margin: const EdgeInsets.only(right: 8),
                    decoration: BoxDecoration(
                      color: colors.primary,
                      borderRadius: VeyloraRadius.rFull,
                      boxShadow: [
                        BoxShadow(
                          color: colors.primary.withValues(alpha: 0.6),
                          blurRadius: 4,
                        ),
                      ],
                    ),
                  ),
                Icon(
                  isSelected ? activeIcon : icon,
                  size: 17,
                  color: isSelected ? colors.primary : colors.textSecondary,
                ),
                if (!_isCollapsed) ...[
                  const SizedBox(width: 9),
                  Expanded(
                    child: Text(
                      label,
                      style: TextStyle(
                        fontSize: 12.5,
                        fontWeight: isSelected ? FontWeight.w600 : FontWeight.w400,
                        color: isSelected ? colors.textPrimary : colors.textSecondary,
                        letterSpacing: -0.15,
                      ),
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                  if (badgeText != null)
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 1.5),
                      decoration: BoxDecoration(
                        color: (badgeColor ?? colors.primary).withValues(alpha: 0.15),
                        borderRadius: VeyloraRadius.rFull,
                        border: Border.all(
                          color: (badgeColor ?? colors.primary).withValues(alpha: 0.3),
                          width: 0.8,
                        ),
                      ),
                      child: Text(
                        badgeText,
                        style: TextStyle(
                          fontSize: 9.5,
                          fontWeight: FontWeight.w700,
                          color: badgeColor ?? colors.primary,
                          fontFamily: VeyloraTypography.fontMono,
                        ),
                      ),
                    ),
                ],
              ],
            ),
          ),
        ),
      ),
    );
  }
}
