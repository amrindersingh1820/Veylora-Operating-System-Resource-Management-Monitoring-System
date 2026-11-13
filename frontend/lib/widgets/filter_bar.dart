import 'package:flutter/material.dart';
import '../core/theme.dart';

class FilterBar extends StatelessWidget {
  final TextEditingController searchController;
  final String searchHint;
  final ValueChanged<String>? onSearchChanged;
  final VoidCallback? onSearchCleared;
  final List<Widget>? filterDropdowns;
  final Widget? primaryAction;

  const FilterBar({
    super.key,
    required this.searchController,
    this.searchHint = 'Search...',
    this.onSearchChanged,
    this.onSearchCleared,
    this.filterDropdowns,
    this.primaryAction,
  });

  @override
  Widget build(BuildContext context) {
    final colors = VosColors.of(context);

    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: colors.card,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: colors.border),
      ),
      child: Row(
        children: [
          // Search Input
          Expanded(
            flex: 3,
            child: SizedBox(
              height: 36,
              child: TextField(
                controller: searchController,
                style: TextStyle(fontSize: 13, color: colors.textPrimary),
                decoration: InputDecoration(
                  hintText: searchHint,
                  hintStyle: TextStyle(fontSize: 12, color: colors.textMuted),
                  prefixIcon: Icon(Icons.search, size: 16, color: colors.textSecondary),
                  suffixIcon: searchController.text.isNotEmpty
                      ? IconButton(
                          icon: const Icon(Icons.close, size: 14),
                          onPressed: () {
                            searchController.clear();
                            onSearchCleared?.call();
                            onSearchChanged?.call('');
                          },
                        )
                      : null,
                  contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
                  filled: true,
                  fillColor: colors.bg,
                  border: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(6),
                    borderSide: BorderSide(color: colors.border),
                  ),
                  enabledBorder: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(6),
                    borderSide: BorderSide(color: colors.border),
                  ),
                  focusedBorder: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(6),
                    borderSide: BorderSide(color: colors.primary, width: 1.5),
                  ),
                ),
                onChanged: onSearchChanged,
              ),
            ),
          ),

          // Optional Dropdown Filters
          if (filterDropdowns != null && filterDropdowns!.isNotEmpty) ...[
            const SizedBox(width: 12),
            ...filterDropdowns!,
          ],

          // Primary Action
          if (primaryAction != null) ...[
            const SizedBox(width: 12),
            primaryAction!,
          ],
        ],
      ),
    );
  }
}

