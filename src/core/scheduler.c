#include "scheduler.h"
#include <stdio.h>
#include <string.h>

static const char *FULL[ALGO_COUNT] = {
    "Auto", "FCFS (First Come First Served)", "SJF (Shortest Job First)",
    "SRTF (Shortest Remaining Time First)", "Priority (non-preemptive)",
    "Priority (preemptive)", "Round Robin"
};
static const char *SHORT[ALGO_COUNT] = {
    "Auto", "FCFS", "SJF", "SRTF", "Priority NP", "Priority P", "Round Robin"
};
static const char *INFO[ALGO_COUNT] = {
    "Chosen automatically from the process set.",
    "Runs processes in order of arrival; simple but short jobs can wait behind long ones.",
    "Runs the shortest job first without interrupting it; gives low average waiting time.",
    "Preemptive SJF: always runs the process with the least time remaining.",
    "Runs the highest-priority ready process to completion (1 = highest).",
    "Always runs the highest-priority ready process and preempts lower ones.",
    "Gives each process a fixed time slice in turn; fair and good response time."
};

const char *sched_algo_name(SchedAlgo a)  { return (a >= 0 && a < ALGO_COUNT) ? FULL[a]  : "?"; }
const char *sched_algo_short(SchedAlgo a) { return (a >= 0 && a < ALGO_COUNT) ? SHORT[a] : "?"; }
const char *sched_algo_info(SchedAlgo a)  { return (a >= 0 && a < ALGO_COUNT) ? INFO[a]  : "?"; }

/* ---- small circular queue for Round Robin ---- */
typedef struct { int a[MAX_PROC + 1]; int head, tail; } Queue;
static void q_init(Queue *q)            { q->head = q->tail = 0; }
static int  q_empty(const Queue *q)     { return q->head == q->tail; }
static void q_push(Queue *q, int v)     { q->a[q->tail] = v; q->tail = (q->tail + 1) % (MAX_PROC + 1); }
static int  q_pop(Queue *q)             { int v = q->a[q->head]; q->head = (q->head + 1) % (MAX_PROC + 1); return v; }

/* ---- Gantt chart: one tick = one time unit, equal neighbours are merged ---- */
static void add_tick(SchedResult *r, int pid, int t)
{
    if (r->ng > 0 && r->g[r->ng - 1].pid == pid && r->g[r->ng - 1].end == t) {
        r->g[r->ng - 1].end = t + 1;
        return;
    }
    if (r->ng < MAX_GANTT) {
        r->g[r->ng].pid = pid; r->g[r->ng].start = t; r->g[r->ng].end = t + 1;
        r->ng++;
    }
}

/* ---- picking the next process for the non-RR algorithms ---- */
static int cmp_key(SchedAlgo a, const Process *x, int rx, const Process *y, int ry)
{
    int kx, ky;
    switch (a) {
    case ALGO_FCFS: kx = x->arrival;  ky = y->arrival;  break;
    case ALGO_SJF:  kx = x->burst;    ky = y->burst;    break;
    case ALGO_SRTF: kx = rx;          ky = ry;          break;
    default:        kx = x->priority; ky = y->priority; break;
    }
    if (kx != ky) return kx < ky ? -1 : 1;
    if (x->arrival != y->arrival) return x->arrival < y->arrival ? -1 : 1;
    return 0;                                   /* tie: keep the earlier candidate */
}

static int choose(SchedAlgo a, const Process *p, const int *rem, int n, int t, int cur)
{
    int best = cur;                             /* the running process wins ties */
    for (int i = 0; i < n; i++) {
        if (i == cur || rem[i] <= 0 || p[i].arrival > t) continue;
        if (best < 0 || cmp_key(a, &p[i], rem[i], &p[best], rem[best]) < 0) best = i;
    }
    return best;
}

int sched_run(SchedAlgo algo, const Process *p, int n, int quantum, SchedResult *out)
{
    if (n <= 0 || n > MAX_PROC || algo <= ALGO_AUTO || algo >= ALGO_COUNT) return -1;
    if (algo == ALGO_RR && quantum <= 0) return -1;

    memset(out, 0, sizeof *out);
    out->algo = algo;
    snprintf(out->algo_name, sizeof out->algo_name, "%s", FULL[algo]);
    out->quantum = (algo == ALGO_RR) ? quantum : 0;
    out->n = n;

    int rem[MAX_PROC], first[MAX_PROC], comp[MAX_PROC], enq[MAX_PROC], order[MAX_PROC];
    for (int i = 0; i < n; i++) { rem[i] = p[i].burst; first[i] = -1; comp[i] = -1; enq[i] = 0; order[i] = i; }
    for (int i = 1; i < n; i++) {               /* order by (arrival, index) for RR admission */
        int v = order[i], j = i - 1;
        while (j >= 0 && p[order[j]].arrival > p[v].arrival) { order[j + 1] = order[j]; j--; }
        order[j + 1] = v;
    }

    Queue q; q_init(&q);
    int t = 0, done = 0, cur = -1, slice = 0;
    int preemptive = (algo == ALGO_SRTF || algo == ALGO_PRIO_P);

    while (done < n) {
        if (algo == ALGO_RR) {
            for (int k = 0; k < n; k++) { int i = order[k]; if (!enq[i] && p[i].arrival <= t) { enq[i] = 1; q_push(&q, i); } }
            if (cur < 0 && !q_empty(&q)) { cur = q_pop(&q); slice = 0; }
        } else if (cur < 0 || preemptive) {
            cur = choose(algo, p, rem, n, t, cur);
        }
        if (cur < 0) { add_tick(out, -1, t); t++; continue; }       /* CPU idle */

        if (first[cur] < 0) first[cur] = t;
        rem[cur]--; slice++;
        add_tick(out, p[cur].pid, t);
        t++;

        if (algo == ALGO_RR)                    /* arrivals during the slice queue up before the preempted process */
            for (int k = 0; k < n; k++) { int i = order[k]; if (!enq[i] && p[i].arrival <= t) { enq[i] = 1; q_push(&q, i); } }

        if (rem[cur] == 0)                       { comp[cur] = t; done++; cur = -1; }
        else if (algo == ALGO_RR && slice >= quantum) { q_push(&q, cur); cur = -1; }
    }

    double sw = 0, st = 0, sr = 0;
    for (int i = 0; i < n; i++) {
        ProcResult *r = &out->r[i];
        r->p = p[i];
        r->completion = comp[i];
        r->tat = comp[i] - p[i].arrival;
        r->wt  = r->tat - p[i].burst;
        r->rt  = first[i] - p[i].arrival;
        sw += r->wt; st += r->tat; sr += r->rt;
        out->busy_time += p[i].burst;
    }
    out->total_time = t;
    out->avg_wt = sw / n; out->avg_tat = st / n; out->avg_rt = sr / n;
    out->cpu_util   = t > 0 ? 100.0 * out->busy_time / t : 0;
    out->throughput = t > 0 ? (double)n / t : 0;

    int prev = -2;                               /* context switches = changes between different processes */
    for (int i = 0; i < out->ng; i++) {
        if (out->g[i].pid < 0) continue;
        if (prev != -2 && prev != out->g[i].pid) out->ctx_switches++;
        prev = out->g[i].pid;
    }
    return 0;
}

SchedAlgo sched_auto_select(const Process *p, int n, int *quantum, char *reason, size_t rs)
{
    int minB = p[0].burst, maxB = p[0].burst, minA = p[0].arrival, maxA = p[0].arrival;
    int distinct = 0, seen[MAX_PROC], sumB = 0;
    for (int i = 0; i < n; i++) {
        if (p[i].burst < minB) minB = p[i].burst;
        if (p[i].burst > maxB) maxB = p[i].burst;
        if (p[i].arrival < minA) minA = p[i].arrival;
        if (p[i].arrival > maxA) maxA = p[i].arrival;
        sumB += p[i].burst;
        int dup = 0;
        for (int j = 0; j < distinct; j++) if (seen[j] == p[i].priority) { dup = 1; break; }
        if (!dup) seen[distinct++] = p[i].priority;
    }
    int staggered = (maxA > minA);
    double ratio = (double)maxB / (double)minB;
    double avgB = (double)sumB / n;

    if (n == 1) {
        snprintf(reason, rs, "Auto: only one process, so scheduling order does not matter - FCFS.");
        return ALGO_FCFS;
    }
    if (distinct >= 3) {
        snprintf(reason, rs, "Auto: %d processes use %d different priority levels, so priorities matter - %s priority scheduling%s.",
                 n, distinct, staggered ? "preemptive" : "non-preemptive",
                 staggered ? " (processes arrive at different times)" : " (all arrive together)");
        return staggered ? ALGO_PRIO_P : ALGO_PRIO_NP;
    }
    if (ratio >= 3.0) {
        snprintf(reason, rs, "Auto: burst times vary a lot (%d to %d, ratio %.1f) - favour short jobs with %s.",
                 minB, maxB, ratio, staggered ? "SRTF (arrivals are staggered)" : "SJF (all arrive together)");
        return staggered ? ALGO_SRTF : ALGO_SJF;
    }
    if (n >= 5) {
        int q = (int)(avgB / 2.0 + 0.5);
        if (q < 2) q = 2;
        *quantum = q;
        snprintf(reason, rs, "Auto: %d processes with similar burst times (%d to %d) and little priority difference - Round Robin, quantum %d, for fairness.",
                 n, minB, maxB, q);
        return ALGO_RR;
    }
    snprintf(reason, rs, "Auto: only %d processes with similar burst times and few priorities - plain FCFS is enough.", n);
    return ALGO_FCFS;
}
