/* Main window: layout, controls and the actions behind every button. */
#include <stdarg.h>
#include "gui.h"
#include "core/scheduler.h"
#include "core/memory.h"
#include "core/resources.h"
#include "core/util.h"
#include "db/database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

App g;

/* ---------------------------------------------------------------- small helpers */
static HWND Mk(HWND parent, const char *cls, const char *text, DWORD style, DWORD ex, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                             (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)g.font, TRUE);
    return c;
}
static HWND Label(HWND parent, const char *text, int x, int y, int w, int h) { return Mk(parent, "STATIC", text, 0, 0, x, y, w, h, 0); }

static void Status(const char *fmt, ...)
{
    char b[300]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    SendMessageA(g.status, SB_SETTEXTA, 0, (LPARAM)b);
}

static void Warn(const char *msg) { MessageBoxA(g.hwnd, msg, "Virtual OS Simulator", MB_ICONWARNING); }

static void ShowPage(int sel)
{
    for (int i = 0; i < NPAGES; i++) ShowWindow(g.page[i], i == sel ? SW_SHOW : SW_HIDE);
    TabCtrl_SetCurSel(g.tab, sel);
}

static void UpdateSelCount(void)
{
    int n = 0, total = ListView_GetItemCount(g.lvProc);
    for (int i = 0; i < total; i++) if (ListView_GetCheckState(g.lvProc, i)) n++;
    char b[96]; snprintf(b, sizeof b, "Processes - tick the ones to simulate   (%d of %d ticked)", n, total);
    SetWindowTextA(g.stProc, b);
}

static void ReloadProcesses(void)
{
    int n = db_load_processes(g.procs, MAX_PROC);
    g.nprocs = n < 0 ? 0 : n;
    views_refresh_processes();
    UpdateSelCount();
}

/* ---------------------------------------------------------------- settings */
static void LoadSettings(void)
{
    char v[200];
    db_get_setting("algo", v, sizeof v, "0");   SendMessageA(g.cbAlgo, CB_SETCURSEL, atoi(v), 0);
    db_get_setting("mem", v, sizeof v, "0");    SendMessageA(g.cbMem, CB_SETCURSEL, atoi(v), 0);
    db_get_setting("quantum", v, sizeof v, "2");              SetWindowTextA(g.edQuantum, v);
    db_get_setting("blocks", v, sizeof v, "100,500,200,300,600"); SetWindowTextA(g.edBlocks, v);
    db_get_setting("totals", v, sizeof v, "10,5,7,6");        SetWindowTextA(g.edTotals, v);
    db_get_setting("save", v, sizeof v, "1");   SendMessageA(g.chkSave, BM_SETCHECK, atoi(v) ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void SaveSettings(void)
{
    char v[200];
    snprintf(v, sizeof v, "%d", (int)SendMessageA(g.cbAlgo, CB_GETCURSEL, 0, 0)); db_set_setting("algo", v);
    snprintf(v, sizeof v, "%d", (int)SendMessageA(g.cbMem, CB_GETCURSEL, 0, 0));  db_set_setting("mem", v);
    GetWindowTextA(g.edQuantum, v, sizeof v); db_set_setting("quantum", v);
    GetWindowTextA(g.edBlocks, v, sizeof v);  db_set_setting("blocks", v);
    GetWindowTextA(g.edTotals, v, sizeof v);  db_set_setting("totals", v);
    db_set_setting("save", SendMessageA(g.chkSave, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0");
}

/* ---------------------------------------------------------------- sample data */
static void AddSamples(void)
{
    static const struct { const char *n; int a, b, pr, mem; int mx[NRES], hd[NRES]; } S[5] = {
        { "P1", 0, 6, 2, 212, {7,5,3,2}, {0,1,0,1} },
        { "P2", 1, 8, 1, 417, {3,2,2,1}, {2,0,0,0} },
        { "P3", 2, 7, 4, 112, {9,0,2,3}, {3,0,2,1} },
        { "P4", 3, 3, 3, 426, {2,2,2,2}, {2,1,1,1} },
        { "P5", 4, 4, 2,  90, {4,3,3,2}, {0,0,2,1} },
    };
    for (int i = 0; i < 5 && g.nprocs + i < MAX_PROC; i++) {
        Process p; memset(&p, 0, sizeof p);
        snprintf(p.name, sizeof p.name, "%s", S[i].n);
        p.arrival = S[i].a; p.burst = S[i].b; p.priority = S[i].pr; p.mem_kb = S[i].mem;
        memcpy(p.max, S[i].mx, sizeof p.max); memcpy(p.held, S[i].hd, sizeof p.held);
        if (db_save_process(&p) != 0) { Warn(db_error()); break; }
    }
    ReloadProcesses();
}

/* ---------------------------------------------------------------- process buttons */
static void OnAdd(void)
{
    if (g.nprocs >= MAX_PROC) { Warn("The list is full (64 processes)."); return; }
    Process p; memset(&p, 0, sizeof p);
    snprintf(p.name, sizeof p.name, "P%d", g.nprocs + 1);
    p.burst = 5; p.priority = 1; p.mem_kb = 100;
    if (!dialog_process(g.hwnd, &p, 0)) return;
    if (db_save_process(&p) != 0) { Warn(db_error()); return; }
    ReloadProcesses(); Status("Added process %s.", p.name);
}

static void OnEdit(void)
{
    int i = ListView_GetNextItem(g.lvProc, -1, LVNI_SELECTED);
    if (i < 0 || i >= g.nprocs) { Warn("Click a process in the list first, then press Edit."); return; }
    Process p = g.procs[i];
    if (!dialog_process(g.hwnd, &p, 1)) return;
    if (db_save_process(&p) != 0) { Warn(db_error()); return; }
    ReloadProcesses(); Status("Updated process %s.", p.name);
}

static void OnDelete(void)
{
    int i = ListView_GetNextItem(g.lvProc, -1, LVNI_SELECTED);
    if (i < 0 || i >= g.nprocs) { Warn("Click a process in the list first, then press Delete."); return; }
    char m[120]; snprintf(m, sizeof m, "Delete process %s? Saved runs that used it are kept.", g.procs[i].name);
    if (MessageBoxA(g.hwnd, m, "Delete process", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (db_delete_process(g.procs[i].pid) != 0) { Warn(db_error()); return; }
    ReloadProcesses(); Status("Process deleted.");
}

static void TickAll(int on)
{
    g.populating = 1;
    for (int i = 0; i < ListView_GetItemCount(g.lvProc); i++) ListView_SetCheckState(g.lvProc, i, on);
    g.populating = 0;
    UpdateSelCount();
}

/* ---------------------------------------------------------------- reading the settings panel */
typedef struct { int algo, quantum, mem, nblocks, blocks[MAX_BLOCKS], totals[NRES]; char blocks_txt[200], totals_txt[100]; } Inputs;

static int ReadInputs(Inputs *in)
{
    char b[200];
    in->algo = (int)SendMessageA(g.cbAlgo, CB_GETCURSEL, 0, 0);
    in->mem  = (int)SendMessageA(g.cbMem, CB_GETCURSEL, 0, 0);
    if (in->algo < 0) in->algo = 0;
    if (in->mem < 0) in->mem = 0;
    GetWindowTextA(g.edQuantum, b, sizeof b);
    char *end; long q = strtol(b, &end, 10);
    if (b[0] == 0 || *end != 0 || q < 1 || q > 100) { Warn("Time quantum must be a whole number from 1 to 100."); SetFocus(g.edQuantum); return 0; }
    in->quantum = (int)q;
    GetWindowTextA(g.edBlocks, in->blocks_txt, sizeof in->blocks_txt);
    in->nblocks = mem_parse_blocks(in->blocks_txt, in->blocks, MAX_BLOCKS);
    if (in->nblocks < 1) { Warn("Memory blocks must be whole numbers separated by commas, e.g. 100, 500, 200, 300, 600."); SetFocus(g.edBlocks); return 0; }
    GetWindowTextA(g.edTotals, in->totals_txt, sizeof in->totals_txt);
    if (res_parse_totals(in->totals_txt, in->totals) != 0) { Warn("Resource totals need exactly four numbers (Printer, Scanner, Disk, Network), e.g. 10, 5, 7, 6."); SetFocus(g.edTotals); return 0; }
    return 1;
}

/* ---------------------------------------------------------------- Run Simulation */
static void OnRun(void)
{
    Process sel[MAX_PROC]; int n = views_get_selected(sel);
    if (n == 0) { Warn("Tick at least one process in the list first."); return; }
    Inputs in; if (!ReadInputs(&in)) return;

    RunBundle *b = (RunBundle *)calloc(1, sizeof *b);
    if (!b) { Warn("Out of memory."); return; }

    SchedAlgo algo = (SchedAlgo)in.algo; int q = in.quantum; char reason[256];
    if (algo == ALGO_AUTO) algo = sched_auto_select(sel, n, &q, reason, sizeof reason);
    else snprintf(reason, sizeof reason, "Manually selected. %s", sched_algo_info(algo));
    if (sched_run(algo, sel, n, q, &b->sched) != 0) { Warn("The scheduler could not run with these settings."); free(b); return; }
    snprintf(b->sched.reason, sizeof b->sched.reason, "%s", reason);

    if (mem_run((MemStrategy)in.mem, in.blocks, in.nblocks, sel, n, &b->mem) != 0) { Warn("Memory allocation could not run."); free(b); return; }
    if (res_run(in.totals, sel, n, &b->res) != 0) {
        char m[300]; snprintf(m, sizeof m, "%s\n\nChange the resource totals or edit the process and run again.", b->res.message);
        Warn(m); free(b); return;
    }
    time_t now = time(NULL); strftime(b->created, sizeof b->created, "%Y-%m-%d %H:%M:%S", localtime(&now));

    int saved = 0;
    if (SendMessageA(g.chkSave, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        if (db_save_run(b, in.blocks_txt, in.totals_txt) < 0) { char m[300]; snprintf(m, sizeof m, "The run worked but could not be saved:\n%s", db_error()); Warn(m); }
        else saved = 1;
    }
    free(g.cur); g.cur = b;
    views_show_bundle(b);
    if (saved) views_refresh_history();
    SaveSettings();
    ShowPage(0);
    if (saved) Status("%s finished: avg WT %.2f, avg TAT %.2f. Saved as run #%d.", b->sched.algo_name, b->sched.avg_wt, b->sched.avg_tat, b->run_id);
    else       Status("%s finished: avg WT %.2f, avg TAT %.2f. (Not saved.)", b->sched.algo_name, b->sched.avg_wt, b->sched.avg_tat);
}

/* ---------------------------------------------------------------- Compare All */
static void OnCompare(void)
{
    Process sel[MAX_PROC]; int n = views_get_selected(sel);
    if (n == 0) { Warn("Tick at least one process in the list first."); return; }
    Inputs in; if (!ReadInputs(&in)) return;
    static const SchedAlgo list[6] = { ALGO_FCFS, ALGO_SJF, ALGO_SRTF, ALGO_PRIO_NP, ALGO_PRIO_P, ALGO_RR };
    SchedResult *rs = (SchedResult *)calloc(6, sizeof *rs);
    if (!rs) { Warn("Out of memory."); return; }
    for (int i = 0; i < 6; i++) if (sched_run(list[i], sel, n, in.quantum, &rs[i]) != 0) { Warn("Comparison failed."); free(rs); return; }
    int q = in.quantum; char reason[256];
    SchedAlgo pick = sched_auto_select(sel, n, &q, reason, sizeof reason);
    int auto_idx = 0;
    for (int i = 0; i < 6; i++) if (list[i] == pick) auto_idx = i;
    views_fill_compare(rs, 6, auto_idx, reason, n);
    free(rs);
    ShowPage(3);
    Status("Compared 6 algorithms on %d processes.", n);
}

/* ---------------------------------------------------------------- history */
static void OnHistView(void)
{
    int id = views_selected_run_id();
    if (!id) { Warn("Click a saved run in the list first."); return; }
    RunBundle *b = (RunBundle *)calloc(1, sizeof *b); char mb[200], rt[100];
    if (!b) return;
    if (db_load_run(id, b, mb, sizeof mb, rt, sizeof rt) != 0) { Warn(db_error()); free(b); return; }
    free(g.cur); g.cur = b;
    views_show_bundle(b);
    ShowPage(0);
    Status("Showing saved run #%d from %s.", id, b->created);
}

static void OnHistDelete(void)
{
    int id = views_selected_run_id();
    if (!id) { Warn("Click a saved run in the list first."); return; }
    char m[100]; snprintf(m, sizeof m, "Delete saved run #%d?", id);
    if (MessageBoxA(g.hwnd, m, "Delete run", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (db_delete_run(id) != 0) { Warn(db_error()); return; }
    if (g.cur && g.cur->run_id == id) g.cur->run_id = 0;
    views_refresh_history();
    Status("Run #%d deleted.", id);
}

/* ---------------------------------------------------------------- layout */
static void Layout(void)
{
    RECT rc; GetClientRect(g.hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    SendMessageA(g.status, WM_SIZE, 0, 0);
    RECT sr; GetWindowRect(g.status, &sr);
    int top = 100, bottom = H - (sr.bottom - sr.top) - 6;
    if (bottom < top + 200) bottom = top + 200;

    MoveWindow(g.grp, 8, 2, W - 16, 92, TRUE);

    int LW = 520;
    MoveWindow(g.stProc, 8, top, LW, 18, TRUE);
    MoveWindow(g.lvProc, 8, top + 20, LW, bottom - top - 20 - 36, TRUE);
    HWND bt[6] = { g.btnAdd, g.btnEdit, g.btnDel, g.btnAll, g.btnNone, g.btnSample };
    for (int i = 0; i < 6; i++) MoveWindow(bt[i], 8 + i * 87, bottom - 30, 83, 28, TRUE);

    int tx = 8 + LW + 8, tw = W - tx - 8, th = bottom - top;
    MoveWindow(g.tab, tx, top, tw, th, TRUE);
    RECT d; GetClientRect(g.tab, &d); TabCtrl_AdjustRect(g.tab, FALSE, &d);
    int px = d.left, py = d.top, pw = d.right - d.left, ph = d.bottom - d.top;
    for (int i = 0; i < NPAGES; i++) MoveWindow(g.page[i], px, py, pw, ph, TRUE);

    MoveWindow(g.stSched, 4, 2, pw - 8, 76, TRUE);
    MoveWindow(g.gantt, 4, 80, pw - 8, 92, TRUE);
    MoveWindow(g.lvResult, 4, 176, pw - 8, ph - 180, TRUE);
    MoveWindow(g.stMem, 4, 2, pw - 8, 60, TRUE);
    MoveWindow(g.memmap, 4, 64, pw - 8, 100, TRUE);
    MoveWindow(g.lvMem, 4, 168, pw - 8, ph - 172, TRUE);
    MoveWindow(g.stRes, 4, 2, pw - 8, 62, TRUE);
    MoveWindow(g.lvRes, 4, 68, pw - 8, ph - 72, TRUE);
    MoveWindow(g.stCmp, 4, 2, pw - 8, 38, TRUE);
    MoveWindow(g.lvCmp, 4, 44, pw - 8, ph - 48, TRUE);
    MoveWindow(g.btnHView, 4, 4, 130, 26, TRUE);
    MoveWindow(g.btnHDel, 142, 4, 110, 26, TRUE);
    MoveWindow(g.btnHRef, 260, 4, 90, 26, TRUE);
    MoveWindow(g.lvHist, 4, 36, pw - 8, ph - 40, TRUE);
}

/* ---------------------------------------------------------------- creation */
static void CreateAll(HWND hwnd)
{
    g.hwnd = hwnd;
    g.status = CreateWindowExA(0, "msctls_statusbar32", "Ready", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                               0, 0, 0, 0, hwnd, (HMENU)ID_STATUS, GetModuleHandle(NULL), NULL);
    SendMessageA(g.status, WM_SETFONT, (WPARAM)g.font, TRUE);

    g.grp = Mk(hwnd, "BUTTON", " Simulation settings ", BS_GROUPBOX, 0, 8, 2, 1000, 92, 0);
    Label(hwnd, "Scheduling algorithm", 22, 28, 130, 18);
    g.cbAlgo = Mk(hwnd, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 150, 24, 230, 240, ID_CB_ALGO);
    SendMessageA(g.cbAlgo, CB_ADDSTRING, 0, (LPARAM)"Auto (let the program decide)");
    for (int a = ALGO_FCFS; a < ALGO_COUNT; a++) SendMessageA(g.cbAlgo, CB_ADDSTRING, 0, (LPARAM)sched_algo_name((SchedAlgo)a));
    SendMessageA(g.cbAlgo, CB_SETCURSEL, 0, 0);
    Label(hwnd, "Time quantum", 396, 28, 80, 18);
    g.edQuantum = Mk(hwnd, "EDIT", "2", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 480, 24, 40, 22, ID_ED_Q);
    Label(hwnd, "Memory strategy", 544, 28, 100, 18);
    g.cbMem = Mk(hwnd, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 646, 24, 130, 200, ID_CB_MEM);
    SendMessageA(g.cbMem, CB_ADDSTRING, 0, (LPARAM)"Auto (best of three)");
    for (int s = MEM_FIRST; s < MEM_COUNT; s++) SendMessageA(g.cbMem, CB_ADDSTRING, 0, (LPARAM)mem_strategy_name((MemStrategy)s));
    SendMessageA(g.cbMem, CB_SETCURSEL, 0, 0);
    Label(hwnd, "Memory blocks (KB)", 794, 28, 112, 18);
    g.edBlocks = Mk(hwnd, "EDIT", "100,500,200,300,600", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 908, 24, 250, 22, ID_ED_BLOCKS);

    Label(hwnd, "Resource totals (Printer, Scanner, Disk, Network)", 22, 62, 300, 18);
    g.edTotals = Mk(hwnd, "EDIT", "10,5,7,6", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 326, 58, 120, 22, ID_ED_TOTALS);
    g.chkSave = Mk(hwnd, "BUTTON", "Save every run to the database", WS_TABSTOP | BS_AUTOCHECKBOX, 0, 470, 58, 230, 22, ID_CHK_SAVE);
    SendMessageA(g.chkSave, BM_SETCHECK, BST_CHECKED, 0);
    g.btnRun = Mk(hwnd, "BUTTON", "Run Simulation", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 720, 54, 160, 30, ID_RUN);
    g.btnCompare = Mk(hwnd, "BUTTON", "Compare All Algorithms", WS_TABSTOP | BS_PUSHBUTTON, 0, 892, 54, 190, 30, ID_COMPARE);

    g.stProc = Label(hwnd, "Processes", 8, 100, 520, 18);
    g.lvProc = lv_create(hwnd, ID_LV_PROC, LVS_EX_CHECKBOXES);
    g.btnAdd = Mk(hwnd, "BUTTON", "Add...", WS_TABSTOP, 0, 0, 0, 10, 10, ID_ADD);
    g.btnEdit = Mk(hwnd, "BUTTON", "Edit...", WS_TABSTOP, 0, 0, 0, 10, 10, ID_EDIT);
    g.btnDel = Mk(hwnd, "BUTTON", "Delete", WS_TABSTOP, 0, 0, 0, 10, 10, ID_DEL);
    g.btnAll = Mk(hwnd, "BUTTON", "Tick all", WS_TABSTOP, 0, 0, 0, 10, 10, ID_ALL);
    g.btnNone = Mk(hwnd, "BUTTON", "Tick none", WS_TABSTOP, 0, 0, 0, 10, 10, ID_NONE);
    g.btnSample = Mk(hwnd, "BUTTON", "Add samples", WS_TABSTOP, 0, 0, 0, 10, 10, ID_SAMPLE);

    g.tab = CreateWindowExA(0, "SysTabControl32", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP,
                            0, 0, 100, 100, hwnd, (HMENU)ID_TAB, GetModuleHandle(NULL), NULL);
    SendMessageA(g.tab, WM_SETFONT, (WPARAM)g.font, TRUE);
    static const char *tabs[NPAGES] = { "Scheduling and Gantt chart", "Memory", "Resources", "Compare algorithms", "Saved runs" };
    for (int i = 0; i < NPAGES; i++) {
        TCITEMA ti; memset(&ti, 0, sizeof ti); ti.mask = TCIF_TEXT; ti.pszText = (char *)tabs[i];
        SendMessageA(g.tab, TCM_INSERTITEMA, i, (LPARAM)&ti);
        g.page[i] = CreateWindowExA(0, "VOSPage", "", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | (i == 0 ? WS_VISIBLE : 0),
                                    0, 0, 10, 10, g.tab, NULL, GetModuleHandle(NULL), NULL);   /* pages live inside the tab control */
    }
    g.stSched = Mk(g.page[0], "STATIC", "", 0, 0, 0, 0, 10, 10, 0);
    g.gantt   = CreateWindowExA(WS_EX_CLIENTEDGE, "VOSGantt", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g.page[0], NULL, GetModuleHandle(NULL), NULL);
    g.lvResult = lv_create(g.page[0], ID_LV_RESULT, 0);
    g.stMem   = Mk(g.page[1], "STATIC", "", 0, 0, 0, 0, 10, 10, 0);
    g.memmap  = CreateWindowExA(WS_EX_CLIENTEDGE, "VOSMemMap", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g.page[1], NULL, GetModuleHandle(NULL), NULL);
    g.lvMem   = lv_create(g.page[1], ID_LV_MEM, 0);
    g.stRes   = Mk(g.page[2], "STATIC", "", 0, 0, 0, 0, 10, 10, 0);
    g.lvRes   = lv_create(g.page[2], ID_LV_RES, 0);
    g.stCmp   = Mk(g.page[3], "STATIC", "", 0, 0, 0, 0, 10, 10, 0);
    g.lvCmp   = lv_create(g.page[3], ID_LV_CMP, 0);
    g.btnHView = Mk(g.page[4], "BUTTON", "View selected run", WS_TABSTOP, 0, 0, 0, 10, 10, ID_H_VIEW);
    g.btnHDel  = Mk(g.page[4], "BUTTON", "Delete run", WS_TABSTOP, 0, 0, 0, 10, 10, ID_H_DEL);
    g.btnHRef  = Mk(g.page[4], "BUTTON", "Refresh", WS_TABSTOP, 0, 0, 0, 10, 10, ID_H_REF);
    g.lvHist   = lv_create(g.page[4], ID_LV_HIST, 0);
    views_init_columns();
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        CreateAll(hwnd);
        return 0;
    case WM_SIZE:
        if (g.tab && w != SIZE_MINIMIZED) Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)l; mm->ptMinTrackSize.x = 1180; mm->ptMinTrackSize.y = 640; return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_RUN: OnRun(); break;
        case ID_COMPARE: OnCompare(); break;
        case ID_ADD: OnAdd(); break;
        case ID_EDIT: OnEdit(); break;
        case ID_DEL: OnDelete(); break;
        case ID_ALL: TickAll(1); break;
        case ID_NONE: TickAll(0); break;
        case ID_SAMPLE: AddSamples(); Status("Sample processes added."); break;
        case ID_H_VIEW: OnHistView(); break;
        case ID_H_DEL: OnHistDelete(); break;
        case ID_H_REF: views_refresh_history(); Status("History refreshed."); break;
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)l;
        if (nm->idFrom == ID_TAB && nm->code == TCN_SELCHANGE) ShowPage(TabCtrl_GetCurSel(g.tab));
        else if (nm->idFrom == ID_LV_PROC) {
            if (nm->code == LVN_ITEMCHANGED && !g.populating) UpdateSelCount();
            else if (nm->code == NM_DBLCLK) OnEdit();
        } else if (nm->idFrom == ID_LV_HIST && nm->code == NM_DBLCLK) OnHistView();
        return 0;
    }
    case WM_DESTROY:
        SaveSettings();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, m, w, l);
}

int gui_run(HINSTANCE hi, int show)
{
    widgets_register(hi);
    NONCLIENTMETRICSA ncm; memset(&ncm, 0, sizeof ncm); ncm.cbSize = sizeof ncm;
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0)) g.font = CreateFontIndirectA(&ncm.lfMessageFont);
    if (!g.font) g.font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = MainProc; wc.hInstance = hi; wc.lpszClassName = "VOSMain";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(0, "VOSMain", "Virtual OS Simulator - CPU Scheduling, Memory and Resource Manager",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1300, 780,
                                NULL, NULL, hi, NULL);
    if (!hwnd) return 1;

    LoadSettings();
    ReloadProcesses();
    char seeded[8]; db_get_setting("seeded", seeded, sizeof seeded, "0");
    if (g.nprocs == 0 && strcmp(seeded, "1") != 0) { AddSamples(); db_set_setting("seeded", "1"); }
    views_refresh_history();
    views_show_empty();
    Layout();
    Status("Ready. Tick processes, pick an algorithm (or Auto) and press Run Simulation. Everything you run is saved to virtual_os.db.");
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    free(g.cur);
    return (int)msg.wParam;
}
