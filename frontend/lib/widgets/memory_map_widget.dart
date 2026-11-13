import 'package:flutter/material.dart';

import '../core/theme.dart';
import '../data/models/simulation_models.dart';

class MemoryMapWidget extends StatelessWidget {
  final MemResultModel memResult;

  const MemoryMapWidget({super.key, required this.memResult});

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);

    if (memResult.allocations.isEmpty) {
      return Container(
        height: 120,
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: colors.card,
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: colors.border),
        ),
        child: Text('No memory partitions allocated', style: TextStyle(color: colors.textMuted, fontSize: 13)),
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // Summary KPIs
        Wrap(
          spacing: 12,
          runSpacing: 8,
          children: [
            _buildStat(context, 'Total Memory', '${memResult.totalMem} KB', colors.primary),
            _buildStat(context, 'Used Memory', '${memResult.usedMem} KB', colors.success),
            _buildStat(context, 'Internal Frag', '${memResult.internalFrag} KB', colors.warning),
            _buildStat(context, 'Free Space', '${memResult.freeMem} KB', colors.info),
          ],
        ),
        const SizedBox(height: 16),

        // Block Visualizer Grid
        LayoutBuilder(
          builder: (context, constraints) {
            return Wrap(
              spacing: 12,
              runSpacing: 12,
              children: memResult.allocations.map((alloc) {
                return _buildBlockCard(context, alloc);
              }).toList(),
            );
          },
        ),
      ],
    );
  }

  Widget _buildStat(BuildContext context, String label, String value, Color color) {
    final colors = VosColors.of(context);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
      decoration: BoxDecoration(
        color: colors.card,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: colors.border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: TextStyle(color: colors.textSecondary, fontSize: 11)),
          const SizedBox(height: 2),
          Text(
            value,
            style: TextStyle(
              color: color,
              fontSize: 15,
              fontWeight: FontWeight.bold,
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildBlockCard(BuildContext context, MemAllocation alloc) {
    final colors = VosColors.of(context);
    final hasAlloc = alloc.allocated && alloc.block > 0;
    return Container(
      width: 160,
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: hasAlloc ? colors.card : colors.bg,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(
          color: hasAlloc ? colors.primary.withValues(alpha: 0.5) : colors.border,
          width: 1.5,
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Expanded(
                child: Text(
                  'Block #${alloc.block > 0 ? alloc.block : "-"}',
                  style: TextStyle(fontWeight: FontWeight.bold, fontSize: 12, color: colors.textPrimary),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                decoration: BoxDecoration(
                  color: hasAlloc ? colors.success.withValues(alpha: 0.15) : colors.danger.withValues(alpha: 0.15),
                  borderRadius: BorderRadius.circular(4),
                ),
                child: Text(
                  hasAlloc ? 'PLACED' : 'FAILED',
                  style: TextStyle(
                    fontSize: 9,
                    fontWeight: FontWeight.bold,
                    color: hasAlloc ? colors.success : colors.danger,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Text('Capacity: ${alloc.blockSize} KB', style: TextStyle(color: colors.textSecondary, fontSize: 11)),
          Text('Process: ${alloc.name}', style: TextStyle(color: colors.primary, fontSize: 12, fontWeight: FontWeight.bold)),
          Text('Requested: ${alloc.size} KB', style: TextStyle(color: colors.textSecondary, fontSize: 11)),
          if (alloc.frag > 0)
            Text('Waste: ${alloc.frag} KB', style: TextStyle(color: colors.warning, fontSize: 11, fontWeight: FontWeight.w600)),
        ],
      ),
    );
  }
}
