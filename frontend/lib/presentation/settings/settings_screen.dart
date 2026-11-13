import 'package:flutter/material.dart';

import '../../core/theme.dart';
import '../../core/constants.dart';
import '../../application/vos_service.dart';
import '../../widgets/status_badge.dart';
import '../../widgets/section_header.dart';

class SettingsScreen extends StatelessWidget {
  final VosService service;

  const SettingsScreen({super.key, required this.service});

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);
    final host = service.hostTelemetry;

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
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Settings & System Diagnostics',
                      style: TextStyle(
                        fontSize: 20,
                        fontWeight: FontWeight.w700,
                        color: colors.textPrimary,
                        letterSpacing: -0.4,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      'Configure visual preferences, inspect C11 engine bindings, and review virtualization infrastructure',
                      style: TextStyle(fontSize: 12, color: colors.textSecondary),
                    ),
                  ],
                ),
                StatusBadge(
                  label: service.isInitialized ? 'ENGINE CONNECTED' : 'DISCONNECTED',
                  color: service.isInitialized ? colors.success : colors.danger,
                ),
              ],
            ),
            const SizedBox(height: VeyloraSpacing.xl),

            // Category 1: Appearance & Theming
            SectionHeader(
              title: 'Appearance & Theme',
              subtitle: 'Select interface color scheme and visual appearance',
              icon: Icons.palette_outlined,
            ),
            const SizedBox(height: VeyloraSpacing.xs),
            Container(
              padding: const EdgeInsets.all(VeyloraSpacing.lg),
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: BorderRadius.circular(VeyloraRadius.lg),
                border: Border.all(color: colors.border),
              ),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Interface Theme',
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w600,
                          color: colors.textPrimary,
                        ),
                      ),
                      const SizedBox(height: 2),
                      Text(
                        'Toggle between deep charcoal dark mode and clean neutral light mode',
                        style: TextStyle(fontSize: 12, color: colors.textSecondary),
                      ),
                    ],
                  ),
                  SegmentedButton<ThemeMode>(
                    segments: const [
                      ButtonSegment(
                        value: ThemeMode.dark,
                        icon: Icon(Icons.dark_mode_outlined, size: 16),
                        label: Text('Dark'),
                      ),
                      ButtonSegment(
                        value: ThemeMode.light,
                        icon: Icon(Icons.light_mode_outlined, size: 16),
                        label: Text('Light'),
                      ),
                    ],
                    selected: {service.themeMode},
                    onSelectionChanged: (set) {
                      service.setThemeMode(set.first);
                    },
                  ),
                ],
              ),
            ),
            const SizedBox(height: VeyloraSpacing.xl),

            // Category 2: Backend & C-ABI Shared Library
            SectionHeader(
              title: 'C11 Backend & Shared Dynamic Library',
              subtitle: 'Native dynamic library integration via dart:ffi',
              icon: Icons.code_rounded,
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
                children: [
                  _infoRow(
                    context,
                    'C-ABI Gateway Library',
                    VosConstants.libraryName,
                    subtitle: 'Compiled with -fPIC -dynamiclib (Apple Silicon ARM64)',
                  ),
                  Divider(height: 20, color: colors.borderSubtle),
                  _infoRow(
                    context,
                    'Engine Status',
                    service.statusMessage,
                    subtitle: 'Thread-safe C gateway initialized with SQLite WAL connection',
                  ),
                  Divider(height: 20, color: colors.borderSubtle),
                  _infoRow(
                    context,
                    'Host Platform',
                    host.hostPlatform,
                    subtitle: 'Native macOS Darwin kernel bindings (Mach host_statistics64)',
                  ),
                ],
              ),
            ),
            const SizedBox(height: VeyloraSpacing.xl),

            // Category 3: Virtualization & Hypervisor
            SectionHeader(
              title: 'Virtualization & QEMU Infrastructure',
              subtitle: 'macOS Hypervisor Framework (HVF) and UEFI firmware',
              icon: Icons.dns_outlined,
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
                children: [
                  _infoRow(
                    context,
                    'QEMU Binary',
                    '/opt/homebrew/bin/qemu-system-aarch64',
                    subtitle: 'Target architecture: aarch64 with -accel hvf',
                  ),
                  Divider(height: 20, color: colors.borderSubtle),
                  _infoRow(
                    context,
                    'EDK2 UEFI Firmware',
                    '/opt/homebrew/share/qemu/edk2-aarch64-code.fd',
                    subtitle: 'AAVMF 64-bit UEFI platform code for ARM virtual machines',
                  ),
                  Divider(height: 20, color: colors.borderSubtle),
                  _infoRow(
                    context,
                    'Guest Communication',
                    'Virtio-Serial (/dev/virtio-ports/vos_agent)',
                    subtitle: 'Bidirectional JSON RPC framing over QEMU chardev socket',
                  ),
                ],
              ),
            ),
            const SizedBox(height: VeyloraSpacing.xl),

            // Category 4: Database & Persistence
            SectionHeader(
              title: 'Database Persistence & Storage',
              subtitle: 'SQLite storage with Write-Ahead Logging (WAL)',
              icon: Icons.storage_rounded,
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
                children: [
                  _infoRow(
                    context,
                    'Database File',
                    VosConstants.defaultDbPath,
                    subtitle: 'SQLite 3.43 persistent database in root directory',
                  ),
                  Divider(height: 20, color: colors.borderSubtle),
                  _infoRow(
                    context,
                    'Journal Mode',
                    'WAL (Write-Ahead Logging)',
                    subtitle: 'Allows concurrent read and write transactions without locking',
                  ),
                  Divider(height: 20, color: colors.borderSubtle),
                  _infoRow(
                    context,
                    'Table Entities',
                    'virtual_machines, application_registry, application_instances, resource_profiles, audit_events',
                    subtitle: 'Foreign key cascade enforcement enabled (PRAGMA foreign_keys = ON)',
                  ),
                ],
              ),
            ),
            const SizedBox(height: VeyloraSpacing.xl),

            // Category 5: About VEYLORA
            SectionHeader(
              title: 'About VEYLORA',
              subtitle: 'Brand identity & core technical specifications',
              icon: Icons.info_outline_rounded,
            ),
            const SizedBox(height: VeyloraSpacing.xs),
            Container(
              padding: const EdgeInsets.all(VeyloraSpacing.xl),
              decoration: BoxDecoration(
                color: colors.card,
                borderRadius: BorderRadius.circular(VeyloraRadius.lg),
                border: Border.all(color: colors.border),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      Container(
                        width: 52,
                        height: 52,
                        decoration: BoxDecoration(
                          borderRadius: BorderRadius.circular(VeyloraRadius.md),
                          border: Border.all(
                            color: colors.primary.withValues(alpha: 0.4),
                          ),
                          boxShadow: [
                            BoxShadow(
                              color: colors.primary.withValues(alpha: 0.2),
                              blurRadius: 10,
                            ),
                          ],
                        ),
                        clipBehavior: Clip.antiAlias,
                        child: Image.asset(
                          'assets/icons/logo.png',
                          fit: BoxFit.cover,
                          errorBuilder: (context, error, stackTrace) => Image.asset(
                            'assets/images/veylora_logo.jpg',
                            fit: BoxFit.cover,
                            errorBuilder: (context, error, stackTrace) => Container(
                              color: colors.primary.withValues(alpha: 0.15),
                              child: Icon(Icons.layers_rounded, color: colors.primary, size: 28),
                            ),
                          ),
                        ),
                      ),
                      const SizedBox(width: VeyloraSpacing.md),
                      Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Row(
                            children: [
                              Text(
                                VosConstants.appTitle.toUpperCase(),
                                style: TextStyle(
                                  fontSize: 18,
                                  fontWeight: FontWeight.w800,
                                  letterSpacing: 2.0,
                                  color: colors.textPrimary,
                                ),
                              ),
                              const SizedBox(width: VeyloraSpacing.sm),
                              Container(
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 7,
                                  vertical: 2,
                                ),
                                decoration: BoxDecoration(
                                  color: colors.primary.withValues(alpha: 0.15),
                                  borderRadius: BorderRadius.circular(VeyloraRadius.xs),
                                ),
                                child: Text(
                                  VosConstants.appVersion,
                                  style: TextStyle(
                                    fontSize: 10.5,
                                    fontFamily: VeyloraTypography.fontMono,
                                    fontWeight: FontWeight.w700,
                                    color: colors.primary,
                                  ),
                                ),
                              ),
                            ],
                          ),
                          const SizedBox(height: 2),
                          Text(
                            VosConstants.appTagline,
                            style: TextStyle(
                              fontSize: 12,
                              fontWeight: FontWeight.w500,
                              color: colors.accent,
                            ),
                          ),
                          const SizedBox(height: 1),
                          Text(
                            VosConstants.appCategory,
                            style: TextStyle(fontSize: 11, color: colors.textMuted),
                          ),
                        ],
                      ),
                    ],
                  ),
                  const SizedBox(height: VeyloraSpacing.md),
                  Text(
                    'VEYLORA is a high-performance Virtual Computing Environment integrating a C11 operating system kernel, QEMU ARM64 virtualization with macOS Hypervisor Framework (HVF) acceleration, Linux cgroups v2 resource governance, real-time multi-layer telemetry, and an enterprise Flutter desktop console.',
                    style: TextStyle(
                      fontSize: 12,
                      color: colors.textSecondary,
                      height: 1.5,
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

  Widget _infoRow(
    BuildContext context,
    String title,
    String value, {
    String? subtitle,
  }) {
    final colors = VosColors.of(context);
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Expanded(
          flex: 2,
          child: Column(
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
              if (subtitle != null) ...[
                const SizedBox(height: 2),
                Text(
                  subtitle,
                  style: TextStyle(fontSize: 11, color: colors.textSecondary),
                ),
              ],
            ],
          ),
        ),
        const SizedBox(width: 16),
        Flexible(
          flex: 3,
          child: SelectableText(
            value,
            textAlign: TextAlign.right,
            style: TextStyle(
              fontSize: 12,
              fontFamily: VeyloraTypography.fontMono,
              fontWeight: FontWeight.w500,
              color: colors.textPrimary,
            ),
          ),
        ),
      ],
    );
  }
}
