import 'package:flutter/material.dart';

import '../core/theme.dart';
import '../data/models/simulation_models.dart';

class GanttChartWidget extends StatelessWidget {
  final List<GanttSegment> segments;
  final int totalTime;

  const GanttChartWidget({
    super.key,
    required this.segments,
    required this.totalTime,
  });

  static final List<Color> _palette = [
    const Color(0xFF38BDF8), // Sky Blue
    const Color(0xFF34D399), // Emerald
    const Color(0xFFFBBF24), // Amber
    const Color(0xFFA78BFA), // Violet
    const Color(0xFFF472B6), // Pink
    const Color(0xFF60A5FA), // Blue
    const Color(0xFF4ADE80), // Green
    const Color(0xFFFB923C), // Orange
  ];

  Color _colorForPid(int pid) {
    if (pid < 0) return const Color(0xFF64748B); // Slate idle
    return _palette[pid % _palette.length];
  }

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);

    if (segments.isEmpty || totalTime <= 0) {
      return Container(
        height: 100,
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: colors.card,
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: colors.border),
        ),
        child: Text('No execution timeline to render', style: TextStyle(color: colors.textMuted, fontSize: 13)),
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // Legend
        Wrap(
          spacing: 12,
          runSpacing: 8,
          children: _buildLegend(context),
        ),
        const SizedBox(height: 16),

        // Scrollable Chart Container
        SingleChildScrollView(
          scrollDirection: Axis.horizontal,
          child: Container(
            padding: const EdgeInsets.symmetric(vertical: 8),
            child: SizedBox(
              height: 90,
              width: (totalTime * 34.0).clamp(420.0, 3400.0),
              child: CustomPaint(
                painter: _GanttPainter(segments, totalTime, _colorForPid, colors.border, colors.textSecondary),
              ),
            ),
          ),
        ),
      ],
    );
  }

  List<Widget> _buildLegend(BuildContext context) {
    final colors = VosColors.of(context);
    final uniquePids = segments.map((s) => s.pid).toSet().toList()..sort();
    return uniquePids.map((pid) {
      final isIdle = pid < 0;
      final c = _colorForPid(pid);
      return Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 10,
            height: 10,
            decoration: BoxDecoration(
              color: c,
              borderRadius: BorderRadius.circular(3),
            ),
          ),
          const SizedBox(width: 6),
          Text(
            isIdle ? 'Idle' : 'Process P$pid',
            style: TextStyle(fontSize: 12, color: colors.textSecondary, fontWeight: FontWeight.w500),
          ),
        ],
      );
    }).toList();
  }
}

class _GanttPainter extends CustomPainter {
  final List<GanttSegment> segments;
  final int totalTime;
  final Color Function(int pid) colorForPid;
  final Color borderColor;
  final Color textColor;

  _GanttPainter(this.segments, this.totalTime, this.colorForPid, this.borderColor, this.textColor);

  @override
  void paint(Canvas canvas, Size size) {
    final blockH = 46.0;
    final scale = size.width / totalTime;

    final axisPaint = Paint()
      ..color = borderColor
      ..strokeWidth = 1.0;

    final borderPaint = Paint()
      ..color = Colors.black.withValues(alpha: 0.3)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.0;

    for (final seg in segments) {
      final left = seg.start * scale;
      final right = seg.end * scale;
      final rect = Rect.fromLTRB(left, 0, right, blockH);

      final c = colorForPid(seg.pid);
      final fillPaint = Paint()
        ..color = c
        ..style = PaintingStyle.fill;

      final rrect = RRect.fromRectAndRadius(rect, const Radius.circular(5));
      canvas.drawRRect(rrect, fillPaint);
      canvas.drawRRect(rrect, borderPaint);

      // Label inside block
      final label = seg.isIdle ? 'IDLE' : 'P${seg.pid}';
      final textSpan = TextSpan(
        text: label,
        style: const TextStyle(
          color: Colors.black,
          fontSize: 12,
          fontWeight: FontWeight.bold,
          letterSpacing: 0.2,
        ),
      );
      final textPainter = TextPainter(
        text: textSpan,
        textDirection: TextDirection.ltr,
      )..layout();

      final textX = left + ((right - left) - textPainter.width) / 2.0;
      final textY = (blockH - textPainter.height) / 2.0;
      if (textPainter.width < (right - left)) {
        textPainter.paint(canvas, Offset(textX, textY));
      }
    }

    // Time Axis Ticks
    canvas.drawLine(Offset(0, blockH + 6), Offset(size.width, blockH + 6), axisPaint);

    for (int t = 0; t <= totalTime; t++) {
      final x = t * scale;
      canvas.drawLine(Offset(x, blockH + 6), Offset(x, blockH + 12), axisPaint);

      final tickSpan = TextSpan(
        text: '$t',
        style: TextStyle(fontSize: 10, color: textColor, fontWeight: FontWeight.w500),
      );
      final tickPainter = TextPainter(
        text: tickSpan,
        textDirection: TextDirection.ltr,
      )..layout();

      tickPainter.paint(canvas, Offset(x - (tickPainter.width / 2.0), blockH + 16));
    }
  }

  @override
  bool shouldRepaint(covariant _GanttPainter oldDelegate) => true;
}
