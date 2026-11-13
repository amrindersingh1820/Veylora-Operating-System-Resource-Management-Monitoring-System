#include "memory.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

static const char *NAMES[MEM_COUNT] = { "Auto", "First Fit", "Best Fit", "Worst Fit" };
const char *mem_strategy_name(MemStrategy s) { return (s >= 0 && s < MEM_COUNT) ? NAMES[s] : "?"; }

int mem_parse_blocks(const char *text, int *blocks, int max)
{
    int n = parse_int_list(text, blocks, max);
    if (n < 1) return -1;
    for (int i = 0; i < n; i++) if (blocks[i] < 1 || blocks[i] > 100000) return -1;
    return n;
}

/* index of the free block chosen for `size`, or -1. free_[i] == 0 means the block is taken. */
static int pick(MemStrategy s, const int *blocks, const int *free_, int nb, int size)
{
    int best = -1;
    for (int i = 0; i < nb; i++) {
        if (!free_[i] || blocks[i] < size) continue;
        if (s == MEM_FIRST) return i;
        if (best < 0 ||
            (s == MEM_BEST  && blocks[i] < blocks[best]) ||
            (s == MEM_WORST && blocks[i] > blocks[best])) best = i;
    }
    return best;
}

void mem_finalize(MemResult *m)
{
    m->total_mem = m->used = m->internal_frag = m->free_kb = m->failed = 0;
    int taken[MAX_BLOCKS] = {0};
    for (int i = 0; i < m->nblocks; i++) m->total_mem += m->blocks[i];
    for (int i = 0; i < m->n; i++) {
        if (m->a[i].allocated) {
            m->used += m->a[i].size;
            m->internal_frag += m->a[i].frag;
            if (m->a[i].block >= 1 && m->a[i].block <= m->nblocks) taken[m->a[i].block - 1] = 1;
        } else m->failed++;
    }
    for (int i = 0; i < m->nblocks; i++) if (!taken[i]) m->free_kb += m->blocks[i];
}

static void place_all(MemStrategy s, const int *blocks, int nb, const Process *procs, int n, MemResult *out)
{
    int free_[MAX_BLOCKS];
    memset(out, 0, sizeof *out);
    out->strategy = s;
    snprintf(out->strategy_name, sizeof out->strategy_name, "%s", NAMES[s]);
    out->nblocks = nb;
    for (int i = 0; i < nb; i++) { out->blocks[i] = blocks[i]; free_[i] = 1; }
    out->n = n;
    for (int i = 0; i < n; i++) {
        MemAlloc *a = &out->a[i];
        a->pid = procs[i].pid;
        snprintf(a->name, sizeof a->name, "%s", procs[i].name);
        a->size = procs[i].mem_kb;
        int b = pick(s, blocks, free_, nb, a->size);
        if (b >= 0) {
            free_[b] = 0; a->allocated = 1; a->block = b + 1;
            a->block_size = blocks[b]; a->frag = blocks[b] - a->size;
        }
    }
    mem_finalize(out);
}

int mem_run(MemStrategy s, const int *blocks, int nb, const Process *procs, int n, MemResult *out)
{
    if (nb < 1 || nb > MAX_BLOCKS || n < 1 || n > MAX_PROC || s < 0 || s >= MEM_COUNT) return -1;
    if (s != MEM_AUTO) {
        place_all(s, blocks, nb, procs, n, out);
        snprintf(out->reason, sizeof out->reason, "Manually selected %s.", NAMES[s]);
        return 0;
    }
    /* Auto: fewest processes left without memory, then least wasted space; ties prefer Best Fit. */
    static const MemStrategy try_order[3] = { MEM_BEST, MEM_FIRST, MEM_WORST };
    MemResult cand, best; int have = 0;
    for (int k = 0; k < 3; k++) {
        place_all(try_order[k], blocks, nb, procs, n, &cand);
        if (!have || cand.failed < best.failed ||
            (cand.failed == best.failed && cand.internal_frag < best.internal_frag)) { best = cand; have = 1; }
    }
    *out = best;
    out->strategy = MEM_AUTO;
    snprintf(out->strategy_name, sizeof out->strategy_name, "Auto -> %s", NAMES[best.strategy]);
    snprintf(out->reason, sizeof out->reason,
             "Auto: compared all three strategies; %s leaves %d process(es) unplaced and wastes %d KB.",
             NAMES[best.strategy], best.failed, best.internal_frag);
    /* keep the winning strategy's id in strategy so it can be reproduced */
    out->strategy = best.strategy;
    return 0;
}
