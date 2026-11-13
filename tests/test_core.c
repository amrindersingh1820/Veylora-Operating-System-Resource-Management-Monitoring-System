/* Native tests for the C engine and database.  Build & run:  make test  (Linux/MSYS2)  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/types.h"
#include "core/scheduler.h"
#include "core/memory.h"
#include "core/resources.h"
#include "core/util.h"
#include "db/database.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
#define NEAR(a,b) CHECK(fabs((a)-(b)) < 1e-6)

static Process mk(int pid, int arr, int burst, int prio, int mem)
{
    Process p; memset(&p, 0, sizeof p);
    p.pid = pid; snprintf(p.name, sizeof p.name, "P%d", pid);
    p.arrival = arr; p.burst = burst; p.priority = prio; p.mem_kb = mem;
    return p;
}

static void invariants(const SchedResult *r)
{
    int busy = 0, prev_end = 0;
    for (int i = 0; i < r->ng; i++) {
        CHECK(r->g[i].start == prev_end);            /* gantt is continuous from 0 */
        prev_end = r->g[i].end;
        if (r->g[i].pid >= 0) busy += r->g[i].end - r->g[i].start;
    }
    CHECK(prev_end == r->total_time);
    CHECK(busy == r->busy_time);
    for (int i = 0; i < r->n; i++) {
        CHECK(r->r[i].tat == r->r[i].completion - r->r[i].p.arrival);
        CHECK(r->r[i].wt == r->r[i].tat - r->r[i].p.burst);
        CHECK(r->r[i].wt >= 0 && r->r[i].rt >= 0 && r->r[i].rt <= r->r[i].wt);
    }
}

int main(void)
{
    SchedResult *r = calloc(1, sizeof *r);

    /* --- sample data used by the app: avg waiting FCFS 11.0, SJF 7.6, Priority 9.8, RR(q=3) 13.8 --- */
    Process s[5] = { mk(1,0,6,2,212), mk(2,1,8,1,417), mk(3,2,7,4,112), mk(4,3,3,3,426), mk(5,4,4,2,90) };
    CHECK(sched_run(ALGO_FCFS, s, 5, 0, r) == 0);    NEAR(r->avg_wt, 11.0); invariants(r);
    CHECK(sched_run(ALGO_SJF, s, 5, 0, r) == 0);     NEAR(r->avg_wt, 7.6);  invariants(r);
    CHECK(sched_run(ALGO_PRIO_NP, s, 5, 0, r) == 0); NEAR(r->avg_wt, 9.8);  invariants(r);
    CHECK(sched_run(ALGO_RR, s, 5, 3, r) == 0);      NEAR(r->avg_wt, 13.8); invariants(r);
    CHECK(sched_run(ALGO_SRTF, s, 5, 0, r) == 0);    invariants(r);
    CHECK(sched_run(ALGO_PRIO_P, s, 5, 0, r) == 0);  invariants(r);
    CHECK(sched_run(ALGO_RR, s, 5, 0, r) != 0);      /* quantum must be positive */

    /* --- textbook SRTF example: P1(0,8) P2(1,4) P3(2,9) P4(3,5) -> average waiting time 6.5 --- */
    Process t1[4] = { mk(1,0,8,1,10), mk(2,1,4,1,10), mk(3,2,9,1,10), mk(4,3,5,1,10) };
    sched_run(ALGO_SRTF, t1, 4, 0, r); NEAR(r->avg_wt, 6.5); invariants(r);
    sched_run(ALGO_FCFS, t1, 4, 0, r); NEAR(r->avg_wt, 8.75);

    /* --- preemptive priority: high-priority latecomer preempts immediately --- */
    Process t2[2] = { mk(1,0,10,5,10), mk(2,2,3,1,10) };
    sched_run(ALGO_PRIO_P, t2, 2, 0, r);
    CHECK(r->r[1].completion == 5 && r->r[1].wt == 0 && r->r[0].completion == 13);
    sched_run(ALGO_PRIO_NP, t2, 2, 0, r);
    CHECK(r->r[0].completion == 10 && r->r[1].completion == 13);

    /* --- idle gap at the start and between jobs --- */
    Process t3[2] = { mk(1,3,2,1,10), mk(2,10,1,1,10) };
    sched_run(ALGO_FCFS, t3, 2, 0, r);
    CHECK(r->g[0].pid == -1 && r->g[0].end == 3 && r->total_time == 11); invariants(r);
    sched_run(ALGO_RR, t3, 2, 2, r); CHECK(r->total_time == 11); invariants(r);

    /* --- Round Robin: arrival during a slice queues before the preempted process --- */
    Process t4[3] = { mk(1,0,5,1,10), mk(2,1,3,1,10), mk(3,2,1,1,10) };
    sched_run(ALGO_RR, t4, 3, 2, r);
    CHECK(r->g[0].pid == 1 && r->g[1].pid == 2 && r->g[2].pid == 3 && r->g[3].pid == 1); invariants(r);

    /* --- auto selector --- */
    char why[256]; int q = 2; SchedAlgo a;
    Process one[1] = { mk(1,0,5,1,10) };
    CHECK(sched_auto_select(one, 1, &q, why, sizeof why) == ALGO_FCFS);
    Process pr[3] = { mk(1,0,5,1,10), mk(2,0,5,2,10), mk(3,0,5,3,10) };
    CHECK(sched_auto_select(pr, 3, &q, why, sizeof why) == ALGO_PRIO_NP);
    pr[1].arrival = 2; CHECK(sched_auto_select(pr, 3, &q, why, sizeof why) == ALGO_PRIO_P);
    Process bu[3] = { mk(1,0,2,1,10), mk(2,0,20,1,10), mk(3,0,6,1,10) };
    CHECK(sched_auto_select(bu, 3, &q, why, sizeof why) == ALGO_SJF);
    bu[2].arrival = 1; CHECK(sched_auto_select(bu, 3, &q, why, sizeof why) == ALGO_SRTF);
    Process eq[6]; for (int i = 0; i < 6; i++) eq[i] = mk(i + 1, 0, 6, 1, 10);
    a = sched_auto_select(eq, 6, &q, why, sizeof why); CHECK(a == ALGO_RR && q == 3);
    Process few[2] = { mk(1,0,5,1,10), mk(2,1,6,1,10) };
    CHECK(sched_auto_select(few, 2, &q, why, sizeof why) == ALGO_FCFS);
    printf("auto reason sample: %s\n", why);

    /* --- memory placement (textbook: blocks 100,500,200,300,600 ; sizes 212,417,112,426) --- */
    int blocks[5] = { 100, 500, 200, 300, 600 };
    Process mp[4] = { mk(1,0,1,1,212), mk(2,0,1,1,417), mk(3,0,1,1,112), mk(4,0,1,1,426) };
    MemResult *m = calloc(1, sizeof *m);
    mem_run(MEM_FIRST, blocks, 5, mp, 4, m);
    CHECK(m->a[0].block == 2 && m->a[1].block == 5 && m->a[2].block == 3 && !m->a[3].allocated && m->failed == 1);
    mem_run(MEM_BEST, blocks, 5, mp, 4, m);
    CHECK(m->a[0].block == 4 && m->a[1].block == 2 && m->a[2].block == 3 && m->a[3].block == 5 && m->failed == 0);
    CHECK(m->internal_frag == 88 + 83 + 88 + 174 && m->used == 212 + 417 + 112 + 426);
    mem_run(MEM_WORST, blocks, 5, mp, 4, m);
    CHECK(m->a[0].block == 5 && m->a[1].block == 2 && m->a[2].block == 4 && !m->a[3].allocated);
    mem_run(MEM_AUTO, blocks, 5, mp, 4, m);
    CHECK(m->failed == 0 && m->strategy == MEM_BEST);
    CHECK(mem_parse_blocks("100, 500,200", blocks, 5) == 3);
    CHECK(mem_parse_blocks("100, x", blocks, 5) == -1 && mem_parse_blocks("", blocks, 5) == -1 && mem_parse_blocks("0", blocks, 5) == -1);

    /* --- Banker's algorithm: classic example, 4th resource added --- */
    int mx[5][3] = { {7,5,3}, {3,2,2}, {9,0,2}, {2,2,2}, {4,3,3} };
    int al[5][3] = { {0,1,0}, {2,0,0}, {3,0,2}, {2,1,1}, {0,0,2} };
    Process bp[5];
    for (int i = 0; i < 5; i++) {
        bp[i] = mk(i + 1, 0, 1, 1, 10);
        for (int j = 0; j < 3; j++) { bp[i].max[j] = mx[i][j]; bp[i].held[j] = al[i][j]; }
        bp[i].max[3] = 2; bp[i].held[3] = 1;
    }
    int tot[NRES] = { 10, 5, 7, 6 };
    ResResult *rr = calloc(1, sizeof *rr);
    CHECK(res_run(tot, bp, 5, rr) == 0 && rr->safe);
    CHECK(rr->avail[0] == 3 && rr->avail[1] == 3 && rr->avail[2] == 2 && rr->avail[3] == 1);
    CHECK(rr->seq_len == 5 && rr->seq[0] == 1 && rr->seq[1] == 3 && rr->seq[2] == 4 && rr->seq[3] == 0 && rr->seq[4] == 2);
    bp[1].held[0] = 3; bp[1].held[2] = 2; bp[0].held[1] = 3;          /* textbook unsafe state: avail (2,1,0) */
    CHECK(res_run(tot, bp, 5, rr) == 0 && rr->valid && !rr->safe);
    tot[0] = 5; CHECK(res_run(tot, bp, 5, rr) != 0 && !rr->valid);    /* more held than exists */
    tot[0] = 10;
    CHECK(res_parse_totals("10,5,7,6", tot) == 0 && res_parse_totals("10,5,7", tot) != 0 && res_parse_totals("a,b,c,d", tot) != 0);

    /* --- database round trip --- */
    remove("test_vos.db");
    CHECK(db_open("test_vos.db") == 0);
    Process p = mk(0, 2, 7, 3, 128); strcpy(p.name, "Alpha"); p.max[2] = 3; p.held[2] = 1;
    CHECK(db_save_process(&p) == 0 && p.pid > 0);
    p.burst = 9; CHECK(db_save_process(&p) == 0);
    Process back[8]; CHECK(db_load_processes(back, 8) == 1 && back[0].burst == 9 && back[0].max[2] == 3 && !strcmp(back[0].name, "Alpha"));
    Process bad = mk(0, 0, 0, 1, 10); CHECK(db_save_process(&bad) != 0);      /* CHECK constraint: burst > 0 */

    RunBundle *b = calloc(1, sizeof *b), *b2 = calloc(1, sizeof *b2);
    int totals[NRES] = { 10, 5, 7, 6 };
    sched_run(ALGO_RR, s, 5, 3, &b->sched); strcpy(b->sched.reason, "unit test");
    mem_run(MEM_AUTO, blocks, 5, s, 5, &b->mem);
    for (int i = 0; i < 5; i++) { s[i].max[0] = 2; s[i].held[0] = 1; }
    res_run(totals, s, 5, &b->res);
    CHECK(mem_parse_blocks("100,500,200,300,600", blocks, 5) == 5);
    int id = db_save_run(b, "100,500,200,300,600", "10,5,7,6");
    CHECK(id > 0 && b->created[0]);
    char mb[64], rt[64];
    CHECK(db_load_run(id, b2, mb, sizeof mb, rt, sizeof rt) == 0);
    CHECK(b2->sched.n == 5 && b2->sched.ng == b->sched.ng && b2->sched.quantum == 3);
    NEAR(b2->sched.avg_wt, b->sched.avg_wt);
    CHECK(!strcmp(mb, "100,500,200,300,600") && b2->mem.n == 5 && b2->mem.nblocks == 5);
    CHECK(b2->mem.used == b->mem.used && b2->mem.internal_frag == b->mem.internal_frag && b2->mem.failed == b->mem.failed);
    CHECK(b2->res.valid && b2->res.safe == b->res.safe && b2->res.seq_len == b->res.seq_len);
    CHECK(!strcmp(b2->sched.reason, "unit test") && b2->sched.r[2].wt == b->sched.r[2].wt);
    RunSummary list[8]; CHECK(db_list_runs(list, 8) == 1 && list[0].run_id == id && list[0].nproc == 5);
    CHECK(db_delete_run(id) == 0 && db_list_runs(list, 8) == 0);
    db_set_setting("k", "v1"); db_set_setting("k", "v2");
    char sv[16]; db_get_setting("k", sv, sizeof sv, "d"); CHECK(!strcmp(sv, "v2"));
    db_get_setting("none", sv, sizeof sv, "dflt"); CHECK(!strcmp(sv, "dflt"));
    CHECK(db_delete_process(p.pid) == 0 && db_load_processes(back, 8) == 0);
    db_close(); remove("test_vos.db");

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
