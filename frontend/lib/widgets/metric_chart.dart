import 'dart:math' as math;
import 'package:flutter/material.dart';
import '../core/theme.dart';

class MetricChart extends StatelessWidget {
  final String title;
  final String currentValue;
  final String? subtitle;
  final List<double> data;
  final Color accentColor;
  final String unit;
  final double height;
  final double? fixedMaxY;

  const MetricChart({
    super.key,
    required this.title,
    required this.currentValue,
    this.subtitle,
    required this.data,
    this.accentColor = VosColors.primary,
    this.unit = '',
    this.height = 160,
    this.fixedMaxY,
  });

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);

    double maxVal = fixedMaxY ?? 100.0;
    if (fixedMaxY == null && data.isNotEmpty) {
      final dataMax = data.reduce(math.max);
      maxVal = (dataMax * 1.2).clamp(10.0, 100000.0);
    }

    final minVal = 0.0;
    final avgVal = data.isNotEmpty ? (data.reduce((a, b) => a + b) / data.length) : 0.0;
    final peakVal = data.isNotEmpty ? data.reduce(math.max) : 0.0;

    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: colors.card,
        borderRadius: VeyloraRadius.rLg,
        border: Border.all(color: colors.border),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withValues(alpha: colors.isDark ? 0.2 : 0.02),
            blurRadius: 8,
            offset: const Offset(0, 2),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Header: Title, Metric Values, and Live Badge
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        Flexible(
                          child: Text(
                            title,
                            style: TextStyle(
                              fontSize: 12.5,
                              fontWeight: FontWeight.w600,
                              color: colors.textSecondary,
                              letterSpacing: -0.1,
                            ),
                            overflow: TextOverflow.ellipsis,
                            maxLines: 1,
                          ),
                        ),
                        const SizedBox(width: 8),
                        Container(
                          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 1.5),
                          decoration: BoxDecoration(
                            color: colors.success.withValues(alpha: 0.12),
                            borderRadius: VeyloraRadius.rFull,
                            border: Border.all(color: colors.success.withValues(alpha: 0.25), width: 0.8),
                          ),
                          child: Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Container(
                                width: 5,
                                height: 5,
                                decoration: BoxDecoration(
                                  shape: BoxShape.circle,
                                  color: colors.success,
                                ),
                              ),
                              const SizedBox(width: 4),
                              Text(
                                'LIVE 1Hz',
                                style: TextStyle(
                                  fontSize: 9,
                                  fontWeight: FontWeight.w700,
                                  color: colors.success,
                                  fontFamily: VeyloraTypography.fontMono,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 4),
                    Row(
                      crossAxisAlignment: CrossAxisAlignment.baseline,
                      textBaseline: TextBaseline.alphabetic,
                      children: [
                        Text(
                          currentValue,
                          style: TextStyle(
                            fontSize: 22,
                            fontWeight: FontWeight.w700,
                            color: colors.textPrimary,
                            letterSpacing: -0.5,
                            fontFeatures: const [FontFeature.tabularFigures()],
                          ),
                        ),
                        if (subtitle != null) ...[
                          const SizedBox(width: 8),
                          Flexible(
                            child: Text(
                              subtitle!,
                              style: TextStyle(
                                fontSize: 11,
                                color: colors.textMuted,
                              ),
                              overflow: TextOverflow.ellipsis,
                              maxLines: 1,
                            ),
                          ),
                        ],
                      ],
                    ),
                  ],
                ),
              ),
              // Stats Pill: Avg & Peak
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 4.5),
                decoration: BoxDecoration(
                  color: accentColor.withValues(alpha: 0.08),
                  borderRadius: VeyloraRadius.rMd,
                  border: Border.all(color: accentColor.withValues(alpha: 0.22), width: 0.8),
                ),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text(
                      'Avg: ${avgVal.toStringAsFixed(1)}$unit',
                      style: TextStyle(
                        fontSize: 10.5,
                        fontWeight: FontWeight.w600,
                        color: accentColor,
                        fontFamily: VeyloraTypography.fontMono,
                      ),
                    ),
                    const SizedBox(width: 6),
                    Text(
                      '|',
                      style: TextStyle(fontSize: 10, color: accentColor.withValues(alpha: 0.4)),
                    ),
                    const SizedBox(width: 6),
                    Text(
                      'Peak: ${peakVal.toStringAsFixed(1)}$unit',
                      style: TextStyle(
                        fontSize: 10.5,
                        fontWeight: FontWeight.w600,
                        color: colors.textSecondary,
                        fontFamily: VeyloraTypography.fontMono,
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 14),

          // Custom Paint Canvas Area with Y-axis markers
          SizedBox(
            height: height,
            width: double.infinity,
            child: data.isEmpty
                ? Center(
                    child: Text(
                      'Waiting for telemetry samples...',
                      style: TextStyle(fontSize: 12, color: colors.textMuted),
                    ),
                  )
                : CustomPaint(
                    painter: _TimeSeriesChartPainter(
                      data: data,
                      accentColor: accentColor,
                      borderColor: colors.borderSubtle,
                      gridColor: colors.borderSubtle.withValues(alpha: 0.4),
                      textColor: colors.textMuted,
                      minY: minVal,
                      maxY: maxVal,
                      unit: unit,
                    ),
                  ),
          ),
        ],
      ),
    );
  }
}

class _TimeSeriesChartPainter extends CustomPainter {
  final List<double> data;
  final Color accentColor;
  final Color borderColor;
  final Color gridColor;
  final Color textColor;
  final double minY;
  final double maxY;
  final String unit;

  _TimeSeriesChartPainter({
    required this.data,
    required this.accentColor,
    required this.borderColor,
    required this.gridColor,
    required this.textColor,
    required this.minY,
    required this.maxY,
    required this.unit,
  });

  @override
  void paint(Canvas canvas, Size size) {
    if (data.isEmpty) return;

    final gridPaint = Paint()
      ..color = gridColor
      ..strokeWidth = 1.0
      ..style = PaintingStyle.stroke;

    // Draw horizontal dashed grid lines (0%, 50%, 100%)
    for (int i = 0; i <= 2; i++) {
      final y = size.height * (i / 2.0);
      canvas.drawLine(Offset(0, y), Offset(size.width, y), gridPaint);
    }

    final range = (maxY - minY).abs() > 0 ? (maxY - minY) : 1.0;
    final dx = size.width / (math.max(data.length - 1, 1));

    final path = Path();
    final fillPath = Path();

    for (int i = 0; i < data.length; i++) {
      final normY = ((data[i] - minY) / range).clamp(0.0, 1.0);
      final x = i * dx;
      final y = size.height - (normY * size.height);

      if (i == 0) {
        path.moveTo(x, y);
        fillPath.moveTo(x, size.height);
        fillPath.lineTo(x, y);
      } else {
        final prevNormY = ((data[i - 1] - minY) / range).clamp(0.0, 1.0);
        final prevX = (i - 1) * dx;
        final prevY = size.height - (prevNormY * size.height);

        // Smooth cubic Bezier spline
        final cx1 = prevX + (x - prevX) / 2.0;
        final cy1 = prevY;
        final cx2 = prevX + (x - prevX) / 2.0;
        final cy2 = y;

        path.cubicTo(cx1, cy1, cx2, cy2, x, y);
        fillPath.cubicTo(cx1, cy1, cx2, cy2, x, y);
      }
    }

    fillPath.lineTo((data.length - 1) * dx, size.height);
    fillPath.close();

    // Gradient Fill
    final gradient = LinearGradient(
      begin: Alignment.topCenter,
      end: Alignment.bottomCenter,
      colors: [
        accentColor.withValues(alpha: 0.22),
        accentColor.withValues(alpha: 0.0),
      ],
    );

    final fillPaint = Paint()
      ..shader = gradient.createShader(Rect.fromLTWH(0, 0, size.width, size.height))
      ..style = PaintingStyle.fill;

    canvas.drawPath(fillPath, fillPaint);

    // Line Stroke
    final linePaint = Paint()
      ..color = accentColor
      ..strokeWidth = 2.0
      ..strokeCap = StrokeCap.round
      ..style = PaintingStyle.stroke;

    canvas.drawPath(path, linePaint);

    // Latest Point Indicator Glow Dot
    if (data.isNotEmpty) {
      final lastIdx = data.length - 1;
      final lastX = lastIdx * dx;
      final lastNormY = ((data[lastIdx] - minY) / range).clamp(0.0, 1.0);
      final lastY = size.height - (lastNormY * size.height);

      final dotBgPaint = Paint()
        ..color = accentColor.withValues(alpha: 0.25)
        ..style = PaintingStyle.fill;
      canvas.drawCircle(Offset(lastX, lastY), 6.5, dotBgPaint);

      final dotPaint = Paint()
        ..color = accentColor
        ..style = PaintingStyle.fill;
      canvas.drawCircle(Offset(lastX, lastY), 3.2, dotPaint);
    }
  }

  @override
  bool shouldRepaint(covariant _TimeSeriesChartPainter oldDelegate) {
    return oldDelegate.data != data || oldDelegate.accentColor != accentColor;
  }
}
