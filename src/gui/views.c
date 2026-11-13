/* Fills the list views and summary labels from simulation data. */
#include "gui.h"
#include "core/scheduler.h"
#include "core/memory.h"
#include "core/resources.h"
#include "db/database.h"
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- list-view helpers */
HWND lv_create(HWND parent, int id, DWORD ex)
{
    HWND lv = CreateWindowExA(WS_EX_CLIENTEDGE, "SysListView32", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0, 0, 100, 100, parent, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessageA(lv, WM_SETFONT, (WPARAM)g.font, TRUE);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | ex);
    return lv;
}

void lv_col(HWND lv, int i, const char *text, int w, int right)
{
    LVCOLUMNA c; memset(&c, 0, sizeof c);
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
    c.fmt = right ? LVCFMT_RIGHT : LVCFMT_LEFT; c.cx = w; c.pszText = (char *)text; c.iSubItem = i;
    SendMessageA(lv, LVM_INSERTCOLUMNA, i, (LPARAM)&c);
}

int lv_add(HWND lv, const char *text, LPARAM param)
{
    LVITEMA it; memset(&it, 0, sizeof it);
    it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = ListView_GetItemCount(lv);
    it.pszText = (char *)text; it.lParam = param;
    return (int)SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)&it);
}

void lv_set(HWND lv, int row, int col, const char *text)
{
    LVITEMA it; memset(&it, 0, sizeof it);
    it.iSubItem = col; it.pszText = (char *)text;
    SendMessageA(lv, LVM_SETITEMTEXTA, row, (LPARAM)&it);
}

static void seti(HWND lv, int row, int col, int v) { char b[32]; snprintf(b, sizeof b, "%d", v); lv_set(lv, row, col, b); }
static void setf(HWND lv, int row, int col, double v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); lv_set(lv, row, col, b); }

/* "P:1/7 S:0/5 D:2/3 N:1/2" (held/max) */
static void fmt_held_max(const Process *p, char *out, size_t n)
{
    size_t len = 0; out[0] = 0;
    for (int j = 0; j < NRES; j++)
        len += snprintf(out + len, n - len, "%s%c:%d/%d", j ? " " : "", RES_NAMES[j][0], p->held[j], p->max[j]);
}
static void fmt_held(const Process *p, char *out, size_t n)
{
    size_t len = 0; out[0] = 0;
    for (int j = 0; j < NRES; j++)
        len += snprintf(out + len, n - len, "%s%c:%d", j ? " " : "", RES_NAMES[j][0], p->held[j]);
}

/* ---------------------------------------------------------------- columns */
void views_init_columns(void)
{
    lv_col(g.lvProc, 0, "Name", 82, 0);  lv_col(g.lvProc, 1, "Arr", 40, 1); lv_col(g.lvProc, 2, "Burst", 46, 1);
    lv_col(g.lvProc, 3, "Prio", 40, 1);  lv_col(g.lvProc, 4, "Mem KB", 56, 1);
    lv_col(g.lvProc, 5, "Resources held/max (P S D N)", 215, 0);

    const char *rc[] = { "Process", "Arrival", "Burst", "Prio", "Finish", "TAT", "WT", "Resp", "Memory", "Resources held" };
    int rw[] = { 64, 56, 48, 44, 54, 48, 48, 50, 130, 150 };
    for (int i = 0; i < 10; i++) lv_col(g.lvResult, i, rc[i], rw[i], i >= 1 && i <= 7);

    const char *mc[] = { "Process", "Needs (KB)", "Block", "Block size (KB)", "Wasted (KB)", "Status" };
    int mw[] = { 80, 90, 60, 110, 100, 240 };
    for (int i = 0; i < 6; i++) lv_col(g.lvMem, i, mc[i], mw[i], i >= 1 && i <= 4);

    lv_col(g.lvRes, 0, "Process", 90, 0);
    for (int j = 0; j < NRES; j++) lv_col(g.lvRes, 1 + j, RES_NAMES[j], 84, 1);   /* cells show held / max */
    lv_col(g.lvRes, 5, "Still needs", 130, 0); lv_col(g.lvRes, 6, "Safe order", 80, 1);

    const char *cc[] = { "Algorithm", "Avg WT", "Avg TAT", "Avg RT", "CPU %", "Ctx switches", "Total time", "Notes" };
    int cw[] = { 140, 60, 62, 60, 55, 90, 78, 170 };
    for (int i = 0; i < 8; i++) lv_col(g.lvCmp, i, cc[i], cw[i], i >= 1 && i <= 6);

    const char *hc[] = { "Run #", "Date and time", "Algorithm", "Processes", "Avg WT", "Avg TAT" };
    int hw[] = { 60, 150, 230, 80, 80, 80 };
    for (int i = 0; i < 6; i++) lv_col(g.lvHist, i, hc[i], hw[i], i == 0 || i >= 3);
}

/* ---------------------------------------------------------------- process list */
void views_refresh_processes(void)
{
    int oldpid[MAX_PROC], oldchk[MAX_PROC], nold = ListView_GetItemCount(g.lvProc);
    if (nold > MAX_PROC) nold = MAX_PROC;
    for (int i = 0; i < nold; i++) {
        LVITEMA it; memset(&it, 0, sizeof it); it.mask = LVIF_PARAM; it.iItem = i;
        SendMessageA(g.lvProc, LVM_GETITEMA, 0, (LPARAM)&it);
        oldpid[i] = (int)it.lParam; oldchk[i] = ListView_GetCheckState(g.lvProc, i);
    }
    g.populating = 1;
    ListView_DeleteAllItems(g.lvProc);
    for (int i = 0; i < g.nprocs; i++) {
        const Process *p = &g.procs[i]; char b[96];
        int row = lv_add(g.lvProc, p->name, p->pid);
        seti(g.lvProc, row, 1, p->arrival); seti(g.lvProc, row, 2, p->burst);
        seti(g.lvProc, row, 3, p->priority); seti(g.lvProc, row, 4, p->mem_kb);
        fmt_held_max(p, b, sizeof b); lv_set(g.lvProc, row, 5, b);
        int chk = 1;                                  /* new processes start ticked */
        for (int k = 0; k < nold; k++) if (oldpid[k] == p->pid) { chk = oldchk[k]; break; }
        ListView_SetCheckState(g.lvProc, row, chk);
    }
    g.populating = 0;
}

int views_get_selected(Process *out)
{
    int n = 0;
    for (int i = 0; i < g.nprocs && i < ListView_GetItemCount(g.lvProc); i++)
        if (ListView_GetCheckState(g.lvProc, i)) out[n++] = g.procs[i];
    return n;
}

/* ---------------------------------------------------------------- result tabs */
void views_show_empty(void)
{
    SetWindowTextA(g.stSched, "Tick the processes you want on the left, choose an algorithm (or Auto) and press Run Simulation.");
    SetWindowTextA(g.stMem,   "Memory allocation results will appear here after a simulation.");
    SetWindowTextA(g.stRes,   "Resource usage and the deadlock (Banker's algorithm) check will appear here after a simulation.");
    SetWindowTextA(g.stCmp,   "Press 'Compare All Algorithms' to run every algorithm on the ticked processes.");
    ListView_DeleteAllItems(g.lvResult); ListView_DeleteAllItems(g.lvMem); ListView_DeleteAllItems(g.lvRes);
    InvalidateRect(g.gantt, NULL, FALSE); InvalidateRect(g.memmap, NULL, FALSE);
}

static void show_sched(const RunBundle *b)
{
    const SchedResult *s = &b->sched; char t[900], q[32] = "-", saved[80] = "Not saved to the database.";
    if (s->quantum) snprintf(q, sizeof q, "%d", s->quantum);
    if (b->run_id) snprintf(saved, sizeof saved, "Saved in the database as run #%d (%s).", b->run_id, b->created);
    snprintf(t, sizeof t,
        "Algorithm: %s     Time quantum: %s     %s\n%s\n"
        "Average waiting time (WT): %.2f     Average turnaround time (TAT): %.2f     Average response: %.2f\n"
        "CPU utilisation: %.1f%%     Throughput: %.3f jobs/unit     Context switches: %d     Total time: %d     "
        "(TAT = finish - arrival,  WT = TAT - burst)",
        s->algo_name, q, saved, s->reason, s->avg_wt, s->avg_tat, s->avg_rt, s->cpu_util, s->throughput,
        s->ctx_switches, s->total_time);
    SetWindowTextA(g.stSched, t);

    ListView_DeleteAllItems(g.lvResult);
    for (int i = 0; i < s->n; i++) {
        const ProcResult *r = &s->r[i]; char b2[96];
        int row = lv_add(g.lvResult, r->p.name, r->p.pid);
        seti(g.lvResult, row, 1, r->p.arrival); seti(g.lvResult, row, 2, r->p.burst); seti(g.lvResult, row, 3, r->p.priority);
        seti(g.lvResult, row, 4, r->completion); seti(g.lvResult, row, 5, r->tat); seti(g.lvResult, row, 6, r->wt);
        seti(g.lvResult, row, 7, r->rt);
        const MemAlloc *a = NULL;
        for (int k = 0; k < b->mem.n; k++) if (b->mem.a[k].pid == r->p.pid) { a = &b->mem.a[k]; break; }
        if (a && a->allocated) snprintf(b2, sizeof b2, "%d KB in B%d (%d)", a->size, a->block, a->block_size);
        else snprintf(b2, sizeof b2, "%d KB - not placed", r->p.mem_kb);
        lv_set(g.lvResult, row, 8, b2);
        fmt_held(&r->p, b2, sizeof b2); lv_set(g.lvResult, row, 9, b2);
    }
    int row = lv_add(g.lvResult, "Average", 0);
    setf(g.lvResult, row, 5, s->avg_tat); setf(g.lvResult, row, 6, s->avg_wt); setf(g.lvResult, row, 7, s->avg_rt);
    InvalidateRect(g.gantt, NULL, FALSE);
}

static void show_mem(const RunBundle *b)
{
    const MemResult *m = &b->mem; char t[600], blocks[200] = "";
    for (int i = 0; i < m->nblocks; i++) {
        size_t l = strlen(blocks); snprintf(blocks + l, sizeof blocks - l, "%s%d", i ? ", " : "", m->blocks[i]);
    }
    snprintf(t, sizeof t,
        "Strategy: %s     Blocks (KB): %s     Total memory: %d KB\n%s\n"
        "Memory used by processes: %d KB     Wasted inside blocks (internal fragmentation): %d KB     "
        "Free unused blocks: %d KB     Processes not placed: %d",
        m->strategy_name, blocks, m->total_mem, m->reason, m->used, m->internal_frag, m->free_kb, m->failed);
    SetWindowTextA(g.stMem, t);

    ListView_DeleteAllItems(g.lvMem);
    for (int i = 0; i < m->n; i++) {
        const MemAlloc *a = &m->a[i]; char b2[32];
        int row = lv_add(g.lvMem, a->name, a->pid);
        seti(g.lvMem, row, 1, a->size);
        if (a->allocated) {
            snprintf(b2, sizeof b2, "B%d", a->block); lv_set(g.lvMem, row, 2, b2);
            seti(g.lvMem, row, 3, a->block_size); seti(g.lvMem, row, 4, a->frag);
            lv_set(g.lvMem, row, 5, "Allocated");
        } else {
            lv_set(g.lvMem, row, 2, "-"); lv_set(g.lvMem, row, 3, "-"); lv_set(g.lvMem, row, 4, "-");
            lv_set(g.lvMem, row, 5, "NOT allocated - no free block is large enough");
        }
    }
    InvalidateRect(g.memmap, NULL, FALSE);
}

static void show_res(const RunBundle *b)
{
    const ResResult *r = &b->res; const SchedResult *s = &b->sched;
    ListView_DeleteAllItems(g.lvRes);
    if (!r->valid) {
        char t[300]; snprintf(t, sizeof t, "Resource check could not run: %s", r->message);
        SetWindowTextA(g.stRes, t);
        return;
    }
    char t[900], tot[160] = "", av[160] = "", seq[400] = "";
    for (int j = 0; j < NRES; j++) {
        size_t l = strlen(tot); snprintf(tot + l, sizeof tot - l, "%s%s %d", j ? "   " : "", RES_NAMES[j], r->total[j]);
        l = strlen(av);         snprintf(av + l, sizeof av - l, "%s%s %d", j ? "   " : "", RES_NAMES[j], r->avail[j]);
    }
    for (int k = 0; k < r->seq_len; k++) {
        size_t l = strlen(seq); snprintf(seq + l, sizeof seq - l, k ? " -> %s" : "%s", s->r[r->seq[k]].p.name);
    }
    snprintf(t, sizeof t,
        "Total units:  %s\nAvailable now:  %s     (table cells show units held / maximum need)\nDeadlock check (Banker's algorithm): %s%s%s",
        tot, av, r->message, r->safe ? "   Safe sequence: " : "", r->safe ? seq : "");
    SetWindowTextA(g.stRes, t);

    int order[MAX_PROC] = {0};
    for (int k = 0; k < r->seq_len; k++) order[r->seq[k]] = k + 1;
    for (int i = 0; i < s->n; i++) {
        const Process *p = &s->r[i].p; char b2[64];
        int row = lv_add(g.lvRes, p->name, p->pid);
        for (int j = 0; j < NRES; j++) { snprintf(b2, sizeof b2, "%d / %d", p->held[j], p->max[j]); lv_set(g.lvRes, row, 1 + j, b2); }
        size_t l = 0; b2[0] = 0;
        for (int j = 0; j < NRES; j++) l += snprintf(b2 + l, sizeof b2 - l, "%s%c:%d", j ? " " : "", RES_NAMES[j][0], r->need[i][j]);
        lv_set(g.lvRes, row, 5, b2);
        if (order[i]) seti(g.lvRes, row, 6, order[i]); else lv_set(g.lvRes, row, 6, "-");
    }
    int row = lv_add(g.lvRes, "TOTAL units", 0);
    for (int j = 0; j < NRES; j++) seti(g.lvRes, row, 1 + j, r->total[j]);
    row = lv_add(g.lvRes, "AVAILABLE", 0);
    for (int j = 0; j < NRES; j++) seti(g.lvRes, row, 1 + j, r->avail[j]);
}

void views_show_bundle(const RunBundle *b)
{
    show_sched(b); show_mem(b); show_res(b);
}

/* ---------------------------------------------------------------- comparison and history */
void views_fill_compare(const SchedResult *rs, int n, int auto_idx, const char *auto_reason, int nprocs)
{
    char t[500];
    snprintf(t, sizeof t, "All algorithms run on the %d ticked processes.   %s", nprocs, auto_reason);
    SetWindowTextA(g.stCmp, t);
    int bw = 0, bt = 0, br = 0;
    for (int i = 1; i < n; i++) {
        if (rs[i].avg_wt  < rs[bw].avg_wt  - 1e-9) bw = i;
        if (rs[i].avg_tat < rs[bt].avg_tat - 1e-9) bt = i;
        if (rs[i].avg_rt  < rs[br].avg_rt  - 1e-9) br = i;
    }
    ListView_DeleteAllItems(g.lvCmp);
    for (int i = 0; i < n; i++) {
        char nm[80], note[160] = ""; const SchedResult *r = &rs[i];
        if (r->algo == ALGO_RR) snprintf(nm, sizeof nm, "Round Robin (q=%d)", r->quantum); else snprintf(nm, sizeof nm, "%s", sched_algo_short(r->algo));
        int row = lv_add(g.lvCmp, nm, 0);
        setf(g.lvCmp, row, 1, r->avg_wt); setf(g.lvCmp, row, 2, r->avg_tat); setf(g.lvCmp, row, 3, r->avg_rt);
        char b2[16]; snprintf(b2, sizeof b2, "%.1f", r->cpu_util); lv_set(g.lvCmp, row, 4, b2);
        seti(g.lvCmp, row, 5, r->ctx_switches); seti(g.lvCmp, row, 6, r->total_time);
        if (r->avg_wt  < rs[bw].avg_wt  + 1e-9) strcat(note, "lowest WT; ");
        if (r->avg_tat < rs[bt].avg_tat + 1e-9) strcat(note, "lowest TAT; ");
        if (r->avg_rt  < rs[br].avg_rt  + 1e-9) strcat(note, "best response; ");
        if (i == auto_idx) strcat(note, "<< Auto's choice");
        lv_set(g.lvCmp, row, 7, note);
    }
}

void views_refresh_history(void)
{
    static RunSummary list[500];
    int n = db_list_runs(list, 500);
    ListView_DeleteAllItems(g.lvHist);
    for (int i = 0; i < n; i++) {
        char b2[32];
        snprintf(b2, sizeof b2, "%d", list[i].run_id);
        int row = lv_add(g.lvHist, b2, list[i].run_id);
        lv_set(g.lvHist, row, 1, list[i].created); lv_set(g.lvHist, row, 2, list[i].algo);
        seti(g.lvHist, row, 3, list[i].nproc); setf(g.lvHist, row, 4, list[i].avg_wt); setf(g.lvHist, row, 5, list[i].avg_tat);
    }
}

int views_selected_run_id(void)
{
    int i = ListView_GetNextItem(g.lvHist, -1, LVNI_SELECTED);
    if (i < 0) return 0;
    LVITEMA it; memset(&it, 0, sizeof it); it.mask = LVIF_PARAM; it.iItem = i;
    SendMessageA(g.lvHist, LVM_GETITEMA, 0, (LPARAM)&it);
    return (int)it.lParam;
}
