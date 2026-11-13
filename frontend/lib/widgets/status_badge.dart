import 'package:flutter/material.dart';
import '../core/theme.dart';

class StatusBadge extends StatelessWidget {
  final String label;
  final Color? color;
  final bool isSmall;
  final bool showDot;

  const StatusBadge({
    super.key,
    required this.label,
    this.color,
    this.isSmall = false,
    this.showDot = true,
  });

  Color _resolveColor(BuildContext context, String text) {
    if (color != null) return color!;
    final colors = VosColors.of(context);
    final upper = text.toUpperCase();
    if (upper == 'RUNNING' ||
        upper == 'LIVE' ||
        upper == 'VERIFIED' ||
        upper == 'FRESH' ||
        upper == 'SAFE' ||
        upper == 'CONNECTED' ||
        upper == 'SYSTEM ONLINE' ||
        upper == 'AGENT READY' ||
        upper == 'HVF ACCELERATED') {
      return colors.success;
    } else if (upper == 'STARTING' ||
        upper == 'WARNING' ||
        upper == 'STALE' ||
        upper == 'STANDBY' ||
        upper == 'DISCONNECTED') {
      return colors.warning;
    } else if (upper == 'FAILED' ||
        upper == 'DANGER' ||
        upper == 'UNSAFE' ||
        upper == 'CONTRADICTION' ||
        upper == 'OOM') {
      return colors.danger;
    } else if (upper == 'UNSUPPORTED' ||
        upper == 'UNAVAILABLE' ||
        upper == 'STOPPED' ||
        upper == 'CREATED') {
      return colors.textMuted;
    }
    return colors.info;
  }

  @override
  Widget build(BuildContext context) {
    final c = _resolveColor(context, label);
    return Container(
      padding: EdgeInsets.symmetric(
        horizontal: isSmall ? 7 : 9,
        vertical: isSmall ? 2.5 : 3.5,
      ),
      decoration: BoxDecoration(
        color: c.withValues(alpha: 0.12),
        borderRadius: VeyloraRadius.rFull,
        border: Border.all(color: c.withValues(alpha: 0.28), width: 0.8),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (showDot) ...[
            Container(
              width: isSmall ? 5 : 6,
              height: isSmall ? 5 : 6,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: c,
                boxShadow: [
                  BoxShadow(
                    color: c.withValues(alpha: 0.4),
                    blurRadius: 3,
                  ),
                ],
              ),
            ),
            const SizedBox(width: 5.5),
          ],
          Flexible(
            child: Text(
              label.toUpperCase(),
              style: TextStyle(
                fontSize: isSmall ? 9.5 : 10.5,
                fontWeight: FontWeight.w700,
                color: c,
                letterSpacing: 0.5,
                fontFamily: VeyloraTypography.fontMono,
              ),
              overflow: TextOverflow.ellipsis,
              maxLines: 1,
            ),
          ),
        ],
      ),
    );
  }
}
