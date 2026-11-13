import 'package:flutter/material.dart';
import '../core/theme.dart';
import '../core/formatting.dart';
import '../data/models/profile_model.dart';
import 'status_badge.dart';

class ResourceProfileCard extends StatefulWidget {
  final ResourceProfile profile;
  final VoidCallback onEdit;
  final VoidCallback? onDelete;

  const ResourceProfileCard({
    super.key,
    required this.profile,
    required this.onEdit,
    this.onDelete,
  });

  @override
  State<ResourceProfileCard> createState() => _ResourceProfileCardState();
}

class _ResourceProfileCardState extends State<ResourceProfileCard> {
  bool _isHovered = false;

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);
    final p = widget.profile;

    return MouseRegion(
      onEnter: (_) => setState(() => _isHovered = true),
      onExit: (_) => setState(() => _isHovered = false),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 150),
        padding: const EdgeInsets.all(14),
        decoration: BoxDecoration(
          color: _isHovered ? colors.cardHover : colors.card,
          borderRadius: BorderRadius.circular(10),
          border: Border.all(
            color: _isHovered ? colors.primary.withValues(alpha: 0.4) : colors.border,
            width: 1,
          ),
          boxShadow: _isHovered
              ? [
                  BoxShadow(
                    color: Colors.black.withValues(alpha: colors.isDark ? 0.2 : 0.04),
                    blurRadius: 8,
                    offset: const Offset(0, 3),
                  )
                ]
              : null,
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            // Top: Name, ID, Badges
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Expanded(
                  child: Row(
                    children: [
                      Container(
                        padding: const EdgeInsets.all(6),
                        decoration: BoxDecoration(
                          color: colors.primary.withValues(alpha: 0.12),
                          borderRadius: BorderRadius.circular(6),
                        ),
                        child: Icon(Icons.tune_rounded, size: 15, color: colors.primary),
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              p.name,
                              style: TextStyle(
                                fontSize: 13.5,
                                fontWeight: FontWeight.w600,
                                color: colors.textPrimary,
                                letterSpacing: -0.2,
                              ),
                              overflow: TextOverflow.ellipsis,
                              maxLines: 1,
                            ),
                            Text(
                              p.profileId,
                              style: TextStyle(
                                fontSize: 10.5,
                                fontFamily: 'monospace',
                                color: colors.textMuted,
                              ),
                              overflow: TextOverflow.ellipsis,
                              maxLines: 1,
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
                const SizedBox(width: 6),
                if (p.isDefault)
                  const StatusBadge(label: 'SYSTEM DEFAULT', isSmall: true)
                else
                  const StatusBadge(label: 'CUSTOM', isSmall: true),
              ],
            ),
            const SizedBox(height: 6),

            // Description
            if (p.description.isNotEmpty)
              Text(
                p.description,
                style: TextStyle(fontSize: 11.5, color: colors.textSecondary),
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
            const SizedBox(height: 8),

            // Quotas Grid
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
              decoration: BoxDecoration(
                color: colors.bg,
                borderRadius: BorderRadius.circular(6),
                border: Border.all(color: colors.borderSubtle),
              ),
              child: Column(
                children: [
                  _quotaRow(context, 'CPU Quota', '${p.cpuCores.toStringAsFixed(2)} cores (Weight: ${p.cpuWeight})'),
                  const SizedBox(height: 4),
                  _quotaRow(context, 'Memory Max', p.memoryMaxBytes > 0 ? VosFormat.formatBytes(p.memoryMaxBytes) : 'Unlimited'),
                  const SizedBox(height: 4),
                  _quotaRow(context, 'Task Limit', p.pidsMax > 0 ? '${p.pidsMax} PIDs' : 'Unlimited'),
                  const SizedBox(height: 4),
                  _quotaRow(
                    context,
                    'Disk I/O',
                    p.ioReadBps > 0 ? '${VosFormat.formatBytes(p.ioReadBps)}/s R/W' : 'Unrestricted',
                  ),
                  const SizedBox(height: 4),
                  _quotaRow(context, 'GPU Accelerator', 'UNSUPPORTED', isNotice: true),
                ],
              ),
            ),
            const SizedBox(height: 8),

            // Actions
            Row(
              mainAxisAlignment: MainAxisAlignment.end,
              children: [
                OutlinedButton.icon(
                  style: OutlinedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                  ),
                  icon: const Icon(Icons.edit_outlined, size: 13),
                  label: const Text('Edit', style: TextStyle(fontSize: 11.5)),
                  onPressed: widget.onEdit,
                ),
                if (!p.isDefault && widget.onDelete != null) ...[
                  const SizedBox(width: 8),
                  OutlinedButton.icon(
                    style: OutlinedButton.styleFrom(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                      foregroundColor: colors.danger,
                      side: BorderSide(color: colors.danger.withValues(alpha: 0.4)),
                    ),
                    icon: const Icon(Icons.delete_outline, size: 13),
                    label: const Text('Delete', style: TextStyle(fontSize: 11.5)),
                    onPressed: widget.onDelete,
                  ),
                ],
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _quotaRow(BuildContext context, String label, String value, {bool isNotice = false}) {
    final colors = VosColors.of(context);
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(label, style: TextStyle(fontSize: 11, color: colors.textMuted)),
        Text(
          value,
          style: TextStyle(
            fontSize: 11,
            fontWeight: FontWeight.w600,
            color: isNotice ? colors.textMuted : colors.textPrimary,
          ),
        ),
      ],
    );
  }
}

