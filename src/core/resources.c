#include "resources.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

const char *RES_NAMES[NRES] = { "Printer", "Scanner", "Disk", "Network" };

int res_parse_totals(const char *text, int total[NRES])
{
    int v[NRES + 1];
    int n = parse_int_list(text, v, NRES + 1);
    if (n != NRES) return -1;
    for (int j = 0; j < NRES; j++) { if (v[j] < 0 || v[j] > 1000) return -1; total[j] = v[j]; }
    return 0;
}

int res_run(const int total[NRES], const Process *procs, int n, ResResult *out)
{
    memset(out, 0, sizeof *out);
    out->n = n;
    for (int j = 0; j < NRES; j++) { out->total[j] = total[j]; out->avail[j] = total[j]; }

    for (int i = 0; i < n; i++)
        for (int j = 0; j < NRES; j++) {
            if (procs[i].held[j] > procs[i].max[j]) {
                snprintf(out->message, sizeof out->message, "%s holds more %s (%d) than its maximum claim (%d).",
                         procs[i].name, RES_NAMES[j], procs[i].held[j], procs[i].max[j]);
                return -1;
            }
            if (procs[i].max[j] > total[j]) {
                snprintf(out->message, sizeof out->message, "%s may need %d %s but the system only has %d.",
                         procs[i].name, procs[i].max[j], RES_NAMES[j], total[j]);
                return -1;
            }
            out->avail[j] -= procs[i].held[j];
            out->need[i][j] = procs[i].max[j] - procs[i].held[j];
        }
    for (int j = 0; j < NRES; j++)
        if (out->avail[j] < 0) {
            snprintf(out->message, sizeof out->message,
                     "The selected processes hold more %s than exist (total %d). Raise the resource totals.",
                     RES_NAMES[j], total[j]);
            return -1;
        }
    out->valid = 1;

    /* Banker's safety algorithm */
    int work[NRES], finish[MAX_PROC] = {0};
    for (int j = 0; j < NRES; j++) work[j] = out->avail[j];
    int progress = 1;
    while (progress) {
        progress = 0;
        for (int i = 0; i < n; i++) {
            if (finish[i]) continue;
            int ok = 1;
            for (int j = 0; j < NRES; j++) if (out->need[i][j] > work[j]) { ok = 0; break; }
            if (!ok) continue;
            for (int j = 0; j < NRES; j++) work[j] += procs[i].held[j];
            finish[i] = 1; out->seq[out->seq_len++] = i; progress = 1;
        }
    }
    out->safe = (out->seq_len == n);
    snprintf(out->message, sizeof out->message, out->safe
             ? "SAFE state: every process can finish, so no deadlock."
             : "UNSAFE state: a deadlock is possible with the current allocation.");
    return 0;
}
