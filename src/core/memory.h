#ifndef VOS_MEMORY_H
#define VOS_MEMORY_H
#include "types.h"

const char *mem_strategy_name(MemStrategy s);
/* Parses "100, 500, 200" into block sizes. Returns the count or -1 if invalid. */
int mem_parse_blocks(const char *text, int *blocks, int max);
/* Places each process into a fixed partition. MEM_AUTO tries all three strategies and keeps the best. */
int mem_run(MemStrategy s, const int *blocks, int nb, const Process *procs, int n, MemResult *out);
/* Recomputes totals (used, fragmentation, failed...) from out->a[] and out->blocks[]. */
void mem_finalize(MemResult *m);
#endif
