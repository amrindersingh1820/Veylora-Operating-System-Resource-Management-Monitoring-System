import 'package:flutter/material.dart';
import '../core/theme.dart';

class MetricCard extends StatefulWidget {
  final String title;
  final String value;
  final String? subtitle;
  final IconData icon;
  final Color accentColor;
  final List<double>? sparklineData;
  final String? trendLabel;
  final bool? isPositiveTrend;

  const MetricCard({
    super.key,
    required this.title,
    required this.value,
    this.subtitle,
    required this.icon,
    this.accentColor = VosColors.primary,
    this.sparklineData,
    this.trendLabel,
    this.isPositiveTrend,
  });

  @override
  State<MetricCard> createState() => _MetricCardState();
}

class _MetricCardState extends State<MetricCard> {
  bool _isHovered = false;

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);

    return MouseRegion(
      onEnter: (_) => setState(() => _isHovered = true),
      onExit: (_) => setState(() => _isHovered = false),
      child: AnimatedContainer(
        duration: VeyloraMotion.fast,
        padding: const EdgeInsets.all(16),
        decoration: BoxDecoration(
          color: _isHovered ? colors.cardHover : colors.card,
          borderRadius: VeyloraRadius.rLg,
          border: Border.all(
            color: _isHovered ? widget.accentColor.withValues(alpha: 0.38) : colors.border,
            width: 1,
          ),
          boxShadow: [
            BoxShadow(
              color: Colors.black.withValues(alpha: colors.isDark ? (_isHovered ? 0.35 : 0.15) : 0.03),
              blurRadius: _isHovered ? 12 : 6,
              offset: Offset(0, _isHovered ? 4 : 2),
            ),
          ],
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            // Top Row: Subtle Icon Badge + Title + Optional Trend Pill
            Row(
              children: [
                Container(
                  padding: const EdgeInsets.all(6.5),
                  decoration: BoxDecoration(
                    color: widget.accentColor.withValues(alpha: 0.12),
                    borderRadius: VeyloraRadius.rMd,
                    border: Border.all(
                      color: widget.accentColor.withValues(alpha: 0.22),
                      width: 0.8,
                    ),
                  ),
                  child: Icon(widget.icon, color: widget.accentColor, size: 15),
                ),
                const SizedBox(width: 9),
                Expanded(
                  child: Text(
                    widget.title,
                    style: TextStyle(
                      color: colors.textSecondary,
                      fontSize: 12,
                      fontWeight: FontWeight.w500,
                      letterSpacing: -0.1,
                    ),
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                if (widget.trendLabel != null)
                  Container(
                    padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                    decoration: BoxDecoration(
                      color: (widget.isPositiveTrend == true ? colors.success : colors.danger).withValues(alpha: 0.12),
                      borderRadius: VeyloraRadius.rFull,
                      border: Border.all(
                        color: (widget.isPositiveTrend == true ? colors.success : colors.danger).withValues(alpha: 0.25),
                        width: 0.8,
                      ),
                    ),
                    child: Text(
                      widget.trendLabel!,
                      style: TextStyle(
                        fontSize: 9.5,
                        fontWeight: FontWeight.w700,
                        color: widget.isPositiveTrend == true ? colors.success : colors.danger,
                        fontFamily: VeyloraTypography.fontMono,
                      ),
                    ),
                  ),
              ],
            ),

            const SizedBox(height: 10),

            // Middle & Bottom Row: Large Numerical Display + Subtitle + Sparkline
            Row(
              crossAxisAlignment: CrossAxisAlignment.end,
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        widget.value,
                        style: TextStyle(
                          color: colors.textPrimary,
                          fontSize: 22,
                          fontWeight: FontWeight.w700,
                          letterSpacing: -0.6,
                          fontFeatures: const [FontFeature.tabularFigures()],
                        ),
                      ),
                      if (widget.subtitle != null) ...[
                        const SizedBox(height: 3),
                        Text(
                          widget.subtitle!,
                          style: TextStyle(
                            color: colors.textMuted,
                            fontSize: 11,
                            letterSpacing: -0.1,
                          ),
                          overflow: TextOverflow.ellipsis,
                        ),
                      ],
                    ],
                  ),
                ),

                // Mini Sparkline with subtle area fill and pulse dot
                if (widget.sparklineData != null && widget.sparklineData!.length > 1) ...[
                  SizedBox(
                    width: 76,
                    height: 32,
                    child: CustomPaint(
                      painter: _MiniSparklinePainter(
                        widget.sparklineData!,
                        widget.accentColor,
                      ),
                    ),
                  ),
                ],
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _MiniSparklinePainter extends CustomPainter {
  final List<double> data;
  final Color color;

  _MiniSparklinePainter(this.data, this.color);

  @override
  void paint(Canvas canvas, Size size) {
    if (data.length < 2) return;

    double minV = data[0];
    double maxV = data[0];
    for (final v in data) {
      if (v < minV) minV = v;
      if (v > maxV) maxV = v;
    }
    final range = (maxV - minV).abs() > 0 ? (maxV - minV) : 1.0;
    final dx = size.width / (data.length - 1);

    final linePath = Path();
    final fillPath = Path();

    for (int i = 0; i < data.length; i++) {
      final x = i * dx;
      final y = (size.height - 4) - (((data[i] - minV) / range) * (size.height - 8));
      if (i == 0) {
        linePath.moveTo(x, y);
        fillPath.moveTo(x, size.height);
        fillPath.lineTo(x, y);
      } else {
        linePath.lineTo(x, y);
        fillPath.lineTo(x, y);
      }
    }
    fillPath.lineTo(size.width, size.height);
    fillPath.close();

    // Subtle area fill under sparkline
    final fillPaint = Paint()
      ..shader = LinearGradient(
        begin: Alignment.topCenter,
        end: Alignment.bottomCenter,
        colors: [
          color.withValues(alpha: 0.25),
          color.withValues(alpha: 0.0),
        ],
      ).createShader(Rect.fromLTWH(0, 0, size.width, size.height));

    canvas.drawPath(fillPath, fillPaint);

    // Line stroke
    final stroke = Paint()
      ..color = color
      ..strokeWidth = 1.6
      ..strokeCap = StrokeCap.round
      ..style = PaintingStyle.stroke;

    canvas.drawPath(linePath, stroke);

    // Endpoint pulse dot
    final lastX = size.width;
    final lastY = (size.height - 4) - (((data.last - minV) / range) * (size.height - 8));
    final dotPaint = Paint()..color = color;
    canvas.drawCircle(Offset(lastX, lastY), 2.2, dotPaint);
  }

  @override
  bool shouldRepaint(covariant _MiniSparklinePainter oldDelegate) => true;
}
