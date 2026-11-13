/* Virtual OS Simulator - Modern Nuklear GUI with Boot Screen, Theming, and Responsive Layout
 * Powered by Nuklear Immediate Mode UI + SDL2
 */
#include "gui_sdl.h"
#include "core/types.h"
#include "core/scheduler.h"
#include "core/memory.h"
#include "core/resources.h"
#include "core/util.h"
#include "db/database.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklear/nuklear.h"
#include "nuklear/nuklear_sdl_renderer.h"

/* ---------------------------------------------------------------- Theme Mode */
typedef enum { THEME_DARK = 0, THEME_LIGHT } GuiTheme;
typedef enum { MODE_BOOT = 0, MODE_DESKTOP } AppMode;

/* 12 Distinct Process Colors for Gantt Chart and Memory Blocks */
static const struct nk_color PID_COLORS[12] = {
    {  59, 125, 216, 255 }, /* Blue */
    { 224, 123,  57, 255 }, /* Orange */
    {  63, 166, 107, 255 }, /* Green */
    { 180,  79, 176, 255 }, /* Purple */
    { 190, 140,  20, 255 }, /* Gold */
    {  42, 161, 176, 255 }, /* Cyan */
    { 196,  71,  58, 255 }, /* Red */
    { 107, 142,  35, 255 }, /* Olive */
    { 122,  92, 199, 255 }, /* Indigo */
    { 204, 102, 153, 255 }, /* Pink */
    {  95, 127, 143, 255 }, /* Slate */
    { 140, 109,  49, 255 }  /* Brown */
};

static struct nk_color get_pid_color(int pid)
{
    return PID_COLORS[((pid % 12) + 12) % 12];
}

/* ---------------------------------------------------------------- Application State */
typedef struct {
    SDL_Window        *win;
    SDL_Renderer      *renderer;
    struct nk_context *ctx;
    struct nk_font    *font;

    int win_w, win_h;
    float dpi_scale;
    AppMode mode;
    Uint32 boot_start_time;

    GuiTheme theme;

    /* Process Library */
    Process procs[MAX_PROC];
    int     ticked[MAX_PROC];
    int     nprocs;
    int     sel_proc_idx;

    /* Settings */
    int     algo_idx;
    char    quantum_str[16];
    int     mem_idx;
    char    blocks_str[128];
    char    totals_str[64];
    nk_bool save_to_db;

    /* Active Tab: 0=Sched/Gantt, 1=Memory, 2=Resources, 3=Compare, 4=History */
    int active_tab;

    /* Current simulation bundle */
    RunBundle *cur;

    /* Comparison data */
    SchedResult cmp_rs[6];
    int         has_cmp;
    int         cmp_auto_idx;
    char        cmp_auto_reason[256];

    /* History data */
    RunSummary history[64];
    int        nhistory;
    int        sel_hist_idx;

    /* Add/Edit Modal Dialog State */
    int     dlg_open;
    int     dlg_is_edit;
    Process dlg_proc;
    char    dlg_name[NAME_LEN];
    char    dlg_arr[16], dlg_burst[16], dlg_prio[16], dlg_mem[16];
    char    dlg_max[4][16], dlg_held[4][16];
    char    dlg_error[128];

    /* Delete Confirmation Dialog */
    int     confirm_dlg_open;
    int     confirm_is_proc; /* 1 = delete proc, 0 = delete run */
    int     confirm_id;
    char    confirm_msg[128];

    /* Status Bar Message */
    char   status_msg[256];
    Uint32 status_time;
} State;

static State G;

/* ---------------------------------------------------------------- Theming */
static void apply_theme_dark(struct nk_context *ctx)
{
    struct nk_color table[NK_COLOR_COUNT];
    table[NK_COLOR_TEXT]                    = nk_rgba(240, 244, 248, 255);
    table[NK_COLOR_WINDOW]                  = nk_rgba(24, 28, 36, 255);
    table[NK_COLOR_HEADER]                  = nk_rgba(34, 40, 52, 255);
    table[NK_COLOR_BORDER]                  = nk_rgba(50, 60, 78, 255);
    table[NK_COLOR_BUTTON]                  = nk_rgba(40, 48, 64, 255);
    table[NK_COLOR_BUTTON_HOVER]            = nk_rgba(56, 68, 90, 255);
    table[NK_COLOR_BUTTON_ACTIVE]           = nk_rgba(37, 99, 235, 255);
    table[NK_COLOR_TOGGLE]                  = nk_rgba(40, 48, 64, 255);
    table[NK_COLOR_TOGGLE_HOVER]            = nk_rgba(56, 68, 90, 255);
    table[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba(34, 197, 94, 255);
    table[NK_COLOR_SELECT]                  = nk_rgba(37, 99, 235, 255);
    table[NK_COLOR_SELECT_ACTIVE]           = nk_rgba(59, 130, 246, 255);
    table[NK_COLOR_SLIDER]                  = nk_rgba(40, 48, 64, 255);
    table[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(37, 99, 235, 255);
    table[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba(59, 130, 246, 255);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba(29, 78, 216, 255);
    table[NK_COLOR_PROPERTY]                = nk_rgba(40, 48, 64, 255);
    table[NK_COLOR_EDIT]                    = nk_rgba(16, 20, 26, 255);
    table[NK_COLOR_EDIT_CURSOR]             = nk_rgba(240, 244, 248, 255);
    table[NK_COLOR_COMBO]                   = nk_rgba(32, 38, 50, 255);
    table[NK_COLOR_CHART]                   = nk_rgba(40, 48, 64, 255);
    table[NK_COLOR_CHART_COLOR]             = nk_rgba(37, 99, 235, 255);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT]   = nk_rgba(239, 68, 68, 255);
    table[NK_COLOR_SCROLLBAR]               = nk_rgba(20, 24, 32, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba(48, 58, 76, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba(64, 78, 102, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(37, 99, 235, 255);
    table[NK_COLOR_TAB_HEADER]              = nk_rgba(32, 38, 50, 255);
    nk_style_from_table(ctx, table);
}

static void apply_theme_light(struct nk_context *ctx)
{
    struct nk_color table[NK_COLOR_COUNT];
    table[NK_COLOR_TEXT]                    = nk_rgba(15, 23, 42, 255);
    table[NK_COLOR_WINDOW]                  = nk_rgba(241, 245, 249, 255);
    table[NK_COLOR_HEADER]                  = nk_rgba(226, 232, 240, 255);
    table[NK_COLOR_BORDER]                  = nk_rgba(203, 213, 225, 255);
    table[NK_COLOR_BUTTON]                  = nk_rgba(226, 232, 240, 255);
    table[NK_COLOR_BUTTON_HOVER]            = nk_rgba(203, 213, 225, 255);
    table[NK_COLOR_BUTTON_ACTIVE]           = nk_rgba(37, 99, 235, 255);
    table[NK_COLOR_TOGGLE]                  = nk_rgba(226, 232, 240, 255);
    table[NK_COLOR_TOGGLE_HOVER]            = nk_rgba(203, 213, 225, 255);
    table[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba(34, 197, 94, 255);
    table[NK_COLOR_SELECT]                  = nk_rgba(219, 234, 254, 255);
    table[NK_COLOR_SELECT_ACTIVE]           = nk_rgba(37, 99, 235, 255);
    table[NK_COLOR_SLIDER]                  = nk_rgba(226, 232, 240, 255);
    table[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(37, 99, 235, 255);
    table[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba(59, 130, 246, 255);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba(29, 78, 216, 255);
    table[NK_COLOR_PROPERTY]                = nk_rgba(226, 232, 240, 255);
    table[NK_COLOR_EDIT]                    = nk_rgba(255, 255, 255, 255);
    table[NK_COLOR_EDIT_CURSOR]             = nk_rgba(15, 23, 42, 255);
    table[NK_COLOR_COMBO]                   = nk_rgba(255, 255, 255, 255);
    table[NK_COLOR_CHART]                   = nk_rgba(226, 232, 240, 255);
    table[NK_COLOR_CHART_COLOR]             = nk_rgba(37, 99, 235, 255);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT]   = nk_rgba(239, 68, 68, 255);
    table[NK_COLOR_SCROLLBAR]               = nk_rgba(241, 245, 249, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba(203, 213, 225, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba(148, 163, 184, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(37, 99, 235, 255);
    table[NK_COLOR_TAB_HEADER]              = nk_rgba(226, 232, 240, 255);
    nk_style_from_table(ctx, table);
}

static void apply_theme(struct nk_context *ctx, GuiTheme t)
{
    if (t == THEME_DARK) apply_theme_dark(ctx);
    else apply_theme_light(ctx);
}

/* ---------------------------------------------------------------- State Helpers */
static void set_status(const char *msg)
{
    snprintf(G.status_msg, sizeof(G.status_msg), "%s", msg);
    G.status_time = SDL_GetTicks();
}

static void refresh_processes(void)
{
    int n = db_load_processes(G.procs, MAX_PROC);
    G.nprocs = (n < 0) ? 0 : n;
    for (int i = 0; i < G.nprocs; i++) {
        G.ticked[i] = 1; /* default to ticked */
    }
}

static void refresh_history(void)
{
    G.nhistory = db_list_runs(G.history, 64);
    if (G.nhistory < 0) G.nhistory = 0;
    if (G.sel_hist_idx >= G.nhistory) G.sel_hist_idx = G.nhistory - 1;
}

static void load_settings(void)
{
    char buf[64];
    db_get_setting("algo", buf, sizeof(buf), "0");
    G.algo_idx = atoi(buf);
    db_get_setting("quantum", G.quantum_str, sizeof(G.quantum_str), "2");
    db_get_setting("mem", buf, sizeof(buf), "0");
    G.mem_idx = atoi(buf);
    db_get_setting("blocks", G.blocks_str, sizeof(G.blocks_str), "100,500,200,300,600");
    db_get_setting("totals", G.totals_str, sizeof(G.totals_str), "10,5,7,6");
    db_get_setting("save", buf, sizeof(buf), "1");
    G.save_to_db = atoi(buf);

    db_get_setting("theme", buf, sizeof(buf), "dark");
    G.theme = (strcmp(buf, "light") == 0) ? THEME_LIGHT : THEME_DARK;
}

static void save_settings(void)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", G.algo_idx);
    db_set_setting("algo", buf);
    db_set_setting("quantum", G.quantum_str);
    snprintf(buf, sizeof(buf), "%d", G.mem_idx);
    db_set_setting("mem", buf);
    db_set_setting("blocks", G.blocks_str);
    db_set_setting("totals", G.totals_str);
    snprintf(buf, sizeof(buf), "%d", G.save_to_db);
    db_set_setting("save", buf);
    db_set_setting("theme", (G.theme == THEME_LIGHT) ? "light" : "dark");
}

static void add_samples(void)
{
    static const struct { const char *n; int a, b, pr, mem; int mx[NRES], hd[NRES]; } S[5] = {
        { "P1", 0, 6, 2, 212, {7,5,3,2}, {0,1,0,1} },
        { "P2", 1, 8, 1, 417, {3,2,2,1}, {2,0,0,0} },
        { "P3", 2, 7, 4, 112, {9,0,2,3}, {3,0,2,1} },
        { "P4", 3, 3, 3, 426, {2,2,2,2}, {2,1,1,1} },
        { "P5", 4, 4, 2,  90, {4,3,3,2}, {0,0,2,1} },
    };
    for (int i = 0; i < 5 && G.nprocs + i < MAX_PROC; i++) {
        Process p; memset(&p, 0, sizeof(p));
        snprintf(p.name, sizeof(p.name), "%s", S[i].n);
        p.arrival = S[i].a; p.burst = S[i].b; p.priority = S[i].pr; p.mem_kb = S[i].mem;
        memcpy(p.max, S[i].mx, sizeof(p.max));
        memcpy(p.held, S[i].hd, sizeof(p.held));
        db_save_process(&p);
    }
    refresh_processes();
    set_status("Loaded sample processes P1 through P5.");
}

/* ---------------------------------------------------------------- Simulation Actions */
static void run_simulation(void)
{
    Process sel[MAX_PROC];
    int nsel = 0;
    for (int i = 0; i < G.nprocs; i++) {
        if (G.ticked[i]) sel[nsel++] = G.procs[i];
    }
    if (nsel == 0) {
        set_status("Warning: Please tick at least one process to simulate.");
        return;
    }

    int q = atoi(G.quantum_str);
    if (q < 1) q = 2;

    int blocks[MAX_BLOCKS];
    int nblocks = mem_parse_blocks(G.blocks_str, blocks, MAX_BLOCKS);
    if (nblocks < 1) {
        set_status("Error: Invalid memory blocks format (e.g. 100,500,200).");
        return;
    }

    int totals[NRES];
    if (res_parse_totals(G.totals_str, totals) != 0) {
        set_status("Error: Resource totals must have 4 numbers (e.g. 10,5,7,6).");
        return;
    }

    RunBundle *b = (RunBundle *)calloc(1, sizeof(*b));
    if (!b) return;

    SchedAlgo algo = (SchedAlgo)G.algo_idx;
    char reason[256] = {0};
    if (algo == ALGO_AUTO) {
        algo = sched_auto_select(sel, nsel, &q, reason, sizeof(reason));
    } else {
        snprintf(reason, sizeof(reason), "Manually selected: %s", sched_algo_info(algo));
    }

    if (sched_run(algo, sel, nsel, q, &b->sched) != 0) {
        set_status("Error: Scheduler execution failed.");
        free(b);
        return;
    }
    snprintf(b->sched.reason, sizeof(b->sched.reason), "%s", reason);

    if (mem_run((MemStrategy)G.mem_idx, blocks, nblocks, sel, nsel, &b->mem) != 0) {
        set_status("Error: Memory allocation failed.");
        free(b);
        return;
    }

    if (res_run(totals, sel, nsel, &b->res) != 0) {
        char err[300];
        snprintf(err, sizeof(err), "Resource error: %s", b->res.message);
        set_status(err);
        free(b);
        return;
    }

    time_t now = time(NULL);
    strftime(b->created, sizeof(b->created), "%Y-%m-%d %H:%M:%S", localtime(&now));

    if (G.save_to_db) {
        int run_id = db_save_run(b, G.blocks_str, G.totals_str);
        if (run_id >= 0) {
            b->run_id = run_id;
            refresh_history();
            char msg[128];
            snprintf(msg, sizeof(msg), "%s complete: Avg WT %.2f, Avg TAT %.2f. Saved as run #%d.",
                     b->sched.algo_name, b->sched.avg_wt, b->sched.avg_tat, run_id);
            set_status(msg);
        } else {
            set_status("Simulation complete, but saving to database failed.");
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s complete: Avg WT %.2f, Avg TAT %.2f. (Not saved).",
                 b->sched.algo_name, b->sched.avg_wt, b->sched.avg_tat);
        set_status(msg);
    }

    free(G.cur);
    G.cur = b;
    save_settings();
    G.active_tab = 0;
}

static void run_compare(void)
{
    Process sel[MAX_PROC];
    int nsel = 0;
    for (int i = 0; i < G.nprocs; i++) {
        if (G.ticked[i]) sel[nsel++] = G.procs[i];
    }
    if (nsel == 0) {
        set_status("Warning: Please tick at least one process to compare.");
        return;
    }

    int q = atoi(G.quantum_str);
    if (q < 1) q = 2;

    static const SchedAlgo list[6] = {
        ALGO_FCFS, ALGO_SJF, ALGO_SRTF, ALGO_PRIO_NP, ALGO_PRIO_P, ALGO_RR
    };
    for (int i = 0; i < 6; i++) {
        if (sched_run(list[i], sel, nsel, q, &G.cmp_rs[i]) != 0) {
            set_status("Comparison failed.");
            return;
        }
    }

    SchedAlgo pick = sched_auto_select(sel, nsel, &q, G.cmp_auto_reason, sizeof(G.cmp_auto_reason));
    G.cmp_auto_idx = 0;
    for (int i = 0; i < 6; i++) {
        if (list[i] == pick) G.cmp_auto_idx = i;
    }
    G.has_cmp = 1;
    G.active_tab = 3;
    set_status("Comparison finished for 6 CPU algorithms.");
}

/* ---------------------------------------------------------------- Add/Edit Modal Helpers */
static void open_dlg_add(void)
{
    if (G.nprocs >= MAX_PROC) {
        set_status("Error: Process list full (64 max).");
        return;
    }
    G.dlg_open = 1;
    G.dlg_is_edit = 0;
    memset(&G.dlg_proc, 0, sizeof(G.dlg_proc));
    snprintf(G.dlg_name, sizeof(G.dlg_name), "P%d", G.nprocs + 1);
    snprintf(G.dlg_arr, sizeof(G.dlg_arr), "0");
    snprintf(G.dlg_burst, sizeof(G.dlg_burst), "5");
    snprintf(G.dlg_prio, sizeof(G.dlg_prio), "1");
    snprintf(G.dlg_mem, sizeof(G.dlg_mem), "100");
    for (int k = 0; k < 4; k++) {
        snprintf(G.dlg_max[k], sizeof(G.dlg_max[k]), "2");
        snprintf(G.dlg_held[k], sizeof(G.dlg_held[k]), "0");
    }
    G.dlg_error[0] = '\0';
}

static void open_dlg_edit(void)
{
    if (G.sel_proc_idx < 0 || G.sel_proc_idx >= G.nprocs) {
        set_status("Click a process in the list first, then click Edit.");
        return;
    }
    G.dlg_open = 1;
    G.dlg_is_edit = 1;
    G.dlg_proc = G.procs[G.sel_proc_idx];
    snprintf(G.dlg_name, sizeof(G.dlg_name), "%s", G.dlg_proc.name);
    snprintf(G.dlg_arr, sizeof(G.dlg_arr), "%d", G.dlg_proc.arrival);
    snprintf(G.dlg_burst, sizeof(G.dlg_burst), "%d", G.dlg_proc.burst);
    snprintf(G.dlg_prio, sizeof(G.dlg_prio), "%d", G.dlg_proc.priority);
    snprintf(G.dlg_mem, sizeof(G.dlg_mem), "%d", G.dlg_proc.mem_kb);
    for (int k = 0; k < 4; k++) {
        snprintf(G.dlg_max[k], sizeof(G.dlg_max[k]), "%d", G.dlg_proc.max[k]);
        snprintf(G.dlg_held[k], sizeof(G.dlg_held[k]), "%d", G.dlg_proc.held[k]);
    }
    G.dlg_error[0] = '\0';
}

static void save_dlg(void)
{
    Process p = G.dlg_proc;
    if (strlen(G.dlg_name) == 0) {
        snprintf(G.dlg_error, sizeof(G.dlg_error), "Process name cannot be empty.");
        return;
    }
    snprintf(p.name, sizeof(p.name), "%s", G.dlg_name);
    p.arrival = atoi(G.dlg_arr);
    p.burst = atoi(G.dlg_burst);
    if (p.burst < 1) p.burst = 1;
    p.priority = atoi(G.dlg_prio);
    if (p.priority < 1) p.priority = 1;
    p.mem_kb = atoi(G.dlg_mem);
    if (p.mem_kb < 1) p.mem_kb = 1;

    for (int k = 0; k < 4; k++) {
        p.max[k] = atoi(G.dlg_max[k]);
        p.held[k] = atoi(G.dlg_held[k]);
        if (p.held[k] > p.max[k]) {
            snprintf(G.dlg_error, sizeof(G.dlg_error), "Held %s cannot exceed Max %s.", RES_NAMES[k], RES_NAMES[k]);
            return;
        }
    }

    if (db_save_process(&p) != 0) {
        snprintf(G.dlg_error, sizeof(G.dlg_error), "%s", db_error());
        return;
    }

    refresh_processes();
    G.dlg_open = 0;
    set_status(G.dlg_is_edit ? "Process updated successfully." : "New process added.");
}

/* ---------------------------------------------------------------- Boot Screen */
static void render_boot_screen(struct nk_context *ctx)
{
    Uint32 elapsed = SDL_GetTicks() - G.boot_start_time;
    float progress = (float)elapsed / 1800.0f; /* 1.8 second boot */
    if (progress > 1.0f) progress = 1.0f;

    struct nk_style_window original_style = ctx->style.window;
    ctx->style.window.background = nk_rgba(11, 14, 20, 255);
    ctx->style.window.fixed_background = nk_style_item_color(nk_rgba(11, 14, 20, 255));

    if (nk_begin(ctx, "BootScreen", nk_rect(0, 0, (float)G.win_w, (float)G.win_h),
                 NK_WINDOW_NO_SCROLLBAR)) {

        nk_layout_row_dynamic(ctx, 40, 1);
        nk_spacing(ctx, 1);

        /* OS Title Header */
        nk_layout_row_dynamic(ctx, 36, 1);
        nk_label_colored(ctx, "V I R T U A L   O S   S I M U L A T O R", NK_TEXT_CENTERED, nk_rgb(96, 165, 250));
        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label_colored(ctx, "Cross-Platform High-DPI Real-Time Kernel Subsystem v2.5", NK_TEXT_CENTERED, nk_rgb(148, 163, 184));

        nk_layout_row_dynamic(ctx, 20, 1);
        nk_spacing(ctx, 1);

        /* Kernel messages based on elapsed time */
        nk_layout_row_dynamic(ctx, 20, 1);
        if (elapsed > 100) nk_label_colored(ctx, "  [ 0.08 s ] [  OK  ] Core: Initializing CPU execution registers and microcode...", NK_TEXT_LEFT, nk_rgb(34, 197, 94));
        if (elapsed > 300) nk_label_colored(ctx, "  [ 0.22 s ] [  OK  ] Memory: Probing RAM blocks [100 KB, 500 KB, 200 KB, 300 KB, 600 KB]...", NK_TEXT_LEFT, nk_rgb(34, 197, 94));
        if (elapsed > 550) nk_label_colored(ctx, "  [ 0.45 s ] [  OK  ] Storage: Mounting SQLite virtual filesystem (virtual_os.db)...", NK_TEXT_LEFT, nk_rgb(34, 197, 94));
        if (elapsed > 800) nk_label_colored(ctx, "  [ 0.70 s ] [  OK  ] Devices: Initializing I/O controllers [Printer, Scanner, Disk, Network]...", NK_TEXT_LEFT, nk_rgb(34, 197, 94));
        if (elapsed > 1050) nk_label_colored(ctx, "  [ 0.98 s ] [  OK  ] Scheduler: Registering multi-queue policies [FCFS, SJF, SRTF, PRIO, RR]...", NK_TEXT_LEFT, nk_rgb(34, 197, 94));
        if (elapsed > 1300) nk_label_colored(ctx, "  [ 1.25 s ] [  OK  ] Security: Starting Dijkstra's Banker's algorithm safety monitor...", NK_TEXT_LEFT, nk_rgb(34, 197, 94));
        if (elapsed > 1500) nk_label_colored(ctx, "  [ 1.50 s ] [  OK  ] Desktop: Initializing Nuklear hardware GUI subsystem...", NK_TEXT_LEFT, nk_rgb(34, 197, 94));
        if (elapsed > 1700) nk_label_colored(ctx, "  [ 1.75 s ] [  OK  ] Boot Complete: Transitioning to Virtual OS Simulator Desktop...", NK_TEXT_LEFT, nk_rgb(59, 130, 246));

        /* Progress Bar */
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_spacing(ctx, 1);
        nk_layout_row_dynamic(ctx, 24, 1);
        size_t cur_prog = (size_t)(progress * 100.0f);
        nk_progress(ctx, &cur_prog, 100, NK_FIXED);

        char pstr[32];
        snprintf(pstr, sizeof(pstr), "Loading OS: %d%%", (int)(progress * 100.0f));
        nk_label_colored(ctx, pstr, NK_TEXT_CENTERED, nk_rgb(203, 213, 225));

        nk_layout_row_dynamic(ctx, 24, 1);
        nk_label_colored(ctx, "Press any key or click to skip boot sequence", NK_TEXT_CENTERED, nk_rgb(100, 116, 139));
    }
    nk_end(ctx);
    ctx->style.window = original_style;

    if (elapsed >= 1800) {
        G.mode = MODE_DESKTOP;
        apply_theme(ctx, G.theme);
    }
}

/* ---------------------------------------------------------------- Custom Widget: Gantt Chart */
static void render_custom_gantt(struct nk_context *ctx, const SchedResult *s)
{
    struct nk_rect bounds;
    enum nk_widget_layout_states state = nk_widget(&bounds, ctx);
    if (state == NK_WIDGET_INVALID) return;

    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    struct nk_color bg_col = (G.theme == THEME_DARK) ? nk_rgb(16, 20, 28) : nk_rgb(241, 245, 249);
    struct nk_color border_col = (G.theme == THEME_DARK) ? nk_rgb(50, 60, 78) : nk_rgb(203, 213, 225);

    nk_fill_rect(canvas, bounds, 4.0f, bg_col);
    nk_stroke_rect(canvas, bounds, 4.0f, 1.0f, border_col);

    if (s->ng == 0 || s->total_time <= 0) {
        struct nk_color text_col = (G.theme == THEME_DARK) ? nk_rgb(100, 116, 139) : nk_rgb(148, 163, 184);
        nk_draw_text(canvas, bounds, "Run a simulation to generate the visual Gantt Chart.", 52, &G.font->handle, bg_col, text_col);
        return;
    }

    float margin = 16.0f;
    float chart_w = bounds.w - margin * 2.0f;
    float bar_y = bounds.y + 12.0f;
    float bar_h = 42.0f;
    float scale = chart_w / (float)s->total_time;

    int hover_seg = -1;
    float mx = ctx->input.mouse.pos.x;
    float my = ctx->input.mouse.pos.y;

    for (int i = 0; i < s->ng; i++) {
        const GanttSeg *seg = &s->g[i];
        float x1 = bounds.x + margin + seg->start * scale;
        float x2 = bounds.x + margin + seg->end * scale;
        if (x2 <= x1) x2 = x1 + 2.0f;
        float seg_w = x2 - x1;

        struct nk_rect r = nk_rect(x1, bar_y, seg_w, bar_h);
        if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) {
            hover_seg = i;
        }

        if (seg->pid < 0) {
            /* Idle block */
            struct nk_color idle_col = (G.theme == THEME_DARK) ? nk_rgb(38, 44, 56) : nk_rgb(203, 213, 225);
            nk_fill_rect(canvas, r, 2.0f, idle_col);
            nk_stroke_rect(canvas, r, 2.0f, 1.0f, border_col);
            if (seg_w >= 30.0f) {
                struct nk_color text_col = (G.theme == THEME_DARK) ? nk_rgb(148, 163, 184) : nk_rgb(100, 116, 139);
                nk_draw_text(canvas, r, "Idle", 4, &G.font->handle, idle_col, text_col);
            }
        } else {
            /* Process block */
            struct nk_color c = get_pid_color(seg->pid);
            if (hover_seg == i) {
                c.r = (nk_byte)(c.r + 30 > 255 ? 255 : c.r + 30);
                c.g = (nk_byte)(c.g + 30 > 255 ? 255 : c.g + 30);
                c.b = (nk_byte)(c.b + 30 > 255 ? 255 : c.b + 30);
            }
            nk_fill_rect(canvas, r, 2.0f, c);
            nk_stroke_rect(canvas, r, 2.0f, 1.0f, nk_rgb(255, 255, 255));

            const char *name = "?";
            for (int k = 0; k < s->n; k++) {
                if (s->r[k].p.pid == seg->pid) { name = s->r[k].p.name; break; }
            }
            if (seg_w >= 22.0f) {
                nk_draw_text(canvas, r, name, (int)strlen(name), &G.font->handle, c, nk_rgb(255, 255, 255));
            }
        }

        /* Time tick number */
        char tstr[16];
        snprintf(tstr, sizeof(tstr), "%d", seg->start);
        struct nk_rect tr = nk_rect(x1 - 10.0f, bar_y + bar_h + 4.0f, 30.0f, 16.0f);
        struct nk_color tick_col = (G.theme == THEME_DARK) ? nk_rgb(148, 163, 184) : nk_rgb(71, 85, 105);
        nk_draw_text(canvas, tr, tstr, (int)strlen(tstr), &G.font->handle, bg_col, tick_col);
    }

    /* Final end tick mark */
    char tstr[16];
    snprintf(tstr, sizeof(tstr), "%d", s->total_time);
    struct nk_rect tr = nk_rect(bounds.x + margin + chart_w - 10.0f, bar_y + bar_h + 4.0f, 30.0f, 16.0f);
    struct nk_color tick_col = (G.theme == THEME_DARK) ? nk_rgb(148, 163, 184) : nk_rgb(71, 85, 105);
    nk_draw_text(canvas, tr, tstr, (int)strlen(tstr), &G.font->handle, bg_col, tick_col);

    /* Hover Tooltip */
    if (hover_seg >= 0) {
        const GanttSeg *seg = &s->g[hover_seg];
        char tip[128];
        if (seg->pid < 0) {
            snprintf(tip, sizeof(tip), "CPU Idle: %d -> %d (%d time units)", seg->start, seg->end, seg->end - seg->start);
        } else {
            const char *name = "?";
            for (int k = 0; k < s->n; k++) {
                if (s->r[k].p.pid == seg->pid) { name = s->r[k].p.name; break; }
            }
            snprintf(tip, sizeof(tip), "%s: Start %d, End %d (Burst %d)", name, seg->start, seg->end, seg->end - seg->start);
        }
        float tip_w = (float)strlen(tip) * 9.0f + 16.0f;
        float tip_x = mx + 12.0f;
        float tip_y = my - 28.0f;
        if (tip_x + tip_w > bounds.x + bounds.w) tip_x = bounds.x + bounds.w - tip_w - 10.0f;

        struct nk_rect tip_r = nk_rect(tip_x, tip_y, tip_w, 24.0f);
        struct nk_color tip_bg = (G.theme == THEME_DARK) ? nk_rgb(30, 36, 48) : nk_rgb(255, 255, 255);
        struct nk_color tip_fg = (G.theme == THEME_DARK) ? nk_rgb(240, 244, 248) : nk_rgb(15, 23, 42);
        nk_fill_rect(canvas, tip_r, 4.0f, tip_bg);
        nk_stroke_rect(canvas, tip_r, 4.0f, 1.0f, nk_rgb(37, 99, 235));
        nk_draw_text(canvas, nk_rect(tip_x + 8.0f, tip_y + 3.0f, tip_w - 16.0f, 18.0f),
                     tip, (int)strlen(tip), &G.font->handle, tip_bg, tip_fg);
    }
}

/* ---------------------------------------------------------------- Custom Widget: Memory Map */
static void render_custom_memmap(struct nk_context *ctx, const MemResult *m)
{
    struct nk_rect bounds;
    enum nk_widget_layout_states state = nk_widget(&bounds, ctx);
    if (state == NK_WIDGET_INVALID) return;

    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    struct nk_color bg_col = (G.theme == THEME_DARK) ? nk_rgb(16, 20, 28) : nk_rgb(241, 245, 249);
    struct nk_color border_col = (G.theme == THEME_DARK) ? nk_rgb(50, 60, 78) : nk_rgb(203, 213, 225);

    nk_fill_rect(canvas, bounds, 4.0f, bg_col);
    nk_stroke_rect(canvas, bounds, 4.0f, 1.0f, border_col);

    if (m->nblocks == 0 || m->total_mem <= 0) return;

    float margin = 16.0f;
    float gap = 8.0f;
    float usable_w = bounds.w - margin * 2.0f - (m->nblocks - 1) * gap;
    float cur_x = bounds.x + margin;
    float block_y = bounds.y + 12.0f;
    float block_h = 44.0f;

    for (int i = 0; i < m->nblocks; i++) {
        float bw = usable_w * ((float)m->blocks[i] / (float)m->total_mem);
        if (bw < 48.0f) bw = 48.0f;

        struct nk_rect br = nk_rect(cur_x, block_y, bw, block_h);

        /* Find process allocated in this block */
        const MemAlloc *alloc = NULL;
        for (int k = 0; k < m->n; k++) {
            if (m->a[k].allocated && m->a[k].block == i + 1) {
                alloc = &m->a[k]; break;
            }
        }

        if (alloc) {
            float used_w = bw * ((float)alloc->size / (float)alloc->block_size);
            struct nk_rect used_r = nk_rect(cur_x, block_y, used_w, block_h);
            struct nk_color pc = get_pid_color(alloc->pid);
            nk_fill_rect(canvas, used_r, 2.0f, pc);

            if (bw > used_w) {
                /* Internal fragmentation */
                struct nk_rect frag_r = nk_rect(cur_x + used_w, block_y, bw - used_w, block_h);
                struct nk_color frag_c = (G.theme == THEME_DARK) ? nk_rgb(60, 48, 30) : nk_rgb(254, 243, 199);
                nk_fill_rect(canvas, frag_r, 2.0f, frag_c);
                nk_stroke_rect(canvas, frag_r, 2.0f, 1.0f, nk_rgb(245, 158, 11));
            }
            nk_stroke_rect(canvas, br, 2.0f, 1.0f, border_col);
            nk_draw_text(canvas, used_r, alloc->name, (int)strlen(alloc->name), &G.font->handle, pc, nk_rgb(255, 255, 255));
        } else {
            /* Free block */
            struct nk_color free_col = (G.theme == THEME_DARK) ? nk_rgb(32, 38, 50) : nk_rgb(226, 232, 240);
            nk_fill_rect(canvas, br, 2.0f, free_col);
            nk_stroke_rect(canvas, br, 2.0f, 1.0f, border_col);
            struct nk_color free_txt = (G.theme == THEME_DARK) ? nk_rgb(100, 116, 139) : nk_rgb(148, 163, 184);
            nk_draw_text(canvas, br, "FREE", 4, &G.font->handle, free_col, free_txt);
        }

        /* Label below block */
        char blabel[32];
        snprintf(blabel, sizeof(blabel), "B%d:%dK", i + 1, m->blocks[i]);
        struct nk_rect lr = nk_rect(cur_x, block_y + block_h + 4.0f, bw, 18.0f);
        struct nk_color lcol = (G.theme == THEME_DARK) ? nk_rgb(148, 163, 184) : nk_rgb(71, 85, 105);
        nk_draw_text(canvas, lr, blabel, (int)strlen(blabel), &G.font->handle, bg_col, lcol);

        cur_x += bw + gap;
    }
}

/* ---------------------------------------------------------------- Nuklear Windows */
static void render_top_bar(struct nk_context *ctx)
{
    float bar_w = (float)G.win_w;
    float bar_h = 92.0f;

    if (nk_begin(ctx, "TopBar", nk_rect(0, 0, bar_w, bar_h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) {

        /* Row 1: Configurations */
        nk_layout_row_begin(ctx, NK_STATIC, 30, 9);
        {
            /* Algorithm combo */
            static const char *algo_items[] = {
                "Auto Select", "FCFS", "SJF (Non-P)", "SRTF (Preempt)",
                "Priority (Non-P)", "Priority (Preempt)", "Round Robin"
            };
            nk_layout_row_push(ctx, 150);
            G.algo_idx = nk_combo(ctx, algo_items, 7, G.algo_idx, 26, nk_vec2(160, 200));

            /* Quantum edit */
            nk_layout_row_push(ctx, 50);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.quantum_str, sizeof(G.quantum_str), nk_filter_decimal);

            /* Memory combo */
            static const char *mem_items[] = { "Auto Select", "First Fit", "Best Fit", "Worst Fit" };
            nk_layout_row_push(ctx, 130);
            G.mem_idx = nk_combo(ctx, mem_items, 4, G.mem_idx, 26, nk_vec2(140, 160));

            /* Memory Blocks string */
            nk_layout_row_push(ctx, 190);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.blocks_str, sizeof(G.blocks_str), nk_filter_ascii);

            /* Resource Totals string */
            nk_layout_row_push(ctx, 110);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.totals_str, sizeof(G.totals_str), nk_filter_ascii);

            /* Save checkbox */
            nk_layout_row_push(ctx, 95);
            nk_checkbox_label(ctx, "Save Run", &G.save_to_db);

            /* Theme Toggle Button */
            nk_layout_row_push(ctx, 125);
            if (nk_button_label(ctx, (G.theme == THEME_DARK) ? "Light Mode" : "Dark Mode")) {
                G.theme = (G.theme == THEME_DARK) ? THEME_LIGHT : THEME_DARK;
                apply_theme(ctx, G.theme);
                save_settings();
            }

            /* Simulation Action Buttons */
            nk_layout_row_push(ctx, 140);
            if (nk_button_label(ctx, "Run Simulation")) {
                run_simulation();
            }

            nk_layout_row_push(ctx, 120);
            if (nk_button_label(ctx, "Compare All")) {
                run_compare();
            }
        }
        nk_layout_row_end(ctx);

        /* Row 2: Sub-labels */
        nk_layout_row_begin(ctx, NK_STATIC, 18, 5);
        {
            nk_layout_row_push(ctx, 150); nk_label(ctx, "CPU Algorithm", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 50);  nk_label(ctx, "Quant", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 130); nk_label(ctx, "Mem Strategy", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 190); nk_label(ctx, "Memory Blocks (KB)", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 110); nk_label(ctx, "Resource Totals", NK_TEXT_LEFT);
        }
        nk_layout_row_end(ctx);
    }
    nk_end(ctx);
}

static void render_process_panel(struct nk_context *ctx)
{
    float px = 0;
    float py = 92.0f;
    float pw = 430.0f;
    float ph = (float)G.win_h - py - 28.0f;

    if (nk_begin(ctx, "Processes", nk_rect(px, py, pw, ph), NK_WINDOW_BORDER)) {
        /* Title and Count */
        int ticked_n = 0;
        for (int i = 0; i < G.nprocs; i++) if (G.ticked[i]) ticked_n++;
        char title[128];
        snprintf(title, sizeof(title), "Process Library (%d of %d ticked)", ticked_n, G.nprocs);
        nk_layout_row_dynamic(ctx, 24, 1);
        nk_label_colored(ctx, title, NK_TEXT_LEFT, (G.theme == THEME_DARK) ? nk_rgb(96, 165, 250) : nk_rgb(37, 99, 235));

        /* Action Buttons Row */
        nk_layout_row_dynamic(ctx, 28, 6);
        if (nk_button_label(ctx, "+ Add"))   open_dlg_add();
        if (nk_button_label(ctx, "Edit"))    open_dlg_edit();
        if (nk_button_label(ctx, "Del")) {
            if (G.sel_proc_idx >= 0 && G.sel_proc_idx < G.nprocs) {
                G.confirm_dlg_open = 1;
                G.confirm_is_proc = 1;
                G.confirm_id = G.procs[G.sel_proc_idx].pid;
                snprintf(G.confirm_msg, sizeof(G.confirm_msg), "Delete process %s?", G.procs[G.sel_proc_idx].name);
            }
        }
        if (nk_button_label(ctx, "All"))     { for (int i = 0; i < G.nprocs; i++) G.ticked[i] = 1; }
        if (nk_button_label(ctx, "None"))    { for (int i = 0; i < G.nprocs; i++) G.ticked[i] = 0; }
        if (nk_button_label(ctx, "Sample"))  add_samples();

        /* Process List Table Header */
        nk_layout_row_begin(ctx, NK_STATIC, 22, 7);
        {
            nk_layout_row_push(ctx, 28);  nk_label(ctx, "[*]", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 60);  nk_label(ctx, "Name", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 36);  nk_label(ctx, "Arr", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 42);  nk_label(ctx, "Burst", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 36);  nk_label(ctx, "Pri", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 55);  nk_label(ctx, "Mem", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 120); nk_label(ctx, "Held / Max (P,S,D,N)", NK_TEXT_LEFT);
        }
        nk_layout_row_end(ctx);

        /* Process Rows */
        for (int i = 0; i < G.nprocs; i++) {
            Process *p = &G.procs[i];
            nk_layout_row_begin(ctx, NK_STATIC, 26, 7);
            {
                nk_layout_row_push(ctx, 28);
                nk_checkbox_label(ctx, "", &G.ticked[i]);

                nk_layout_row_push(ctx, 60);
                if (nk_select_label(ctx, p->name, NK_TEXT_LEFT, (G.sel_proc_idx == i))) {
                    G.sel_proc_idx = i;
                }

                char buf[32];
                nk_layout_row_push(ctx, 36);
                snprintf(buf, sizeof(buf), "%d", p->arrival);
                nk_label(ctx, buf, NK_TEXT_LEFT);

                nk_layout_row_push(ctx, 42);
                snprintf(buf, sizeof(buf), "%d", p->burst);
                nk_label(ctx, buf, NK_TEXT_LEFT);

                nk_layout_row_push(ctx, 36);
                snprintf(buf, sizeof(buf), "%d", p->priority);
                nk_label(ctx, buf, NK_TEXT_LEFT);

                nk_layout_row_push(ctx, 55);
                snprintf(buf, sizeof(buf), "%dK", p->mem_kb);
                nk_label(ctx, buf, NK_TEXT_LEFT);

                nk_layout_row_push(ctx, 120);
                snprintf(buf, sizeof(buf), "%d/%d %d/%d %d/%d %d/%d",
                         p->held[0], p->max[0], p->held[1], p->max[1],
                         p->held[2], p->max[2], p->held[3], p->max[3]);
                nk_label_colored(ctx, buf, NK_TEXT_LEFT, (G.theme == THEME_DARK) ? nk_rgb(148, 163, 184) : nk_rgb(100, 116, 139));
            }
            nk_layout_row_end(ctx);
        }
    }
    nk_end(ctx);
}

static void render_results_panel(struct nk_context *ctx)
{
    float rx = 430.0f;
    float ry = 92.0f;
    float rw = (float)G.win_w - rx;
    float rh = (float)G.win_h - ry - 28.0f;

    if (nk_begin(ctx, "ResultsPanel", nk_rect(rx, ry, rw, rh), NK_WINDOW_BORDER)) {
        /* Tab Bar */
        nk_layout_row_dynamic(ctx, 32, 5);
        if (nk_button_label(ctx, "1. Sched & Gantt")) G.active_tab = 0;
        if (nk_button_label(ctx, "2. Memory Map"))    G.active_tab = 1;
        if (nk_button_label(ctx, "3. Resources"))     G.active_tab = 2;
        if (nk_button_label(ctx, "4. Compare"))       G.active_tab = 3;
        if (nk_button_label(ctx, "5. Saved Runs"))    G.active_tab = 4;

        nk_layout_row_dynamic(ctx, 8, 1);
        nk_spacing(ctx, 1);

        /* Tab 0: CPU Scheduling & Gantt */
        if (G.active_tab == 0) {
            if (!G.cur) {
                nk_layout_row_dynamic(ctx, 40, 1);
                nk_label(ctx, "No simulation results yet. Select processes and click 'Run Simulation'.", NK_TEXT_CENTERED);
            } else {
                const SchedResult *s = &G.cur->sched;
                /* Metrics Card */
                nk_layout_row_dynamic(ctx, 24, 3);
                char mbuf[128];
                snprintf(mbuf, sizeof(mbuf), "Avg Waiting Time (WT): %.2f", s->avg_wt);
                nk_label_colored(ctx, mbuf, NK_TEXT_LEFT, nk_rgb(34, 197, 94));
                snprintf(mbuf, sizeof(mbuf), "Avg Turnaround (TAT): %.2f", s->avg_tat);
                nk_label(ctx, mbuf, NK_TEXT_LEFT);
                snprintf(mbuf, sizeof(mbuf), "Avg Response (RT): %.2f", s->avg_rt);
                nk_label(ctx, mbuf, NK_TEXT_LEFT);

                nk_layout_row_dynamic(ctx, 22, 4);
                snprintf(mbuf, sizeof(mbuf), "CPU Util: %.1f%%", s->cpu_util); nk_label(ctx, mbuf, NK_TEXT_LEFT);
                snprintf(mbuf, sizeof(mbuf), "Throughput: %.3f/u", s->throughput); nk_label(ctx, mbuf, NK_TEXT_LEFT);
                snprintf(mbuf, sizeof(mbuf), "Context Switches: %d", s->ctx_switches); nk_label(ctx, mbuf, NK_TEXT_LEFT);
                snprintf(mbuf, sizeof(mbuf), "Total Time: %d", s->total_time); nk_label(ctx, mbuf, NK_TEXT_LEFT);

                if (s->reason[0]) {
                    nk_layout_row_dynamic(ctx, 24, 1);
                    snprintf(mbuf, sizeof(mbuf), "Auto Decision: %s", s->reason);
                    nk_label_colored(ctx, mbuf, NK_TEXT_LEFT, nk_rgb(245, 158, 11));
                }

                /* Gantt Chart Widget */
                nk_layout_row_dynamic(ctx, 84, 1);
                render_custom_gantt(ctx, s);

                /* Table Header */
                nk_layout_row_dynamic(ctx, 10, 1);
                nk_spacing(ctx, 1);

                nk_layout_row_begin(ctx, NK_STATIC, 24, 10);
                {
                    nk_layout_row_push(ctx, 35);  nk_label(ctx, "PID", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 60);  nk_label(ctx, "Name", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 45);  nk_label(ctx, "Arr", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 50);  nk_label(ctx, "Burst", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 40);  nk_label(ctx, "Pri", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 55);  nk_label(ctx, "Finish", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 50);  nk_label(ctx, "TAT", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 50);  nk_label(ctx, "WT", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 50);  nk_label(ctx, "RT", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 120); nk_label(ctx, "Memory Block", NK_TEXT_LEFT);
                }
                nk_layout_row_end(ctx);

                /* Process Results */
                for (int i = 0; i < s->n; i++) {
                    const ProcResult *pr = &s->r[i];
                    nk_layout_row_begin(ctx, NK_STATIC, 24, 10);
                    {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%d", pr->p.pid); nk_layout_row_push(ctx, 35); nk_label(ctx, buf, NK_TEXT_LEFT);
                        nk_layout_row_push(ctx, 60); nk_label(ctx, pr->p.name, NK_TEXT_LEFT);
                        snprintf(buf, sizeof(buf), "%d", pr->p.arrival); nk_layout_row_push(ctx, 45); nk_label(ctx, buf, NK_TEXT_LEFT);
                        snprintf(buf, sizeof(buf), "%d", pr->p.burst); nk_layout_row_push(ctx, 50); nk_label(ctx, buf, NK_TEXT_LEFT);
                        snprintf(buf, sizeof(buf), "%d", pr->p.priority); nk_layout_row_push(ctx, 40); nk_label(ctx, buf, NK_TEXT_LEFT);
                        snprintf(buf, sizeof(buf), "%d", pr->completion); nk_layout_row_push(ctx, 55); nk_label(ctx, buf, NK_TEXT_LEFT);
                        snprintf(buf, sizeof(buf), "%d", pr->tat); nk_layout_row_push(ctx, 50); nk_label(ctx, buf, NK_TEXT_LEFT);
                        snprintf(buf, sizeof(buf), "%d", pr->wt); nk_layout_row_push(ctx, 50); nk_label_colored(ctx, buf, NK_TEXT_LEFT, nk_rgb(34, 197, 94));
                        snprintf(buf, sizeof(buf), "%d", pr->rt); nk_layout_row_push(ctx, 50); nk_label(ctx, buf, NK_TEXT_LEFT);

                        int blk = 0;
                        for (int m = 0; m < G.cur->mem.n; m++) {
                            if (G.cur->mem.a[m].pid == pr->p.pid && G.cur->mem.a[m].allocated) {
                                blk = G.cur->mem.a[m].block; break;
                            }
                        }
                        if (blk > 0) snprintf(buf, sizeof(buf), "Block B%d", blk);
                        else snprintf(buf, sizeof(buf), "Unplaced");
                        nk_layout_row_push(ctx, 120); nk_label(ctx, buf, NK_TEXT_LEFT);
                    }
                    nk_layout_row_end(ctx);
                }
            }
        }

        /* Tab 1: Memory Map */
        else if (G.active_tab == 1) {
            if (!G.cur) {
                nk_layout_row_dynamic(ctx, 40, 1);
                nk_label(ctx, "Run a simulation to view memory placement and partition maps.", NK_TEXT_CENTERED);
            } else {
                const MemResult *m = &G.cur->mem;
                nk_layout_row_dynamic(ctx, 24, 4);
                char mbuf[128];
                snprintf(mbuf, sizeof(mbuf), "Strategy: %s", m->strategy_name); nk_label_colored(ctx, mbuf, NK_TEXT_LEFT, nk_rgb(34, 197, 94));
                snprintf(mbuf, sizeof(mbuf), "Total: %d KB", m->total_mem); nk_label(ctx, mbuf, NK_TEXT_LEFT);
                snprintf(mbuf, sizeof(mbuf), "Used: %d KB", m->used); nk_label(ctx, mbuf, NK_TEXT_LEFT);
                snprintf(mbuf, sizeof(mbuf), "Internal Frag: %d KB", m->internal_frag); nk_label_colored(ctx, mbuf, NK_TEXT_LEFT, nk_rgb(245, 158, 11));

                /* Visual Memory Map */
                nk_layout_row_dynamic(ctx, 90, 1);
                render_custom_memmap(ctx, m);

                /* Table */
                nk_layout_row_dynamic(ctx, 10, 1);
                nk_spacing(ctx, 1);

                nk_layout_row_begin(ctx, NK_STATIC, 24, 6);
                {
                    nk_layout_row_push(ctx, 80);  nk_label(ctx, "Process", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 90);  nk_label(ctx, "Needs (KB)", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 70);  nk_label(ctx, "Block", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 110); nk_label(ctx, "Block Size", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 120); nk_label(ctx, "Internal Frag", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 140); nk_label(ctx, "Placement Status", NK_TEXT_LEFT);
                }
                nk_layout_row_end(ctx);

                for (int i = 0; i < m->n; i++) {
                    const MemAlloc *a = &m->a[i];
                    nk_layout_row_begin(ctx, NK_STATIC, 24, 6);
                    {
                        char buf[32];
                        nk_layout_row_push(ctx, 80); nk_label(ctx, a->name, NK_TEXT_LEFT);
                        snprintf(buf, sizeof(buf), "%d KB", a->size); nk_layout_row_push(ctx, 90); nk_label(ctx, buf, NK_TEXT_LEFT);
                        if (a->allocated) {
                            snprintf(buf, sizeof(buf), "B%d", a->block); nk_layout_row_push(ctx, 70); nk_label(ctx, buf, NK_TEXT_LEFT);
                            snprintf(buf, sizeof(buf), "%d KB", a->block_size); nk_layout_row_push(ctx, 110); nk_label(ctx, buf, NK_TEXT_LEFT);
                            snprintf(buf, sizeof(buf), "%d KB wasted", a->frag); nk_layout_row_push(ctx, 120); nk_label_colored(ctx, buf, NK_TEXT_LEFT, nk_rgb(245, 158, 11));
                            nk_layout_row_push(ctx, 140); nk_label_colored(ctx, "Placed", NK_TEXT_LEFT, nk_rgb(34, 197, 94));
                        } else {
                            nk_layout_row_push(ctx, 70); nk_label(ctx, "--", NK_TEXT_LEFT);
                            nk_layout_row_push(ctx, 110); nk_label(ctx, "--", NK_TEXT_LEFT);
                            nk_layout_row_push(ctx, 120); nk_label(ctx, "--", NK_TEXT_LEFT);
                            nk_layout_row_push(ctx, 140); nk_label_colored(ctx, "Out of Memory", NK_TEXT_LEFT, nk_rgb(239, 68, 68));
                        }
                    }
                    nk_layout_row_end(ctx);
                }
            }
        }

        /* Tab 2: Resources & Banker's */
        else if (G.active_tab == 2) {
            if (!G.cur) {
                nk_layout_row_dynamic(ctx, 40, 1);
                nk_label(ctx, "Run a simulation to analyze resource allocation and Banker's safety.", NK_TEXT_CENTERED);
            } else {
                const ResResult *r = &G.cur->res;
                nk_layout_row_dynamic(ctx, 26, 1);
                if (r->safe) {
                    nk_label_colored(ctx, "VERDICT: SYSTEM IS IN A SAFE STATE (No Deadlock)", NK_TEXT_LEFT, nk_rgb(34, 197, 94));
                    char sbuf[256] = "Safe Execution Sequence: ";
                    int len = (int)strlen(sbuf);
                    for (int i = 0; i < r->seq_len; i++) {
                        len += snprintf(sbuf + len, sizeof(sbuf) - len, "%s%s",
                                        G.cur->sched.r[r->seq[i]].p.name,
                                        (i < r->seq_len - 1) ? " -> " : "");
                    }
                    nk_label_colored(ctx, sbuf, NK_TEXT_LEFT, (G.theme == THEME_DARK) ? nk_rgb(240, 244, 248) : nk_rgb(15, 23, 42));
                } else {
                    nk_label_colored(ctx, "VERDICT: SYSTEM IS IN AN UNSAFE STATE (Potential Deadlock)", NK_TEXT_LEFT, nk_rgb(239, 68, 68));
                    nk_label_colored(ctx, r->message, NK_TEXT_LEFT, (G.theme == THEME_DARK) ? nk_rgb(240, 244, 248) : nk_rgb(15, 23, 42));
                }

                nk_layout_row_dynamic(ctx, 24, 2);
                char tbuf[128];
                snprintf(tbuf, sizeof(tbuf), "System Totals: P:%d  S:%d  D:%d  N:%d", r->total[0], r->total[1], r->total[2], r->total[3]);
                nk_label(ctx, tbuf, NK_TEXT_LEFT);
                snprintf(tbuf, sizeof(tbuf), "Available: P:%d  S:%d  D:%d  N:%d", r->avail[0], r->avail[1], r->avail[2], r->avail[3]);
                nk_label_colored(ctx, tbuf, NK_TEXT_LEFT, nk_rgb(34, 197, 94));

                /* Resource Matrix Table */
                nk_layout_row_dynamic(ctx, 10, 1);
                nk_spacing(ctx, 1);

                nk_layout_row_begin(ctx, NK_STATIC, 24, 7);
                {
                    nk_layout_row_push(ctx, 70);  nk_label(ctx, "Process", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 90);  nk_label(ctx, "Printer(H/M)", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 90);  nk_label(ctx, "Scanner(H/M)", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 90);  nk_label(ctx, "Disk(H/M)", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 90);  nk_label(ctx, "Net(H/M)", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 110); nk_label(ctx, "Still Needs", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 90);  nk_label(ctx, "Safe Order", NK_TEXT_LEFT);
                }
                nk_layout_row_end(ctx);

                for (int i = 0; i < G.cur->sched.n; i++) {
                    const Process *p = &G.cur->sched.r[i].p;
                    nk_layout_row_begin(ctx, NK_STATIC, 24, 7);
                    {
                        char buf[32];
                        nk_layout_row_push(ctx, 70); nk_label(ctx, p->name, NK_TEXT_LEFT);
                        for (int k = 0; k < 4; k++) {
                            snprintf(buf, sizeof(buf), "%d / %d", p->held[k], p->max[k]);
                            nk_layout_row_push(ctx, 90); nk_label(ctx, buf, NK_TEXT_LEFT);
                        }
                        snprintf(buf, sizeof(buf), "%d %d %d %d", r->need[i][0], r->need[i][1], r->need[i][2], r->need[i][3]);
                        nk_layout_row_push(ctx, 110); nk_label_colored(ctx, buf, NK_TEXT_LEFT, nk_rgb(245, 158, 11));

                        int pos = -1;
                        for (int s = 0; s < r->seq_len; s++) if (r->seq[s] == i) { pos = s + 1; break; }
                        if (pos > 0) snprintf(buf, sizeof(buf), "Step #%d", pos);
                        else snprintf(buf, sizeof(buf), "--");
                        nk_layout_row_push(ctx, 90);
                        nk_label_colored(ctx, buf, NK_TEXT_LEFT, (pos > 0) ? nk_rgb(34, 197, 94) : nk_rgb(239, 68, 68));
                    }
                    nk_layout_row_end(ctx);
                }
            }
        }

        /* Tab 3: Compare Algorithms */
        else if (G.active_tab == 3) {
            if (!G.has_cmp) {
                nk_layout_row_dynamic(ctx, 40, 1);
                nk_label(ctx, "Click 'Compare All' to benchmark all 6 CPU algorithms side by side.", NK_TEXT_CENTERED);
            } else {
                nk_layout_row_dynamic(ctx, 26, 1);
                char rbuf[300];
                snprintf(rbuf, sizeof(rbuf), "Auto Recommendation: %s", G.cmp_auto_reason);
                nk_label_colored(ctx, rbuf, NK_TEXT_LEFT, nk_rgb(245, 158, 11));

                nk_layout_row_dynamic(ctx, 10, 1);
                nk_spacing(ctx, 1);

                nk_layout_row_begin(ctx, NK_STATIC, 26, 8);
                {
                    nk_layout_row_push(ctx, 160); nk_label(ctx, "Algorithm", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 65);  nk_label(ctx, "Avg WT", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 65);  nk_label(ctx, "Avg TAT", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 65);  nk_label(ctx, "Avg RT", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 75);  nk_label(ctx, "CPU Util", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 65);  nk_label(ctx, "Ctx Sw", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 70);  nk_label(ctx, "Time", NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 120); nk_label(ctx, "Notes", NK_TEXT_LEFT);
                }
                nk_layout_row_end(ctx);

                for (int i = 0; i < 6; i++) {
                    const SchedResult *sr = &G.cmp_rs[i];
                    int is_auto = (i == G.cmp_auto_idx);
                    nk_layout_row_begin(ctx, NK_STATIC, 26, 8);
                    {
                        char buf[32];
                        nk_layout_row_push(ctx, 160);
                        nk_label_colored(ctx, sr->algo_name, NK_TEXT_LEFT, is_auto ? nk_rgb(245, 158, 11) : ((G.theme == THEME_DARK) ? nk_rgb(240, 244, 248) : nk_rgb(15, 23, 42)));

                        snprintf(buf, sizeof(buf), "%.2f", sr->avg_wt); nk_layout_row_push(ctx, 65);
                        nk_label_colored(ctx, buf, NK_TEXT_LEFT, is_auto ? nk_rgb(34, 197, 94) : ((G.theme == THEME_DARK) ? nk_rgb(203, 213, 225) : nk_rgb(51, 65, 85)));

                        snprintf(buf, sizeof(buf), "%.2f", sr->avg_tat); nk_layout_row_push(ctx, 65); nk_label(ctx, buf, NK_TEXT_LEFT);
                        snprintf(buf, sizeof(buf), "%.2f", sr->avg_rt); nk_layout_row_push(ctx, 65); nk_label(ctx, buf, NK_TEXT_LEFT);
                        snprintf(buf, sizeof(buf), "%.1f%%", sr->cpu_util); nk_layout_row_push(ctx, 75); nk_label(ctx, buf, NK_TEXT_LEFT);
                        snprintf(buf, sizeof(buf), "%d", sr->ctx_switches); nk_layout_row_push(ctx, 65); nk_label(ctx, buf, NK_TEXT_LEFT);
                        snprintf(buf, sizeof(buf), "%d", sr->total_time); nk_layout_row_push(ctx, 70); nk_label(ctx, buf, NK_TEXT_LEFT);

                        nk_layout_row_push(ctx, 120);
                        if (is_auto) nk_label_colored(ctx, "* AUTO RECOMMENDED", NK_TEXT_LEFT, nk_rgb(245, 158, 11));
                        else nk_label(ctx, "", NK_TEXT_LEFT);
                    }
                    nk_layout_row_end(ctx);
                }
            }
        }

        /* Tab 4: Saved Runs History */
        else if (G.active_tab == 4) {
            nk_layout_row_dynamic(ctx, 28, 4);
            if (nk_button_label(ctx, "View Selected Run")) {
                if (G.sel_hist_idx >= 0 && G.sel_hist_idx < G.nhistory) {
                    RunBundle *b = (RunBundle *)calloc(1, sizeof(*b));
                    char mb[200], rt[100];
                    if (db_load_run(G.history[G.sel_hist_idx].run_id, b, mb, sizeof(mb), rt, sizeof(rt)) == 0) {
                        free(G.cur);
                        G.cur = b;
                        G.active_tab = 0;
                        set_status("Loaded saved run from database.");
                    } else {
                        free(b);
                        set_status("Failed to load run from database.");
                    }
                }
            }
            if (nk_button_label(ctx, "Delete Run")) {
                if (G.sel_hist_idx >= 0 && G.sel_hist_idx < G.nhistory) {
                    G.confirm_dlg_open = 1;
                    G.confirm_is_proc = 0;
                    G.confirm_id = G.history[G.sel_hist_idx].run_id;
                    snprintf(G.confirm_msg, sizeof(G.confirm_msg), "Delete saved run #%d?", G.confirm_id);
                }
            }
            if (nk_button_label(ctx, "Refresh")) refresh_history();
            nk_spacing(ctx, 1);

            nk_layout_row_dynamic(ctx, 10, 1);
            nk_spacing(ctx, 1);

            nk_layout_row_begin(ctx, NK_STATIC, 24, 6);
            {
                nk_layout_row_push(ctx, 50);  nk_label(ctx, "Run #", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 160); nk_label(ctx, "Date and Time", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 160); nk_label(ctx, "Algorithm", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 55);  nk_label(ctx, "Procs", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 70);  nk_label(ctx, "Avg WT", NK_TEXT_LEFT);
                nk_layout_row_push(ctx, 70);  nk_label(ctx, "Avg TAT", NK_TEXT_LEFT);
            }
            nk_layout_row_end(ctx);

            for (int i = 0; i < G.nhistory; i++) {
                const RunSummary *run = &G.history[i];
                nk_layout_row_begin(ctx, NK_STATIC, 24, 6);
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "#%d", run->run_id);
                    nk_layout_row_push(ctx, 50);
                    if (nk_select_label(ctx, buf, NK_TEXT_LEFT, (G.sel_hist_idx == i))) {
                        G.sel_hist_idx = i;
                    }
                    nk_layout_row_push(ctx, 160); nk_label(ctx, run->created, NK_TEXT_LEFT);
                    nk_layout_row_push(ctx, 160); nk_label(ctx, run->algo, NK_TEXT_LEFT);
                    snprintf(buf, sizeof(buf), "%d", run->nproc); nk_layout_row_push(ctx, 55); nk_label(ctx, buf, NK_TEXT_LEFT);
                    snprintf(buf, sizeof(buf), "%.2f", run->avg_wt); nk_layout_row_push(ctx, 70); nk_label_colored(ctx, buf, NK_TEXT_LEFT, nk_rgb(34, 197, 94));
                    snprintf(buf, sizeof(buf), "%.2f", run->avg_tat); nk_layout_row_push(ctx, 70); nk_label(ctx, buf, NK_TEXT_LEFT);
                }
                nk_layout_row_end(ctx);
            }
        }
    }
    nk_end(ctx);
}

static void render_status_bar(struct nk_context *ctx)
{
    float sy = (float)G.win_h - 28.0f;
    float sw = (float)G.win_w;

    if (nk_begin(ctx, "StatusBar", nk_rect(0, sy, sw, 28.0f),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(ctx, 18, 1);
        nk_label_colored(ctx, G.status_msg, NK_TEXT_LEFT, (G.theme == THEME_DARK) ? nk_rgb(148, 163, 184) : nk_rgb(71, 85, 105));
    }
    nk_end(ctx);
}

static void render_modals(struct nk_context *ctx)
{
    /* Add / Edit Process Modal Dialog */
    if (G.dlg_open) {
        float dw = 480.0f;
        float dh = 430.0f;
        float dx = ((float)G.win_w - dw) / 2.0f;
        float dy = ((float)G.win_h - dh) / 2.0f;

        if (nk_begin(ctx, "ProcessDialog", nk_rect(dx, dy, dw, dh),
                     NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE)) {

            nk_layout_row_dynamic(ctx, 24, 2);
            nk_label(ctx, "Process Name:", NK_TEXT_LEFT);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.dlg_name, sizeof(G.dlg_name), nk_filter_ascii);

            nk_label(ctx, "Arrival Time (>= 0):", NK_TEXT_LEFT);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.dlg_arr, sizeof(G.dlg_arr), nk_filter_decimal);

            nk_label(ctx, "Burst Time (> 0):", NK_TEXT_LEFT);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.dlg_burst, sizeof(G.dlg_burst), nk_filter_decimal);

            nk_label(ctx, "Priority (1 = highest):", NK_TEXT_LEFT);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.dlg_prio, sizeof(G.dlg_prio), nk_filter_decimal);

            nk_label(ctx, "Memory Needed (KB):", NK_TEXT_LEFT);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.dlg_mem, sizeof(G.dlg_mem), nk_filter_decimal);

            nk_layout_row_dynamic(ctx, 10, 1);
            nk_spacing(ctx, 1);

            nk_layout_row_dynamic(ctx, 20, 1);
            nk_label_colored(ctx, "Resource Limits (Printer, Scanner, Disk, Network):", NK_TEXT_LEFT, (G.theme == THEME_DARK) ? nk_rgb(96, 165, 250) : nk_rgb(37, 99, 235));

            nk_layout_row_dynamic(ctx, 24, 5);
            nk_label(ctx, "Max:", NK_TEXT_LEFT);
            for (int k = 0; k < 4; k++) {
                nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.dlg_max[k], sizeof(G.dlg_max[k]), nk_filter_decimal);
            }

            nk_layout_row_dynamic(ctx, 24, 5);
            nk_label(ctx, "Held:", NK_TEXT_LEFT);
            for (int k = 0; k < 4; k++) {
                nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, G.dlg_held[k], sizeof(G.dlg_held[k]), nk_filter_decimal);
            }

            if (G.dlg_error[0]) {
                nk_layout_row_dynamic(ctx, 20, 1);
                nk_label_colored(ctx, G.dlg_error, NK_TEXT_LEFT, nk_rgb(239, 68, 68));
            } else {
                nk_layout_row_dynamic(ctx, 10, 1);
                nk_spacing(ctx, 1);
            }

            nk_layout_row_dynamic(ctx, 32, 2);
            if (nk_button_label(ctx, "Cancel")) G.dlg_open = 0;
            if (nk_button_label(ctx, "Save Process")) save_dlg();
        }
        nk_end(ctx);
    }

    /* Delete Confirmation Modal */
    if (G.confirm_dlg_open) {
        float dw = 380.0f;
        float dh = 150.0f;
        float dx = ((float)G.win_w - dw) / 2.0f;
        float dy = ((float)G.win_h - dh) / 2.0f;

        if (nk_begin(ctx, "ConfirmDelete", nk_rect(dx, dy, dw, dh),
                     NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {

            nk_layout_row_dynamic(ctx, 24, 1);
            nk_label_colored(ctx, G.confirm_msg, NK_TEXT_CENTERED, (G.theme == THEME_DARK) ? nk_rgb(240, 244, 248) : nk_rgb(15, 23, 42));

            nk_layout_row_dynamic(ctx, 15, 1);
            nk_spacing(ctx, 1);

            nk_layout_row_dynamic(ctx, 30, 2);
            if (nk_button_label(ctx, "Cancel")) G.confirm_dlg_open = 0;
            if (nk_button_label(ctx, "Delete")) {
                if (G.confirm_is_proc) {
                    db_delete_process(G.confirm_id);
                    refresh_processes();
                    set_status("Process deleted.");
                } else {
                    db_delete_run(G.confirm_id);
                    refresh_history();
                    set_status("Saved run deleted.");
                }
                G.confirm_dlg_open = 0;
            }
        }
        nk_end(ctx);
    }
}

/* ---------------------------------------------------------------- Main Entry Point */
int gui_sdl_run(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    G.win_w = 1280;
    G.win_h = 760;
    G.mode = MODE_BOOT;
    G.boot_start_time = SDL_GetTicks();

    /* Enable High-DPI support across macOS Retina, Windows 4K, and Linux HiDPI displays */
    G.win = SDL_CreateWindow(
        "Virtual OS Simulator - Multi-Platform Edition",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        G.win_w, G.win_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!G.win) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    G.renderer = SDL_CreateRenderer(G.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!G.renderer) {
        G.renderer = SDL_CreateRenderer(G.win, -1, 0);
    }
    if (!G.renderer) {
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(G.win);
        SDL_Quit();
        return 1;
    }

    /* Initialize Nuklear SDL Renderer */
    G.ctx = nk_sdl_init(G.win, G.renderer);

    /* Detect High-DPI Backbuffer Scaling */
    int win_w = 0, win_h = 0;
    int ren_w = 0, ren_h = 0;
    SDL_GetWindowSize(G.win, &win_w, &win_h);
    SDL_GetRendererOutputSize(G.renderer, &ren_w, &ren_h);
    G.dpi_scale = (win_w > 0) ? ((float)ren_w / (float)win_w) : 1.0f;
    if (G.dpi_scale < 1.0f) G.dpi_scale = 1.0f;

    /* Scale hardware drawing primitives and viewport to match backbuffer resolution */
    SDL_RenderSetScale(G.renderer, G.dpi_scale, G.dpi_scale);

    /* Bake TrueType font at high resolution with subpixel oversampling for razor-sharp rendering */
    struct nk_font_atlas *atlas;
    nk_sdl_font_stash_begin(&atlas);

    float font_size = 16.0f;
    float baked_size = font_size * G.dpi_scale;

    struct nk_font_config cfg = nk_font_config(baked_size);
    cfg.oversample_h = 3;
    cfg.oversample_v = 3;
    cfg.pixel_snap = 1;

    const char *font_candidates[] = {
        "/Library/Fonts/SF-Pro.ttf",
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial Unicode.ttf",
        "/System/Library/Fonts/Geneva.ttf",
        "/System/Library/Fonts/Monaco.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf"
    };
    G.font = NULL;
    for (size_t i = 0; i < sizeof(font_candidates) / sizeof(font_candidates[0]); i++) {
        G.font = nk_font_atlas_add_from_file(atlas, font_candidates[i], baked_size, &cfg);
        if (G.font) break;
    }
    if (!G.font) {
        G.font = nk_font_atlas_add_default(atlas, baked_size, &cfg);
    }
    nk_sdl_font_stash_end();

    if (G.font) {
        /* Normalize logical font height so UI layout coordinates align 1:1 */
        G.font->handle.height /= G.dpi_scale;
        nk_style_set_font(G.ctx, &G.font->handle);
    }

    load_settings();
    refresh_processes();
    refresh_history();

    if (G.nprocs == 0) {
        add_samples();
    }

    set_status("Virtual OS Simulator ready. Tick processes and click 'Run Simulation'.");

    int running = 1;
    while (running) {
        /* Update window and renderer dimensions */
        int cur_w = 0, cur_h = 0, cur_ren_w = 0, cur_ren_h = 0;
        SDL_GetWindowSize(G.win, &cur_w, &cur_h);
        SDL_GetRendererOutputSize(G.renderer, &cur_ren_w, &cur_ren_h);
        G.win_w = cur_w;
        G.win_h = cur_h;
        if (cur_w > 0) {
            float new_scale = (float)cur_ren_w / (float)cur_w;
            if (new_scale < 1.0f) new_scale = 1.0f;
            if (fabsf(new_scale - G.dpi_scale) > 0.01f) {
                G.dpi_scale = new_scale;
                SDL_RenderSetScale(G.renderer, G.dpi_scale, G.dpi_scale);
            }
        }

        SDL_Event ev;
        nk_input_begin(G.ctx);
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEBUTTONDOWN) {
                /* Skip boot on any key or click */
                if (G.mode == MODE_BOOT) {
                    G.mode = MODE_DESKTOP;
                    apply_theme(G.ctx, G.theme);
                }
            }
            nk_sdl_handle_event(&ev);
        }
        nk_input_end(G.ctx);

        /* Render State */
        if (G.mode == MODE_BOOT) {
            render_boot_screen(G.ctx);
        } else {
            render_top_bar(G.ctx);
            render_process_panel(G.ctx);
            render_results_panel(G.ctx);
            render_status_bar(G.ctx);
            render_modals(G.ctx);
        }

        /* Clear and Render Nuklear Frame */
        if (G.mode == MODE_BOOT) {
            SDL_SetRenderDrawColor(G.renderer, 11, 14, 20, 255);
        } else if (G.theme == THEME_DARK) {
            SDL_SetRenderDrawColor(G.renderer, 24, 28, 36, 255);
        } else {
            SDL_SetRenderDrawColor(G.renderer, 241, 245, 249, 255);
        }
        SDL_RenderClear(G.renderer);

        nk_sdl_render(NK_ANTI_ALIASING_ON);
        SDL_RenderPresent(G.renderer);
        SDL_Delay(16);
    }

    nk_sdl_shutdown();
    SDL_DestroyRenderer(G.renderer);
    SDL_DestroyWindow(G.win);
    SDL_Quit();

    return 0;
}
