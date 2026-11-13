/* Virtual OS Simulator - Interactive Terminal / CLI Interface */
#include "cli.h"
#include "core/types.h"
#include "core/scheduler.h"
#include "core/memory.h"
#include "core/resources.h"
#include "core/util.h"
#include "db/database.h"
#include "virtualization/vm_manager.h"
#include "virtualization/vm_config.h"
#include "monitoring/host_monitor.h"
#include "monitoring/agent_protocol.h"
#include "monitoring/process_monitor.h"
#include "monitoring/resource_limits.h"
#include "monitoring/resource_manager.h"
#include "monitoring/vm_monitor.h"
#include "application/app_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_DIM     "\033[2m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_WHITE   "\033[37m"
#define ANSI_BG_BLUE "\033[44m"

static const char *PID_COLORS[12] = {
    "\033[1;34m", "\033[1;33m", "\033[1;32m", "\033[1;35m",
    "\033[1;36m", "\033[1;31m", "\033[0;34m", "\033[0;32m",
    "\033[0;35m", "\033[0;36m", "\033[0;33m", "\033[1;37m"
};

typedef struct {
    Process procs[MAX_PROC];
    int ticked[MAX_PROC];
    int nprocs;

    SchedAlgo algo;
    int quantum;
    MemStrategy mem_strat;
    char blocks_txt[200];
    char totals_txt[100];
    int save_run;

    RunBundle cur;
    int has_cur;
} CliState;

static CliState s_cli;

static void cli_load_settings(void)
{
    char buf[200];
    db_get_setting("algo", buf, sizeof(buf), "0");
    s_cli.algo = (SchedAlgo)atoi(buf);
    db_get_setting("quantum", buf, sizeof(buf), "2");
    s_cli.quantum = atoi(buf);
    if (s_cli.quantum < 1) s_cli.quantum = 2;
    db_get_setting("mem", buf, sizeof(buf), "0");
    s_cli.mem_strat = (MemStrategy)atoi(buf);
    db_get_setting("blocks", s_cli.blocks_txt, sizeof(s_cli.blocks_txt), "100,500,200,300,600");
    db_get_setting("totals", s_cli.totals_txt, sizeof(s_cli.totals_txt), "10,5,7,6");
    db_get_setting("save", buf, sizeof(buf), "1");
    s_cli.save_run = atoi(buf);
}

static void cli_save_settings(void)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", (int)s_cli.algo);
    db_set_setting("algo", buf);
    snprintf(buf, sizeof(buf), "%d", s_cli.quantum);
    db_set_setting("quantum", buf);
    snprintf(buf, sizeof(buf), "%d", (int)s_cli.mem_strat);
    db_set_setting("mem", buf);
    db_set_setting("blocks", s_cli.blocks_txt);
    db_set_setting("totals", s_cli.totals_txt);
    snprintf(buf, sizeof(buf), "%d", s_cli.save_run);
    db_set_setting("save", buf);
}

static void cli_refresh_processes(void)
{
    int n = db_load_processes(s_cli.procs, MAX_PROC);
    s_cli.nprocs = (n < 0) ? 0 : n;
    for (int i = 0; i < s_cli.nprocs; i++) {
        s_cli.ticked[i] = 1; /* default: all ticked */
    }
}

static void cli_add_samples(void)
{
    static const struct { const char *n; int a, b, pr, mem; int mx[NRES], hd[NRES]; } S[5] = {
        { "P1", 0, 6, 2, 212, {7,5,3,2}, {0,1,0,1} },
        { "P2", 1, 8, 1, 417, {3,2,2,1}, {2,0,0,0} },
        { "P3", 2, 7, 4, 112, {9,0,2,3}, {3,0,2,1} },
        { "P4", 3, 3, 3, 426, {2,2,2,2}, {2,1,1,1} },
        { "P5", 4, 4, 2,  90, {4,3,3,2}, {0,0,2,1} },
    };
    for (int i = 0; i < 5 && s_cli.nprocs + i < MAX_PROC; i++) {
        Process p; memset(&p, 0, sizeof(p));
        snprintf(p.name, sizeof(p.name), "%s", S[i].n);
        p.arrival = S[i].a; p.burst = S[i].b; p.priority = S[i].pr; p.mem_kb = S[i].mem;
        memcpy(p.max, S[i].mx, sizeof(p.max));
        memcpy(p.held, S[i].hd, sizeof(p.held));
        db_save_process(&p);
    }
    cli_refresh_processes();
    printf(ANSI_GREEN "Loaded sample processes P1 through P5.\n" ANSI_RESET);
}

static void cli_list_processes(void)
{
    printf("\n" ANSI_BOLD ANSI_CYAN "=== PROCESS LIBRARY (%d processes) ===" ANSI_RESET "\n", s_cli.nprocs);
    printf("Idx Tick  PID  Name       Arr  Burst Prio  Mem(KB)  Held (P,S,D,N)    Max (P,S,D,N)\n");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < s_cli.nprocs; i++) {
        const Process *p = &s_cli.procs[i];
        const char *col = PID_COLORS[((p->pid % 12) + 12) % 12];
        printf("%2d  [%s]  %3d  %s%-10s" ANSI_RESET " %3d    %3d   %2d   %6d   [%d, %d, %d, %d]   [%d, %d, %d, %d]\n",
               i + 1,
               s_cli.ticked[i] ? ANSI_GREEN "*" ANSI_RESET : " ",
               p->pid, col, p->name,
               p->arrival, p->burst, p->priority, p->mem_kb,
               p->held[0], p->held[1], p->held[2], p->held[3],
               p->max[0], p->max[1], p->max[2], p->max[3]);
    }
    printf("--------------------------------------------------------------------------------\n");
}

static void cli_print_gantt(const SchedResult *s)
{
    if (s->ng == 0 || s->total_time <= 0) return;
    printf("\n" ANSI_BOLD ANSI_CYAN "=== GANTT CHART (Time 0 to %d) ===" ANSI_RESET "\n", s->total_time);

    /* Print top bar */
    printf("+");
    for (int i = 0; i < s->ng; i++) {
        int seg_len = s->g[i].end - s->g[i].start;
        int width = seg_len * 3;
        if (width < 5) width = 5;
        for (int k = 0; k < width; k++) printf("-");
        printf("+");
    }
    printf("\n");

    /* Print process names inside segments */
    printf("|");
    for (int i = 0; i < s->ng; i++) {
        int seg_len = s->g[i].end - s->g[i].start;
        int width = seg_len * 3;
        if (width < 5) width = 5;
        char label[32];
        const char *col = ANSI_WHITE;
        if (s->g[i].pid < 0) {
            snprintf(label, sizeof(label), "Idle");
            col = ANSI_DIM;
        } else {
            const char *name = "?";
            for (int k = 0; k < s->n; k++) {
                if (s->r[k].p.pid == s->g[i].pid) { name = s->r[k].p.name; break; }
            }
            snprintf(label, sizeof(label), "%s", name);
            col = PID_COLORS[((s->g[i].pid % 12) + 12) % 12];
        }
        int text_len = (int)strlen(label);
        int pad_left = (width - text_len) / 2;
        int pad_right = width - text_len - pad_left;
        for (int k = 0; k < pad_left; k++) printf(" ");
        printf("%s%s" ANSI_RESET, col, label);
        for (int k = 0; k < pad_right; k++) printf(" ");
        printf("|");
    }
    printf("\n");

    /* Print bottom bar */
    printf("+");
    for (int i = 0; i < s->ng; i++) {
        int seg_len = s->g[i].end - s->g[i].start;
        int width = seg_len * 3;
        if (width < 5) width = 5;
        for (int k = 0; k < width; k++) printf("-");
        printf("+");
    }
    printf("\n");

    /* Print time coordinates */
    printf("0");
    int printed_col = 1;
    for (int i = 0; i < s->ng; i++) {
        int seg_len = s->g[i].end - s->g[i].start;
        int width = seg_len * 3;
        if (width < 5) width = 5;
        char tstr[16];
        snprintf(tstr, sizeof(tstr), "%d", s->g[i].end);
        int tlen = (int)strlen(tstr);
        int target_col = printed_col + width;
        int spaces = target_col - printed_col - tlen + 1;
        if (spaces < 1) spaces = 1;
        for (int k = 0; k < spaces; k++) printf(" ");
        printf("%s", tstr);
        printed_col += spaces + tlen;
    }
    printf("\n");
}

static void cli_print_sched_results(const SchedResult *s)
{
    printf("\n" ANSI_BOLD "Algorithm:" ANSI_RESET " %s\n", s->algo_name);
    if (s->reason[0]) {
        printf(ANSI_BOLD "Decision/Notes:" ANSI_RESET " %s\n", s->reason);
    }
    printf(ANSI_BOLD ANSI_YELLOW "Metrics:" ANSI_RESET " Total Time: %d | Busy: %d | CPU Util: %.1f%% | Throughput: %.3f/unit | Ctx Switches: %d\n",
           s->total_time, s->busy_time, s->cpu_util, s->throughput, s->ctx_switches);
    printf(ANSI_BOLD ANSI_GREEN "Averages:" ANSI_RESET " Avg Waiting Time (WT): " ANSI_BOLD "%.2f" ANSI_RESET " | Avg Turnaround (TAT): " ANSI_BOLD "%.2f" ANSI_RESET " | Avg Response (RT): " ANSI_BOLD "%.2f" ANSI_RESET "\n\n",
           s->avg_wt, s->avg_tat, s->avg_rt);

    printf("PID  Name       Arrival  Burst  Priority  Finish   TAT    WT    RT\n");
    printf("-----------------------------------------------------------------\n");
    for (int i = 0; i < s->n; i++) {
        const ProcResult *pr = &s->r[i];
        const char *col = PID_COLORS[((pr->p.pid % 12) + 12) % 12];
        printf("%3d  %s%-10s" ANSI_RESET " %5d  %5d     %3d   %6d %5d %5d %5d\n",
               pr->p.pid, col, pr->p.name,
               pr->p.arrival, pr->p.burst, pr->p.priority,
               pr->completion, pr->tat, pr->wt, pr->rt);
    }
    printf("-----------------------------------------------------------------\n");
}

static void cli_print_mem_results(const MemResult *m)
{
    printf("\n" ANSI_BOLD ANSI_CYAN "=== MEMORY ALLOCATION (%s) ===" ANSI_RESET "\n", m->strategy_name);
    printf(ANSI_BOLD "Summary:" ANSI_RESET " Total: %d KB | Used: %d KB | Internal Frag: %d KB | Free: %d KB | Failed: %d\n",
           m->total_mem, m->used, m->internal_frag, m->free_kb, m->failed);

    printf("PID  Name       Requested  Block #  Block Size  Frag (Wasted)  Status\n");
    printf("---------------------------------------------------------------------\n");
    for (int i = 0; i < m->n; i++) {
        const MemAlloc *a = &m->a[i];
        if (a->allocated) {
            printf("%3d  %-10s %7d KB     B%-2d   %7d KB     %7d KB  " ANSI_GREEN "Allocated" ANSI_RESET "\n",
                   a->pid, a->name, a->size, a->block, a->block_size, a->frag);
        } else {
            printf("%3d  %-10s %7d KB     --          --          --    " ANSI_RED "Out of Memory" ANSI_RESET "\n",
                   a->pid, a->name, a->size);
        }
    }
    printf("---------------------------------------------------------------------\n");
}

static void cli_print_res_results(const ResResult *r)
{
    printf("\n" ANSI_BOLD ANSI_CYAN "=== RESOURCES & DEADLOCK / BANKER'S ALGORITHM ===" ANSI_RESET "\n");
    if (!r->valid) {
        printf(ANSI_RED "Error: %s\n" ANSI_RESET, r->message);
        return;
    }
    printf("System Totals:     Printer: %d | Scanner: %d | Disk: %d | Network: %d\n",
           r->total[0], r->total[1], r->total[2], r->total[3]);
    printf("Currently Avail:   Printer: %d | Scanner: %d | Disk: %d | Network: %d\n",
           r->avail[0], r->avail[1], r->avail[2], r->avail[3]);

    if (r->safe) {
        printf(ANSI_BOLD ANSI_GREEN "VERDICT: SYSTEM IS IN A SAFE STATE (No Deadlock)\n" ANSI_RESET);
        printf(ANSI_BOLD "Safe Execution Sequence: " ANSI_RESET);
        for (int i = 0; i < r->seq_len; i++) {
            printf(ANSI_GREEN "P%d" ANSI_RESET "%s", r->seq[i] + 1, (i < r->seq_len - 1) ? " -> " : "\n");
        }
    } else {
        printf(ANSI_BOLD ANSI_RED "VERDICT: SYSTEM IS IN AN UNSAFE STATE (Potential Deadlock!)\n" ANSI_RESET);
        printf("%s\n", r->message);
    }
}

static void cli_run_simulation(void)
{
    Process sel[MAX_PROC];
    int nsel = 0;
    for (int i = 0; i < s_cli.nprocs; i++) {
        if (s_cli.ticked[i]) {
            sel[nsel++] = s_cli.procs[i];
        }
    }
    if (nsel == 0) {
        printf(ANSI_RED "No processes ticked! Please tick at least one process first.\n" ANSI_RESET);
        return;
    }

    int blocks[MAX_BLOCKS];
    int nblocks = mem_parse_blocks(s_cli.blocks_txt, blocks, MAX_BLOCKS);
    if (nblocks < 1) {
        printf(ANSI_RED "Invalid memory blocks: '%s'\n" ANSI_RESET, s_cli.blocks_txt);
        return;
    }
    int totals[NRES];
    if (res_parse_totals(s_cli.totals_txt, totals) != 0) {
        printf(ANSI_RED "Invalid resource totals: '%s'\n" ANSI_RESET, s_cli.totals_txt);
        return;
    }

    RunBundle b;
    memset(&b, 0, sizeof(b));

    SchedAlgo algo = s_cli.algo;
    int q = s_cli.quantum;
    char reason[256] = {0};
    if (algo == ALGO_AUTO) {
        algo = sched_auto_select(sel, nsel, &q, reason, sizeof(reason));
    } else {
        snprintf(reason, sizeof(reason), "Manually selected: %s", sched_algo_info(algo));
    }

    if (sched_run(algo, sel, nsel, q, &b.sched) != 0) {
        printf(ANSI_RED "Scheduler execution failed!\n" ANSI_RESET);
        return;
    }
    snprintf(b.sched.reason, sizeof(b.sched.reason), "%s", reason);

    if (mem_run(s_cli.mem_strat, blocks, nblocks, sel, nsel, &b.mem) != 0) {
        printf(ANSI_RED "Memory allocation failed!\n" ANSI_RESET);
        return;
    }
    if (res_run(totals, sel, nsel, &b.res) != 0) {
        printf(ANSI_RED "Resource check failed: %s\n" ANSI_RESET, b.res.message);
        return;
    }

    time_t now = time(NULL);
    strftime(b.created, sizeof(b.created), "%Y-%m-%d %H:%M:%S", localtime(&now));

    if (s_cli.save_run) {
        int run_id = db_save_run(&b, s_cli.blocks_txt, s_cli.totals_txt);
        if (run_id >= 0) {
            printf(ANSI_GREEN "Simulation saved to SQLite as Run #%d.\n" ANSI_RESET, run_id);
            b.run_id = run_id;
        } else {
            printf(ANSI_YELLOW "Warning: Could not save run: %s\n" ANSI_RESET, db_error());
        }
    }

    s_cli.cur = b;
    s_cli.has_cur = 1;

    cli_print_sched_results(&b.sched);
    cli_print_gantt(&b.sched);
    cli_print_mem_results(&b.mem);
    cli_print_res_results(&b.res);
}

static void cli_compare_all(void)
{
    Process sel[MAX_PROC];
    int nsel = 0;
    for (int i = 0; i < s_cli.nprocs; i++) {
        if (s_cli.ticked[i]) sel[nsel++] = s_cli.procs[i];
    }
    if (nsel == 0) {
        printf(ANSI_RED "Tick at least one process first!\n" ANSI_RESET);
        return;
    }

    static const SchedAlgo list[6] = {
        ALGO_FCFS, ALGO_SJF, ALGO_SRTF, ALGO_PRIO_NP, ALGO_PRIO_P, ALGO_RR
    };
    SchedResult rs[6];
    memset(rs, 0, sizeof(rs));

    for (int i = 0; i < 6; i++) {
        if (sched_run(list[i], sel, nsel, s_cli.quantum, &rs[i]) != 0) {
            printf(ANSI_RED "Comparison error for algorithm %d\n" ANSI_RESET, i);
            return;
        }
    }

    int q = s_cli.quantum;
    char reason[256];
    SchedAlgo pick = sched_auto_select(sel, nsel, &q, reason, sizeof(reason));

    printf("\n" ANSI_BOLD ANSI_CYAN "=== ALGORITHM COMPARISON (%d processes) ===" ANSI_RESET "\n", nsel);
    printf("Algorithm               Avg WT    Avg TAT   Avg RT   CPU Util   Ctx Sw   Total Time   Notes\n");
    printf("---------------------------------------------------------------------------------------------\n");
    for (int i = 0; i < 6; i++) {
        int is_auto = (list[i] == pick);
        const char *star = is_auto ? ANSI_YELLOW " [AUTO PICK]" ANSI_RESET : "";
        printf("%-20s  %8.2f  %8.2f  %7.2f    %5.1f%%   %6d    %8d   %s\n",
               rs[i].algo_name, rs[i].avg_wt, rs[i].avg_tat, rs[i].avg_rt,
               rs[i].cpu_util, rs[i].ctx_switches, rs[i].total_time, star);
    }
    printf("---------------------------------------------------------------------------------------------\n");
    printf(ANSI_BOLD "Auto Heuristic Choice:" ANSI_RESET " %s\n\n", reason);
}

static void cli_add_process(void)
{
    if (s_cli.nprocs >= MAX_PROC) {
        printf(ANSI_RED "Process list is full!\n" ANSI_RESET);
        return;
    }
    Process p;
    memset(&p, 0, sizeof(p));
    char buf[64];

    printf("\n" ANSI_BOLD "Add New Process" ANSI_RESET "\n");
    printf("Enter process name [P%d]: ", s_cli.nprocs + 1);
    if (!fgets(buf, sizeof(buf), stdin) || buf[0] == '\n') {
        snprintf(p.name, sizeof(p.name), "P%d", s_cli.nprocs + 1);
    } else {
        buf[strcspn(buf, "\r\n")] = '\0';
        snprintf(p.name, sizeof(p.name), "%s", buf);
    }

    printf("Arrival time (0-500) [0]: ");
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') p.arrival = atoi(buf);

    printf("Burst time (1-200) [5]: ");
    p.burst = 5;
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') p.burst = atoi(buf);
    if (p.burst < 1) p.burst = 1;

    printf("Priority (1=highest, 1-99) [1]: ");
    p.priority = 1;
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') p.priority = atoi(buf);
    if (p.priority < 1) p.priority = 1;

    printf("Memory required in KB (1-100000) [100]: ");
    p.mem_kb = 100;
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') p.mem_kb = atoi(buf);
    if (p.mem_kb < 1) p.mem_kb = 1;

    printf("Resource Maximums (Printer, Scanner, Disk, Network) [2, 1, 1, 1]: ");
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') {
        int r[4];
        if (parse_int_list(buf, r, 4) == 4) {
            for (int k = 0; k < 4; k++) p.max[k] = r[k];
        }
    } else {
        p.max[0] = 2; p.max[1] = 1; p.max[2] = 1; p.max[3] = 1;
    }

    printf("Resource Held (Printer, Scanner, Disk, Network) [0, 0, 0, 0]: ");
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') {
        int r[4];
        if (parse_int_list(buf, r, 4) == 4) {
            for (int k = 0; k < 4; k++) {
                p.held[k] = r[k];
                if (p.held[k] > p.max[k]) p.held[k] = p.max[k];
            }
        }
    }

    if (db_save_process(&p) != 0) {
        printf(ANSI_RED "Failed to save process: %s\n" ANSI_RESET, db_error());
        return;
    }
    cli_refresh_processes();
    printf(ANSI_GREEN "Successfully added process %s (PID %d).\n" ANSI_RESET, p.name, p.pid);
}

static void cli_view_history(void)
{
    RunSummary runs[64];
    int count = db_list_runs(runs, 64);
    if (count <= 0) {
        printf("\nNo saved simulation runs found.\n");
        return;
    }
    printf("\n" ANSI_BOLD ANSI_CYAN "=== SAVED SIMULATION RUNS (%d) ===" ANSI_RESET "\n", count);
    printf("Run ID   Date and Time        Algorithm                 Procs   Avg WT    Avg TAT\n");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < count; i++) {
        printf("#%-6d  %-19s  %-24s  %5d   %7.2f    %7.2f\n",
               runs[i].run_id, runs[i].created, runs[i].algo, runs[i].nproc,
               runs[i].avg_wt, runs[i].avg_tat);
    }
    printf("--------------------------------------------------------------------------------\n");

    printf("Enter Run ID to inspect (or 0 to cancel): ");
    char buf[32];
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') {
        int id = atoi(buf);
        if (id > 0) {
            RunBundle b; char mb[200], rt[100];
            if (db_load_run(id, &b, mb, sizeof(mb), rt, sizeof(rt)) == 0) {
                printf(ANSI_GREEN "\nLoaded Run #%d from %s (%s)\n" ANSI_RESET, b.run_id, b.created, b.sched.algo_name);
                cli_print_sched_results(&b.sched);
                cli_print_gantt(&b.sched);
                cli_print_mem_results(&b.mem);
                cli_print_res_results(&b.res);
            } else {
                printf(ANSI_RED "Error loading run: %s\n" ANSI_RESET, db_error());
            }
        }
    }
}

/* ================================================================ VM Management CLI */

static VmManager *s_vm_mgr = NULL;

static void cli_vm_ensure_mgr(void)
{
    if (!s_vm_mgr) {
        s_vm_mgr = vm_manager_init(NULL);
        vm_manager_reconcile_db(s_vm_mgr);
        vm_monitor_init("virtual_os.db");
        vm_monitor_db_ensure_schema();
    }
}

static void cli_vm_list(void)
{
    cli_vm_ensure_mgr();
    vm_reap_children(s_vm_mgr);

    VmConfig configs[32];
    VmState  states[32];
    int count = vm_db_list(configs, states, NULL, 32);

    printf("\n" ANSI_BOLD ANSI_CYAN " === CONFIGURED VIRTUAL MACHINES (%d) === " ANSI_RESET "\n", count);
    if (count == 0) {
        printf(" No virtual machines configured yet. Choose [3] to create one.\n");
        return;
    }

    printf(" %-15s  %-20s  %-8s  %-4s  %-7s  %-10s  %-6s\n",
           "VM ID", "Name", "Arch", "vCPU", "RAM(MB)", "Status", "PID");
    printf(" ------------------------------------------------------------------------------\n");

    for (int i = 0; i < count; i++) {
        VmHandle live_h;
        int has_live = (vm_get_handle(s_vm_mgr, configs[i].id, &live_h) == 0);
        VmState st = has_live ? live_h.state : states[i];
        int pid = (has_live && live_h.pid > 0) ? (int)live_h.pid : 0;

        const char *st_col = (st == VM_STATE_RUNNING) ? ANSI_GREEN :
                             (st == VM_STATE_FAILED)  ? ANSI_RED :
                             (st == VM_STATE_STOPPED) ? ANSI_DIM : ANSI_YELLOW;

        printf(" %-15s  %-20.20s  %-8s  %4d  %7d  %s%-10s" ANSI_RESET "  %6d\n",
               configs[i].id, configs[i].name, vm_arch_name(configs[i].arch),
               configs[i].cpu_count, configs[i].memory_mb,
               st_col, vm_state_name(st), pid);
    }
    printf(" ------------------------------------------------------------------------------\n");
}

static void cli_vm_details(void)
{
    cli_vm_ensure_mgr();
    vm_reap_children(s_vm_mgr);

    char vmid[VM_NAME_MAX_LEN];
    printf("Enter VM ID to inspect: ");
    if (!fgets(vmid, sizeof(vmid), stdin)) return;
    vmid[strcspn(vmid, "\r\n")] = 0;
    if (vmid[0] == '\0') return;

    VmConfig cfg;
    VmState  db_st;
    if (vm_db_get(vmid, &cfg, &db_st) != 0) {
        printf(ANSI_RED "VM '%s' not found in database.\n" ANSI_RESET, vmid);
        return;
    }

    VmHandle live_h;
    int has_live = (vm_get_handle(s_vm_mgr, vmid, &live_h) == 0);
    VmState st = has_live ? live_h.state : db_st;

    printf("\n" ANSI_BOLD ANSI_CYAN "=== VM DETAILS: %s (%s) ===" ANSI_RESET "\n", cfg.id, cfg.name);
    printf(" Guest Architecture : %s\n", vm_arch_name(cfg.arch));
    printf(" OS Image Path      : %s\n", cfg.os_image_path);
    printf(" Disk Image Path    : %s\n", cfg.disk_image_path[0] ? cfg.disk_image_path : "(none configured)");
    printf(" Virtual CPUs (vCPU): %d\n", cfg.cpu_count);
    printf(" Memory Allocation  : %d MiB\n", cfg.memory_mb);
    printf(" Display Backend    : %s\n", vm_display_name(cfg.display));
    printf(" Networking Mode    : %s\n", vm_net_name(cfg.net));
    printf(" Working Directory  : %s\n", cfg.work_dir[0] ? cfg.work_dir : ".");
    printf(" Lifecycle State    : %s%s" ANSI_RESET "\n",
           (st == VM_STATE_RUNNING) ? ANSI_GREEN : (st == VM_STATE_FAILED) ? ANSI_RED : ANSI_YELLOW,
           vm_state_name(st));

    if (has_live && live_h.pid > 0) {
        printf(" Child Process PID  : %d\n", (int)live_h.pid);
        printf(" Guest Readiness    : %s\n", live_h.guest_ready ? "Guest OS ready" : "Booting / Guest readiness unconfirmed");
        printf(" Console Log File   : %s\n", live_h.console_log);
    } else {
        printf(" Child Process PID  : (not running)\n");
    }

    if (has_live && live_h.last_error[0]) {
        printf(" Last Error         : %s%s" ANSI_RESET "\n", ANSI_RED, live_h.last_error);
    }

    printf("\n" ANSI_BOLD "Recent Lifecycle Events:" ANSI_RESET "\n");
    char ev_buf[2048];
    int n = vm_db_get_events(vmid, ev_buf, sizeof(ev_buf));
    if (n > 0) {
        printf("%s", ev_buf);
    } else {
        printf(" (no events recorded yet)\n");
    }
}

static void cli_vm_create(void)
{
    cli_vm_ensure_mgr();

    VmConfig cfg;
    vm_config_init(&cfg);

    char buf[VM_PATH_MAX_LEN];

    printf("\n" ANSI_BOLD ANSI_CYAN "=== CREATE NEW ARM64 VIRTUAL MACHINE ===" ANSI_RESET "\n");

    printf("Enter unique VM identifier (e.g. alpine_1): ");
    if (!fgets(buf, sizeof(buf), stdin)) return;
    buf[strcspn(buf, "\r\n")] = 0;
    if (buf[0] == '\0') { printf(ANSI_RED "Aborted: ID cannot be empty.\n" ANSI_RESET); return; }
    snprintf(cfg.id, sizeof(cfg.id), "%s", buf);

    printf("Enter friendly VM name (e.g. Alpine Linux 3.20 ARM64): ");
    if (!fgets(buf, sizeof(buf), stdin)) return;
    buf[strcspn(buf, "\r\n")] = 0;
    snprintf(cfg.name, sizeof(cfg.name), "%s", buf[0] ? buf : cfg.id);

    printf("Enter OS Image Path (.iso installation media or bootable .qcow2/.raw):\n");
    printf(" (e.g. vm_images/alpine-arm64.iso or vm_images/alpine.qcow2): ");
    if (!fgets(buf, sizeof(buf), stdin)) return;
    buf[strcspn(buf, "\r\n")] = 0;
    if (buf[0] == '\0') { printf(ANSI_RED "Aborted: Image path cannot be empty.\n" ANSI_RESET); return; }
    snprintf(cfg.os_image_path, sizeof(cfg.os_image_path), "%s", buf);

    printf("Enter secondary disk image path (optional, press Enter to skip):\n");
    printf(" (e.g. vm_images/alpine.qcow2): ");
    if (fgets(buf, sizeof(buf), stdin)) {
        buf[strcspn(buf, "\r\n")] = 0;
        if (buf[0] != '\0') snprintf(cfg.disk_image_path, sizeof(cfg.disk_image_path), "%s", buf);
    }

    printf("vCPU count (1-16, default %d): ", cfg.cpu_count);
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') {
        int c = atoi(buf);
        if (c > 0) cfg.cpu_count = c;
    }

    printf("RAM in MiB (128-16384, default %d): ", cfg.memory_mb);
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') {
        int m = atoi(buf);
        if (m >= 128) cfg.memory_mb = m;
    }

    printf("Display backend: [0] none (headless), [1] cocoa (macOS GUI), [2] sdl, [3] vnc (default 0): ");
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') {
        int d = atoi(buf);
        if (d >= 0 && d < VM_DISPLAY_COUNT) cfg.display = (VmDisplay)d;
    }

    printf("Network mode: [0] none, [1] user (NAT with virtio-net) (default 1): ");
    if (fgets(buf, sizeof(buf), stdin) && buf[0] != '\n') {
        int net = atoi(buf);
        if (net >= 0 && net < VM_NET_COUNT) cfg.net = (VmNet)net;
    }

    if (cfg.work_dir[0] == '\0') snprintf(cfg.work_dir, sizeof(cfg.work_dir), ".");

    /* Validate */
    char qemu_path[VM_PATH_MAX_LEN] = {0};
    char err_buf[512] = {0};
    vm_manager_detect_qemu(s_vm_mgr, cfg.arch, qemu_path, sizeof(qemu_path), NULL, 0);

    int vrc = vm_config_validate(&cfg, qemu_path, err_buf, sizeof(err_buf));
    if (vrc != VM_OK) {
        printf(ANSI_RED "\n[ERROR] VM configuration validation failed:\n %s\n" ANSI_RESET, err_buf);
        return;
    }

    /* Save to DB */
    VmHandle h;
    memset(&h, 0, sizeof(h));
    h.cfg = cfg;
    h.state = VM_STATE_CREATED;
    int db_id = vm_db_save(&h);
    if (db_id <= 0) {
        printf(ANSI_RED "\n[ERROR] Failed to save VM configuration to database.\n" ANSI_RESET);
        return;
    }
    vm_db_log_event(db_id, "created", "VM configuration created via CLI");

    printf(ANSI_GREEN "\n[SUCCESS] VM '%s' (%s) configured successfully!\n" ANSI_RESET, cfg.id, cfg.name);
    printf("Note: Allocation of %d MiB RAM will be reserved from host memory during execution.\n", cfg.memory_mb);
}

static void cli_vm_start(void)
{
    cli_vm_ensure_mgr();
    vm_reap_children(s_vm_mgr);

    char vmid[VM_NAME_MAX_LEN];
    printf("Enter VM ID to start: ");
    if (!fgets(vmid, sizeof(vmid), stdin)) return;
    vmid[strcspn(vmid, "\r\n")] = 0;
    if (vmid[0] == '\0') return;

    VmConfig cfg;
    VmState  db_st;
    if (vm_db_get(vmid, &cfg, &db_st) != 0) {
        printf(ANSI_RED "VM '%s' not found in database. Create it first.\n" ANSI_RESET, vmid);
        return;
    }

    printf("Starting VM '%s' via QEMU (ARM64 / HVF)...\n", vmid);
    char err[512] = {0};
    int rc = vm_start(s_vm_mgr, &cfg, err, sizeof(err));
    if (rc != VM_MGR_OK) {
        printf(ANSI_RED "\n[ERROR] Failed to start VM '%s':\n %s\n" ANSI_RESET, vmid, err);
        return;
    }

    VmHandle h;
    vm_get_handle(s_vm_mgr, vmid, &h);
    vm_monitor_start(vmid, 1000, s_vm_mgr);
    printf(ANSI_GREEN "\n[SUCCESS] VM '%s' started successfully!\n" ANSI_RESET, vmid);
    printf(" Child Process PID : %d\n", (int)h.pid);
    printf(" Process State     : RUNNING\n");
    printf(" Guest Readiness   : Booting (UEFI firmware initialized, guest OS loading)\n");
    printf(" Real-Time Monitor : ACTIVE (virtio-serial socket /tmp/vos_agent_%s.sock)\n", vmid);
    printf(" Console Log File  : %s\n", h.console_log);
    if (cfg.display == VM_DISPLAY_COCOA) {
        printf(" Display Window    : Native macOS Cocoa window opened by QEMU.\n");
    } else if (cfg.display == VM_DISPLAY_VNC) {
        printf(" Display VNC       : Listening on 127.0.0.1:5900 (connect with Screen Sharing / VNC Viewer).\n");
    } else {
        printf(" Display           : Headless (serial console redirected to log file).\n");
    }
}

static void cli_vm_stop(void)
{
    cli_vm_ensure_mgr();
    vm_reap_children(s_vm_mgr);

    char vmid[VM_NAME_MAX_LEN];
    printf("Enter VM ID to stop: ");
    if (!fgets(vmid, sizeof(vmid), stdin)) return;
    vmid[strcspn(vmid, "\r\n")] = 0;
    if (vmid[0] == '\0') return;

    vm_monitor_stop(vmid);
    printf("Stopping VM '%s' (sending SIGTERM, 5000ms graceful shutdown timeout)...\n", vmid);
    char err[256] = {0};
    int rc = vm_stop(s_vm_mgr, vmid, 5000, err, sizeof(err));
    if (rc == VM_MGR_OK) {
        printf(ANSI_GREEN "\n[SUCCESS] VM '%s' stopped cleanly.\n" ANSI_RESET, vmid);
    } else if (rc == VM_MGR_ERR_TIMEOUT) {
        printf(ANSI_YELLOW "\n[WARNING] VM '%s' did not stop in time; forced termination (SIGKILL) was issued.\n" ANSI_RESET, vmid);
    } else {
        printf(ANSI_RED "\n[ERROR] Could not stop VM '%s': %s\n" ANSI_RESET, vmid, err);
    }
}

static void cli_vm_check_status(void)
{
    cli_vm_ensure_mgr();
    int reaped = vm_reap_children(s_vm_mgr);
    if (reaped > 0) {
        printf(ANSI_YELLOW "Note: Reaped %d exited VM process(es).\n" ANSI_RESET, reaped);
    }
    cli_vm_list();
}

static void cli_vm_delete(void)
{
    cli_vm_ensure_mgr();
    vm_reap_children(s_vm_mgr);

    char vmid[VM_NAME_MAX_LEN];
    printf("Enter VM ID to delete: ");
    if (!fgets(vmid, sizeof(vmid), stdin)) return;
    vmid[strcspn(vmid, "\r\n")] = 0;
    if (vmid[0] == '\0') return;

    char err[256] = {0};
    if (vm_db_delete(vmid, err, sizeof(err)) == 0) {
        printf(ANSI_GREEN "\n[SUCCESS] VM configuration '%s' removed from database.\n" ANSI_RESET, vmid);
        printf("Note: Virtual disk and ISO images on host disk were NOT deleted.\n");
    } else {
        printf(ANSI_RED "\n[ERROR] Failed to delete VM '%s': %s\n" ANSI_RESET, vmid, err);
    }
}

static void cli_host_monitor(void)
{
    printf("\n" ANSI_BOLD ANSI_BG_BLUE " === HOST SYSTEM RESOURCE MONITOR (macOS / Apple Silicon) === " ANSI_RESET "\n");

    host_monitor_init();
    HostMetrics h;
    memset(&h, 0, sizeof(h));
    if (host_monitor_sample(&h) != 0) {
        printf(ANSI_RED "Failed to sample host metrics.\n" ANSI_RESET);
        return;
    }

    printf(ANSI_BOLD " Hardware & Architecture:\n" ANSI_RESET);
    printf("   Architecture       : %s\n", h.cpu_arch);
    printf("   Hardware Model     : %s\n", h.host_model[0] ? h.host_model : "Apple Silicon Mac");
    printf("   CPU Core Count     : %d logical cores\n", h.cpu_core_count);

    /* Delta sample for accurate CPU percentage */
    usleep(80000);
    host_monitor_sample(&h);

    printf("\n" ANSI_BOLD " Host CPU Utilization (Mach Kernel Statistics):\n" ANSI_RESET);
    printf("   Overall CPU Load   : %5.1f %%\n", h.cpu_total_percent);
    printf("   User Space CPU     : %5.1f %%\n", h.cpu_user_percent);
    printf("   System / Kernel    : %5.1f %%\n", h.cpu_system_percent);
    printf("   Idle Time          : %5.1f %%\n", h.cpu_idle_percent);

    printf("\n" ANSI_BOLD " Physical Memory Distribution (Mach VM Info 64-bit):\n" ANSI_RESET);
    double total_gb = (double)h.memory_total_bytes / (1024.0 * 1024.0 * 1024.0);
    double used_gb  = (double)h.memory_used_bytes / (1024.0 * 1024.0 * 1024.0);
    double free_gb  = (double)h.memory_free_bytes / (1024.0 * 1024.0 * 1024.0);
    double wire_gb  = (double)h.memory_wire_bytes / (1024.0 * 1024.0 * 1024.0);
    double comp_gb  = (double)h.memory_compressed_bytes / (1024.0 * 1024.0 * 1024.0);

    printf("   Total Physical RAM : %6.2f GB (100.0 %%)\n", total_gb);
    printf("   Used Memory        : %6.2f GB (%5.1f %%)\n", used_gb, total_gb > 0 ? (used_gb / total_gb) * 100.0 : 0.0);
    printf("   Free Unused RAM    : %6.2f GB (%5.1f %%)\n", free_gb, total_gb > 0 ? (free_gb / total_gb) * 100.0 : 0.0);
    printf("   Wired (Kernel)     : %6.2f GB\n", wire_gb);
    printf("   Compressed RAM     : %6.2f GB\n", comp_gb);

    const char *pressure_str = "Normal (Level 0)";
    if (h.memory_pressure_level == 1) pressure_str = "Warning (Level 1)";
    else if (h.memory_pressure_level == 2) pressure_str = "Critical (Level 2)";
    printf("   Memory Pressure    : %s\n", pressure_str);

    /* Inspect running VMs */
    cli_vm_ensure_mgr();
    vm_reap_children(s_vm_mgr);
    VmConfig configs[16];
    VmState  states[16];
    int count = vm_db_list(configs, states, NULL, 16);

    printf("\n" ANSI_BOLD " Active Virtual Machine Hypervisors:\n" ANSI_RESET);
    int active_found = 0;
    for (int i = 0; i < count; i++) {
        VmHandle live;
        if (vm_get_handle(s_vm_mgr, configs[i].id, &live) == 0 && live.pid > 0) {
            active_found++;
            ProcessHostMetrics pm;
            host_monitor_sample_process(live.pid, &pm);
            double rss_mb = (double)pm.memory_rss_bytes / (1024.0 * 1024.0);
            printf("   [VM: %-12s] QEMU PID: %-6d | CPU: %5.1f %% | Host RSS: %6.1f MB | State: %s\n",
                   configs[i].id, (int)live.pid, pm.cpu_percent, rss_mb, pm.state);
        }
    }
    if (!active_found) {
        printf("   (No active QEMU instances currently running)\n");
    }
}

static void cli_vm_monitor_view(void)
{
    cli_vm_ensure_mgr();
    vm_reap_children(s_vm_mgr);

    char vmid[VM_NAME_MAX_LEN];
    printf("Enter VM ID to monitor: ");
    if (!fgets(vmid, sizeof(vmid), stdin)) return;
    vmid[strcspn(vmid, "\r\n")] = 0;
    if (vmid[0] == '\0') return;

    VmResourceMetrics m;
    memset(&m, 0, sizeof(m));
    if (vm_monitor_get_latest(vmid, &m) != 0) {
        /* Not yet sampled or not active; attempt sample */
        if (vm_monitor_start(vmid, 1000, s_vm_mgr) == 0) {
            usleep(100000);
            vm_monitor_get_latest(vmid, &m);
        }
    }

    printf("\n" ANSI_BOLD ANSI_BG_BLUE " === VM RESOURCE MONITOR: %s === " ANSI_RESET "\n", vmid);
    printf(" Lifecycle State       : %s\n", vm_state_name(m.vm_state));
    printf(" Uptime                : %llu seconds\n", (unsigned long long)m.uptime_seconds);
    printf(" Telemetry Source      : %s\n", metric_source_name(m.metric_source));

    if (m.freshness == TELEMETRY_LIVE) {
        printf(" Telemetry Freshness   : " ANSI_BOLD ANSI_GREEN "[LIVE]" ANSI_RESET " (Sampled %llu ms ago)\n",
               (unsigned long long)m.telemetry_age_ms);
    } else if (m.freshness == TELEMETRY_STALE) {
        printf(" Telemetry Freshness   : " ANSI_BOLD ANSI_YELLOW "[STALE]" ANSI_RESET " (Sampled %llu ms ago; hypervisor %s)\n",
               (unsigned long long)m.telemetry_age_ms,
               (m.qemu_pid > 0 && kill(m.qemu_pid, 0) == 0) ? "running" : "stopped");
    } else {
        printf(" Telemetry Freshness   : " ANSI_BOLD ANSI_RED "[UNAVAILABLE]" ANSI_RESET " (No guest agent data received)\n");
    }

    if (m.has_integrity_warning) {
        printf(ANSI_BOLD ANSI_RED "\n [!] TELEMETRY INTEGRITY ALERT:\n     %s\n" ANSI_RESET, m.integrity_warning);
    }

    printf("\n" ANSI_BOLD " [A] Host Machine Resources (macOS M5 ARM64 Mach Kernel):\n" ANSI_RESET);
    if (m.host_memory_total_bytes > 0) {
        double host_used_gb = (double)m.host_memory_used_bytes / (1024.0 * 1024.0 * 1024.0);
        double host_tot_gb  = (double)m.host_memory_total_bytes / (1024.0 * 1024.0 * 1024.0);
        if (m.host_cpu_percent >= 0.0) {
            printf("   Host Total CPU Load : %5.1f %%\n", m.host_cpu_percent);
        } else {
            printf("   Host Total CPU Load : [Unavailable / -1.0]\n");
        }
        printf("   Host Physical RAM   : %5.1f / %5.1f GB\n", host_used_gb, host_tot_gb);
    } else {
        printf("   Host system metrics not yet sampled.\n");
    }

    printf("\n" ANSI_BOLD " [B] Hypervisor Process Observations (QEMU on Host):\n" ANSI_RESET);
    if (m.qemu_pid > 0 && kill(m.qemu_pid, 0) == 0) {
        double rss_mb = (double)m.qemu_memory_rss_bytes / (1024.0 * 1024.0);
        printf("   QEMU Child PID      : %d\n", (int)m.qemu_pid);
        if (m.qemu_cpu_percent >= 0.0) {
            printf("   Host CPU Load (QEMU): %5.1f %%\n", m.qemu_cpu_percent);
        } else {
            printf("   Host CPU Load (QEMU): [Unavailable / -1.0]\n");
        }
        printf("   Host RAM (RSS)      : %6.1f MB (Configured VM RAM: %d MB)\n", rss_mb, m.memory_mb_configured);
    } else {
        printf("   QEMU hypervisor process is not running (stopped / inactive).\n");
    }

    printf("\n" ANSI_BOLD " [C] In-Guest OS Telemetry (Linux Kernel /proc via Virtio-Serial):\n" ANSI_RESET);
    if (m.has_guest_telemetry && m.freshness != TELEMETRY_UNAVAILABLE) {
        double g_tot_mb  = (double)m.guest_memory_total_bytes / (1024.0 * 1024.0);
        double g_used_mb = (double)m.guest_memory_used_bytes / (1024.0 * 1024.0);
        double g_avail_mb= (double)m.guest_memory_available_bytes / (1024.0 * 1024.0);
        if (m.guest_cpu_percent >= 0.0) {
            printf("   Guest CPU Usage     : %5.1f %%\n", m.guest_cpu_percent);
        } else {
            printf("   Guest CPU Usage     : [Initial / Uncomputed (-1.0)]\n");
        }
        printf("   Guest Memory Used   : %6.1f / %6.1f MB (Available: %6.1f MB)\n", g_used_mb, g_tot_mb, g_avail_mb);
        printf("   Disk Read / Write   : %llu B / %llu B\n", (unsigned long long)m.disk_read_bytes, (unsigned long long)m.disk_write_bytes);
        printf("   Network Rx / Tx     : %llu B / %llu B\n", (unsigned long long)m.network_rx_bytes, (unsigned long long)m.network_tx_bytes);

        GuestProcessMetric procs[32];
        int proc_cnt = 0;
        if (vm_monitor_get_processes(vmid, NULL, procs, 32, &proc_cnt) == 0 && proc_cnt > 0) {
            printf("\n" ANSI_BOLD " In-Guest Process Hierarchy & Categories (%d active):\n" ANSI_RESET, proc_cnt);
            printf("  %-7s %-20s %-12s %-8s %-10s\n", "PID", "COMMAND", "CATEGORY", "CPU %", "RSS RAM");
            printf("  ----------------------------------------------------------------\n");
            for (int i = 0; i < proc_cnt && i < 15; i++) {
                AppGroup grp = process_classify_group(procs[i].name);
                double mb = (double)procs[i].memory_rss_bytes / (1024.0 * 1024.0);
                printf("  %-7d %-20s %-12s %6.1f %% %7.1f MB\n",
                       (int)procs[i].pid, procs[i].name, process_group_name(grp),
                       procs[i].cpu_percent, mb);
            }
        }
    } else {
        printf("   Guest Agent Status  : Inactive / Not Connected\n");
        printf("   Note                : Run 'vos_guest_agent' inside guest to establish telemetry stream.\n");
        printf("                         Virtio-serial channel: /dev/virtio-ports/org.virtualos.agent.0\n");
        printf("                         Metrics are marked UNAVAILABLE rather than simulated.\n");
    }
}

static void cli_vm_limits_view(void)
{
    cli_vm_ensure_mgr();
    char vmid[VM_NAME_MAX_LEN];
    printf("Enter VM ID for resource limit inspection: ");
    if (!fgets(vmid, sizeof(vmid), stdin)) return;
    vmid[strcspn(vmid, "\r\n")] = 0;
    if (vmid[0] == '\0') return;

    VmConfig cfg;
    VmState  state;
    if (vm_db_get(vmid, &cfg, &state) != 0) {
        printf(ANSI_RED "VM '%s' not found.\n" ANSI_RESET, vmid);
        return;
    }

    printf("\n" ANSI_BOLD ANSI_BG_BLUE " === RESOURCE LIMITS & HARDWARE ENFORCEMENT === " ANSI_RESET "\n");

    /* 1. Hypervisor hardware boundary verification */
    char report[512];
    resource_limits_verify_vm(&cfg, report, sizeof(report));
    printf("%s\n", report);

    /* 2. In-guest cgroup status */
    printf("\n" ANSI_BOLD " Guest Cgroup v2 Capability Check:\n" ANSI_RESET);
    int cgv2 = resource_limits_check_cgroup_v2(NULL);
    if (cgv2 > 0) {
        printf("   Host/Guest cgroup v2: ACTIVE at /sys/fs/cgroup\n");
    } else {
        printf("   Host environment    : macOS (mach microkernel; cgroups v2 is guest-only)\n");
        printf("   Enforcement Target  : Linux ARM64 guest via guest agent or cgroupfs mount\n");
    }

    printf("\n" ANSI_BOLD " In-Guest Cgroups v2 & Hardware Controls:\n" ANSI_RESET);
    printf(" [1] Query In-Guest Cgroups v2 Capabilities (virtio-serial RPC)\n");
    printf(" [2] Create Application Resource Group (/sys/fs/cgroup/virtualos/applications/<id>)\n");
    printf(" [3] Set Group CPU Limit (cores / quota)\n");
    printf(" [4] Set Group CPU Weight (1-10000)\n");
    printf(" [5] Set Group Memory Limit (MB, verified against current usage)\n");
    printf(" [6] Set Group Maximum Process Count (pids.max)\n");
    printf(" [7] Attach In-Guest Process (PID) to Group\n");
    printf(" [8] Inspect Live Group Status & Metrics (Verified Readback)\n");
    printf(" [9] Remove Application Resource Group\n");
    printf(" [10] Set Host QEMU Process Priority (Nice value)\n");
    printf(" [0] Return\n");
    printf(ANSI_BOLD "Select option: " ANSI_RESET);

    char line[64];
    if (!fgets(line, sizeof(line), stdin)) return;
    int opt = atoi(line);

    if (opt == 1) {
        AgentCapabilities caps;
        char err[256] = {0};
        if (resource_manager_detect_capabilities(vmid, &caps, err, sizeof(err)) == RESOURCE_SUCCESS) {
            printf(ANSI_GREEN "\n=== VM '%s' Cgroups v2 Capabilities ===" ANSI_RESET "\n", vmid);
            printf(" Kernel Release        : %s\n", caps.kernel_release);
            printf(" Cgroup v2 Supported   : %s\n", caps.cgroup_v2_supported ? ANSI_GREEN "YES (unified hierarchy)" ANSI_RESET : ANSI_RED "NO" ANSI_RESET);
            printf(" Cgroup v2 Mounted     : %s (%s)\n", caps.cgroup_v2_mounted ? ANSI_GREEN "YES" ANSI_RESET : ANSI_YELLOW "NO" ANSI_RESET, caps.cgroup_root);
            printf(" CPU Controller        : %s\n", caps.cpu_controller ? ANSI_GREEN "ACTIVE (+cpu)" ANSI_RESET : ANSI_RED "DISABLED" ANSI_RESET);
            printf(" Memory Controller     : %s\n", caps.memory_controller ? ANSI_GREEN "ACTIVE (+memory)" ANSI_RESET : ANSI_RED "DISABLED" ANSI_RESET);
            printf(" PIDs Controller       : %s\n", caps.pids_controller ? ANSI_GREEN "ACTIVE (+pids)" ANSI_RESET : ANSI_RED "DISABLED" ANSI_RESET);
            printf(" Disk I/O Controller   : %s\n", caps.io_controller ? ANSI_GREEN "ACTIVE (+io)" ANSI_RESET : ANSI_YELLOW "DISABLED / UNSUPPORTED" ANSI_RESET);
            printf(" GPU Virtualization    : %s (%s)\n", caps.gpu_supported ? ANSI_GREEN "SUPPORTED" ANSI_RESET : ANSI_YELLOW "UNSUPPORTED" ANSI_RESET, caps.gpu_status[0] ? caps.gpu_status : "UNSUPPORTED_BY_HOST_HYPERVISOR");
        } else {
            printf(ANSI_RED "[ERROR] Could not query capabilities: %s\n" ANSI_RESET, err[0] ? err : "Guest agent offline");
        }
    } else if (opt == 2) {
        char gid[64], gname[64];
        printf("Enter Group ID (e.g. app_worker, test_app): ");
        if (!fgets(gid, sizeof(gid), stdin)) return;
        gid[strcspn(gid, "\r\n")] = 0;
        printf("Enter Group Description / Name: ");
        if (!fgets(gname, sizeof(gname), stdin)) return;
        gname[strcspn(gname, "\r\n")] = 0;
        char err[256] = {0};
        if (resource_manager_create_group(vmid, gid, gname, err, sizeof(err)) == RESOURCE_SUCCESS) {
            printf(ANSI_GREEN "[SUCCESS] Resource group '%s' created and active.\n" ANSI_RESET, gid);
        } else {
            printf(ANSI_RED "[ERROR] Failed to create group: %s\n" ANSI_RESET, err);
        }
    } else if (opt == 3) {
        char gid[64];
        printf("Enter Group ID: ");
        if (!fgets(gid, sizeof(gid), stdin)) return;
        gid[strcspn(gid, "\r\n")] = 0;
        printf("Enter CPU cores limit (e.g. 1.0, 1.5, 0.5; <=0 for unlimited): ");
        if (!fgets(line, sizeof(line), stdin)) return;
        double cores = atof(line);
        char err[256] = {0};
        if (resource_manager_set_cpu_limit(vmid, gid, cores, 100000, err, sizeof(err)) == RESOURCE_SUCCESS) {
            printf(ANSI_GREEN "[SUCCESS] CPU limit of %.2f cores enforced on '%s' (VERIFIED)\n" ANSI_RESET, cores, gid);
        } else {
            printf(ANSI_RED "[ERROR] Failed to enforce CPU limit: %s\n" ANSI_RESET, err);
        }
    } else if (opt == 4) {
        char gid[64];
        printf("Enter Group ID: ");
        if (!fgets(gid, sizeof(gid), stdin)) return;
        gid[strcspn(gid, "\r\n")] = 0;
        printf("Enter CPU Weight (1 - 10000, default 100): ");
        if (!fgets(line, sizeof(line), stdin)) return;
        int weight = atoi(line);
        char err[256] = {0};
        if (resource_manager_set_cpu_weight(vmid, gid, weight, err, sizeof(err)) == RESOURCE_SUCCESS) {
            printf(ANSI_GREEN "[SUCCESS] CPU weight %d enforced on '%s' (VERIFIED)\n" ANSI_RESET, weight, gid);
        } else {
            printf(ANSI_RED "[ERROR] Failed to set CPU weight: %s\n" ANSI_RESET, err);
        }
    } else if (opt == 5) {
        char gid[64];
        printf("Enter Group ID: ");
        if (!fgets(gid, sizeof(gid), stdin)) return;
        gid[strcspn(gid, "\r\n")] = 0;
        printf("Enter Max Memory in MB (e.g. 256; 0 for unlimited): ");
        if (!fgets(line, sizeof(line), stdin)) return;
        uint64_t bytes = (uint64_t)atoll(line) * 1024ULL * 1024ULL;
        char err[256] = {0};
        ResourceResult res = resource_manager_set_memory_limit(vmid, gid, bytes, err, sizeof(err));
        if (res == RESOURCE_SUCCESS) {
            printf(ANSI_GREEN "[SUCCESS] Memory limit %llu MB enforced on '%s' (VERIFIED)\n" ANSI_RESET, (unsigned long long)atoll(line), gid);
        } else if (res == RESOURCE_ERR_LIMIT_BELOW_USAGE) {
            printf(ANSI_YELLOW "[REJECTED] %s\n" ANSI_RESET, err);
        } else {
            printf(ANSI_RED "[ERROR] Failed to set memory limit: %s\n" ANSI_RESET, err);
        }
    } else if (opt == 6) {
        char gid[64];
        printf("Enter Group ID: ");
        if (!fgets(gid, sizeof(gid), stdin)) return;
        gid[strcspn(gid, "\r\n")] = 0;
        printf("Enter Max Process Count (e.g. 50; <=0 for unlimited): ");
        if (!fgets(line, sizeof(line), stdin)) return;
        int64_t pmax = atoll(line);
        char err[256] = {0};
        if (resource_manager_set_process_limit(vmid, gid, pmax, err, sizeof(err)) == RESOURCE_SUCCESS) {
            printf(ANSI_GREEN "[SUCCESS] Process limit %lld enforced on '%s' (VERIFIED)\n" ANSI_RESET, (long long)pmax, gid);
        } else {
            printf(ANSI_RED "[ERROR] Failed to set process limit: %s\n" ANSI_RESET, err);
        }
    } else if (opt == 7) {
        char gid[64];
        printf("Enter Group ID: ");
        if (!fgets(gid, sizeof(gid), stdin)) return;
        gid[strcspn(gid, "\r\n")] = 0;
        printf("Enter In-Guest Process PID to attach: ");
        if (!fgets(line, sizeof(line), stdin)) return;
        pid_t pid = (pid_t)atoi(line);
        char err[256] = {0};
        if (resource_manager_attach_process(vmid, gid, pid, err, sizeof(err)) == RESOURCE_SUCCESS) {
            printf(ANSI_GREEN "[SUCCESS] Attached PID %d to group '%s' (VERIFIED in cgroup.procs)\n" ANSI_RESET, (int)pid, gid);
        } else {
            printf(ANSI_RED "[ERROR] Failed to attach process: %s\n" ANSI_RESET, err);
        }
    } else if (opt == 8) {
        char gid[64];
        printf("Enter Group ID: ");
        if (!fgets(gid, sizeof(gid), stdin)) return;
        gid[strcspn(gid, "\r\n")] = 0;
        AgentCgroupStatus st;
        char err[256] = {0};
        if (resource_manager_get_group_status(vmid, gid, &st, err, sizeof(err)) == RESOURCE_SUCCESS) {
            printf(ANSI_GREEN "\n=== Resource Group Status: '%s' (VM '%s') ===" ANSI_RESET "\n", gid, vmid);
            printf(" Exists in Cgroupfs    : %s\n", st.exists ? "YES" : "NO");
            printf(" CPU Quota / Period    : %lld us / %lld us (Weight: %d)\n", (long long)st.cpu_quota_us, (long long)st.cpu_period_us, st.cpu_weight);
            printf(" Total CPU Time Used   : %.3f sec (%llu us)\n", (double)st.cpu_usage_usec / 1000000.0, (unsigned long long)st.cpu_usage_usec);
            printf(" Memory Max Ceiling    : %llu bytes (%.2f MB)\n", (unsigned long long)st.memory_max_bytes, (double)st.memory_max_bytes / (1024.0 * 1024.0));
            printf(" Current Memory Usage  : %llu bytes (%.2f MB)\n", (unsigned long long)st.memory_current_bytes, (double)st.memory_current_bytes / (1024.0 * 1024.0));
            printf(" Memory OOM Kill Events: %llu\n", (unsigned long long)st.memory_oom_events);
            printf(" Maximum Allowed PIDs  : %lld\n", (long long)st.pids_max);
            printf(" Current Active PIDs   : %lld\n", (long long)st.pids_current);
            printf(" Attached PIDs Count   : %d\n", st.attached_pids_count);
            printf(" Verification State    : " ANSI_GREEN "OS_VERIFIED" ANSI_RESET "\n");
        } else {
            printf(ANSI_RED "[ERROR] Could not read group status: %s\n" ANSI_RESET, err);
        }
    } else if (opt == 9) {
        char gid[64];
        printf("Enter Group ID to remove: ");
        if (!fgets(gid, sizeof(gid), stdin)) return;
        gid[strcspn(gid, "\r\n")] = 0;
        char err[256] = {0};
        if (resource_manager_remove_group(vmid, gid, err, sizeof(err)) == RESOURCE_SUCCESS) {
            printf(ANSI_GREEN "[SUCCESS] Resource group '%s' removed.\n" ANSI_RESET, gid);
        } else {
            printf(ANSI_RED "[ERROR] Failed to remove group: %s\n" ANSI_RESET, err);
        }
    } else if (opt == 10) {
        VmHandle live;
        if (vm_get_handle(s_vm_mgr, vmid, &live) == 0 && live.pid > 0) {
            printf("Enter Nice value for QEMU PID %d (-20 to 19, higher = lower priority): ", (int)live.pid);
            if (fgets(line, sizeof(line), stdin)) {
                int nice_val = atoi(line);
                char err[256] = {0};
                if (resource_limits_apply_host_priority(live.pid, nice_val, err, sizeof(err)) == 0) {
                    printf(ANSI_GREEN "[SUCCESS] %s\n" ANSI_RESET, err);
                } else {
                    printf(ANSI_YELLOW "[NOTICE] %s\n" ANSI_RESET, err);
                }
            }
        } else {
            printf(ANSI_RED "VM '%s' is not currently running.\n" ANSI_RESET, vmid);
        }
    }
}

static void cli_vm_app_menu(void)
{
    char vmid[64] = {0};
    printf("Enter VM ID for application management: ");
    char line[128];
    if (!fgets(line, sizeof(line), stdin)) return;
    line[strcspn(line, "\r\n")] = 0;
    if (strlen(line) == 0) return;
    strncpy(vmid, line, sizeof(vmid) - 1);

    app_manager_init(NULL);

    while (1) {
        printf("\n" ANSI_BOLD ANSI_BG_BLUE " === APPLICATION MANAGEMENT (VM: %s) === " ANSI_RESET "\n", vmid);
        printf(" [1] Discover In-Guest Applications (/bin, /usr/bin, /usr/local/bin, /opt)\n");
        printf(" [2] List Registered Applications\n");
        printf(" [3] Register New Application\n");
        printf(" [4] View Application Details (info)\n");
        printf(" [5] Unregister Application (Preserves Guest Files)\n");
        printf(" [6] View / Create Resource Profiles\n");
        printf(" [7] Launch Application (Pre-Execution Cgroup Handshake)\n");
        printf(" [8] Stop Running Application Instance\n");
        printf(" [9] View Running Instances (ps) & Live Metrics\n");
        printf(" [10] Reconcile Dead Instances\n");
        printf(" [0] Return to VM Manager Menu\n");
        printf(ANSI_BOLD "Select option: " ANSI_RESET);

        if (!fgets(line, sizeof(line), stdin)) break;
        int opt = atoi(line);
        if (opt == 0) break;

        switch (opt) {
        case 1: {
            AgentAppDiscoveryList discovery;
            char err[256] = {0};
            printf("Discovering applications in VM '%s'...\n", vmid);
            if (app_manager_discover_apps(vmid, &discovery, err, sizeof(err)) == APP_SUCCESS) {
                printf(ANSI_BOLD "=== Discovered %d In-Guest Applications ===" ANSI_RESET "\n", discovery.count);
                printf("%-4s %-20s %-16s %s\n", "#", "Name", "Package", "Executable Path");
                printf("----------------------------------------------------------------------\n");
                for (int i = 0; i < discovery.count; i++) {
                    printf("[%2d] %-20s %-16s %s\n", i + 1,
                           discovery.apps[i].name,
                           discovery.apps[i].package,
                           discovery.apps[i].path);
                }
            } else {
                printf(ANSI_RED "Discovery failed: %s\n" ANSI_RESET, err[0] ? err : "unknown error");
            }
            break;
        }
        case 2: {
            ApplicationEntry apps[64];
            int count = 0;
            if (app_manager_list_apps(vmid, NULL, apps, 64, &count) == APP_SUCCESS) {
                printf(ANSI_BOLD "=== Registered Applications for VM '%s' (%d total) ===" ANSI_RESET "\n", vmid, count);
                printf("%-16s %-18s %-14s %-12s %-8s %-6s %s\n",
                       "APP ID", "NAME", "CATEGORY", "PROFILE", "ENABLED", "LAUNCH", "PATH");
                printf("----------------------------------------------------------------------------------------\n");
                for (int i = 0; i < count; i++) {
                    printf("%-16s %-18s %-14s %-12s %-8s %-6d %s\n",
                           apps[i].app_id, apps[i].name, apps[i].category,
                           apps[i].default_profile_id[0] ? apps[i].default_profile_id : "default",
                           apps[i].enabled ? "YES" : "NO",
                           apps[i].launch_count,
                           apps[i].executable_path);
                }
            } else {
                printf(ANSI_RED "Failed to list applications.\n" ANSI_RESET);
            }
            break;
        }
        case 3: {
            char app_id[64] = {0}, name[128] = {0}, path[256] = {0}, cat[64] = {0}, prof[64] = {0};
            printf("App ID: "); if (!fgets(line, sizeof(line), stdin)) break; line[strcspn(line, "\r\n")] = 0; strncpy(app_id, line, sizeof(app_id)-1);
            printf("App Display Name: "); if (!fgets(line, sizeof(line), stdin)) break; line[strcspn(line, "\r\n")] = 0; strncpy(name, line, sizeof(name)-1);
            printf("Guest Executable Path: "); if (!fgets(line, sizeof(line), stdin)) break; line[strcspn(line, "\r\n")] = 0; strncpy(path, line, sizeof(path)-1);
            printf("Category [System Utility]: "); if (fgets(line, sizeof(line), stdin)) { line[strcspn(line, "\r\n")] = 0; if (line[0]) strncpy(cat, line, sizeof(cat)-1); }
            printf("Resource Profile [default]: "); if (fgets(line, sizeof(line), stdin)) { line[strcspn(line, "\r\n")] = 0; if (line[0]) strncpy(prof, line, sizeof(prof)-1); }
            char err[256] = {0};
            if (app_manager_register_app(vmid, app_id, name, path, "", cat[0] ? cat : "System Utility", "1.0", prof[0] ? prof : "default", err, sizeof(err)) == APP_SUCCESS) {
                printf(ANSI_GREEN "[SUCCESS] Registered application '%s' successfully.\n" ANSI_RESET, app_id);
            } else {
                printf(ANSI_RED "Registration failed: %s\n" ANSI_RESET, err[0] ? err : "unknown error");
            }
            break;
        }
        case 4: {
            printf("App ID: "); if (!fgets(line, sizeof(line), stdin)) break; line[strcspn(line, "\r\n")] = 0;
            ApplicationEntry app;
            char err[256] = {0};
            if (app_manager_get_app(vmid, line, &app, err, sizeof(err)) == APP_SUCCESS) {
                printf(ANSI_BOLD "=== Application: %s ===" ANSI_RESET "\n", app.app_id);
                printf(" Name            : %s\n", app.name);
                printf(" VM ID           : %s\n", app.vm_id);
                printf(" Executable Path : %s\n", app.executable_path);
                printf(" Category        : %s\n", app.category);
                printf(" Version         : %s\n", app.version);
                printf(" Default Profile : %s\n", app.default_profile_id);
                printf(" Status          : %s\n", app.enabled ? "ENABLED" : "DISABLED");
                printf(" Launch Count    : %d\n", app.launch_count);
            } else {
                printf(ANSI_RED "App not found: %s\n" ANSI_RESET, err[0] ? err : "not found");
            }
            break;
        }
        case 5: {
            printf("App ID to unregister: "); if (!fgets(line, sizeof(line), stdin)) break; line[strcspn(line, "\r\n")] = 0;
            char err[256] = {0};
            if (app_manager_unregister_app(vmid, line, err, sizeof(err)) == APP_SUCCESS) {
                printf(ANSI_GREEN "[SUCCESS] Unregistered app '%s' from SQLite. (Guest binary remains intact).\n" ANSI_RESET, line);
            } else {
                printf(ANSI_RED "Unregister failed: %s\n" ANSI_RESET, err[0] ? err : "unknown error");
            }
            break;
        }
        case 6: {
            ApplicationProfile profiles[32];
            int count = 0;
            if (app_manager_list_profiles(profiles, 32, &count) == APP_SUCCESS) {
                printf(ANSI_BOLD "=== Resource Profiles (%d total) ===" ANSI_RESET "\n", count);
                printf("%-16s %-18s %-8s %-8s %-12s %-10s\n", "PROFILE ID", "NAME", "CORES", "WEIGHT", "MAX MEM(MB)", "MAX PIDS");
                printf("----------------------------------------------------------------------------\n");
                for (int i = 0; i < count; i++) {
                    printf("%-16s %-18s %-8.2f %-8d %-12llu %-10lld\n",
                           profiles[i].profile_id, profiles[i].name,
                           profiles[i].cpu_cores, profiles[i].cpu_weight,
                           (unsigned long long)(profiles[i].memory_max_bytes / (1024 * 1024)),
                           (long long)profiles[i].pids_max);
                }
            }
            break;
        }
        case 7: {
            char app_id[64] = {0}, args[256] = {0};
            printf("App ID to launch: "); if (!fgets(line, sizeof(line), stdin)) break; line[strcspn(line, "\r\n")] = 0; strncpy(app_id, line, sizeof(app_id)-1);
            printf("Arguments (optional): "); if (fgets(line, sizeof(line), stdin)) { line[strcspn(line, "\r\n")] = 0; strncpy(args, line, sizeof(args)-1); }
            char inst_id[96] = {0}, err[256] = {0};
            if (app_manager_launch_app(vmid, app_id, args[0] ? args : NULL, NULL, inst_id, sizeof(inst_id), err, sizeof(err)) == APP_SUCCESS) {
                ApplicationInstance inst;
                app_manager_get_instance(inst_id, &inst, NULL, 0);
                printf(ANSI_GREEN "[SUCCESS] Launched application '%s' successfully!\n" ANSI_RESET, app_id);
                printf("  Instance ID : %s\n", inst.instance_id);
                printf("  Guest PID   : %d\n", (int)inst.guest_pid);
                printf("  Start Time  : %llu\n", (unsigned long long)inst.process_start_time);
                printf("  Cgroup      : %s\n", inst.cgroup_path);
                printf("  State       : %s\n", app_state_name(inst.state));
            } else {
                printf(ANSI_RED "Launch failed: %s\n" ANSI_RESET, err[0] ? err : "unknown error");
            }
            break;
        }
        case 8: {
            printf("Instance ID to stop: "); if (!fgets(line, sizeof(line), stdin)) break; line[strcspn(line, "\r\n")] = 0;
            char err[256] = {0};
            if (app_manager_stop_app(vmid, line, 3000, 0, err, sizeof(err)) == APP_SUCCESS) {
                printf(ANSI_GREEN "[SUCCESS] Stopped instance '%s'.\n" ANSI_RESET, line);
            } else {
                printf(ANSI_RED "Stop failed: %s\n" ANSI_RESET, err[0] ? err : "unknown error");
            }
            break;
        }
        case 9: {
            ApplicationInstance instances[64];
            int count = 0;
            if (app_manager_list_instances(vmid, (AppState)-1, instances, 64, &count) == APP_SUCCESS) {
                printf(ANSI_BOLD "=== Running Application Instances for VM '%s' (%d total) ===" ANSI_RESET "\n", vmid, count);
                printf("%-24s %-16s %-8s %-10s %-14s\n", "INSTANCE ID", "APP ID", "PID", "STATE", "PROFILE");
                printf("----------------------------------------------------------------------------\n");
                for (int i = 0; i < count; i++) {
                    printf("%-24s %-16s %-8d %-10s %-14s\n",
                           instances[i].instance_id, instances[i].app_id,
                           (int)instances[i].guest_pid, app_state_name(instances[i].state),
                           instances[i].profile_id);
                    if (instances[i].state == APP_STATE_RUNNING) {
                        ApplicationResourceSample sample;
                        char err[256] = {0};
                        if (app_manager_collect_metrics(vmid, instances[i].instance_id, &sample, err, sizeof(err)) == APP_SUCCESS) {
                            printf("  └─ CPU: %.1f%% (Throttled: %lluus) | RAM: %.1fMB (Peak: %.1fMB) | PIDs: %lld/%lld\n",
                                   sample.cpu_usage_pct, (unsigned long long)sample.throttled_usec,
                                   (double)sample.memory_current_bytes / (1024.0*1024.0),
                                   (double)sample.memory_peak_bytes / (1024.0*1024.0),
                                   (long long)sample.pids_current, (long long)sample.pids_max);
                        }
                    }
                }
            }
            break;
        }
        case 10: {
            int reconciled = 0;
            if (app_manager_reconcile_instances(vmid, &reconciled) == APP_SUCCESS) {
                printf(ANSI_GREEN "[SUCCESS] Reconciled instances for VM '%s'. Dead instances updated: %d\n" ANSI_RESET, vmid, reconciled);
            } else {
                printf(ANSI_RED "Reconciliation failed.\n" ANSI_RESET);
            }
            break;
        }
        default:
            printf("Unknown option.\n");
            break;
        }
    }
}

static void cli_vm_menu(void)
{
    cli_vm_ensure_mgr();
    char line[64];
    while (1) {
        printf("\n" ANSI_BOLD ANSI_BG_BLUE " === VIRTUAL MACHINE MANAGER (ARM64 / QEMU) === " ANSI_RESET "\n");
        printf(" [1] List All Virtual Machines\n");
        printf(" [2] View VM Details & Event Log\n");
        printf(" [3] Create New VM Configuration\n");
        printf(" [4] Start Virtual Machine\n");
        printf(" [5] Stop Running VM\n");
        printf(" [6] Refresh / Check VM Status\n");
        printf(" [7] Delete VM Configuration\n");
        printf(" [8] Live Resource Monitor & Guest Telemetry\n");
        printf(" [9] Resource Limits & Hardware Verification\n");
        printf(" [10] Application Management & In-Guest Launch\n");
        printf(" [0] Return to Main Menu\n");
        printf(ANSI_BOLD "Select option: " ANSI_RESET);

        if (!fgets(line, sizeof(line), stdin)) break;
        int opt = atoi(line);
        if (opt == 0) break;

        switch (opt) {
        case 1: cli_vm_list(); break;
        case 2: cli_vm_details(); break;
        case 3: cli_vm_create(); break;
        case 4: cli_vm_start(); break;
        case 5: cli_vm_stop(); break;
        case 6: cli_vm_check_status(); break;
        case 7: cli_vm_delete(); break;
        case 8: cli_vm_monitor_view(); break;
        case 9: cli_vm_limits_view(); break;
        case 10: cli_vm_app_menu(); break;
        default: printf("Unknown option.\n"); break;
        }
    }
}

int cli_run(void)
{
    cli_load_settings();
    cli_refresh_processes();

    if (s_cli.nprocs == 0) {
        printf("Process library empty. Loading 5 default sample processes...\n");
        cli_add_samples();
    }

    char line[128];
    while (1) {
        printf("\n" ANSI_BOLD ANSI_BG_BLUE " === VIRTUAL OS SIMULATOR (CLI MODE) === " ANSI_RESET "\n");
        printf(" [1] List & Toggle Processes (%d procs)\n", s_cli.nprocs);
        printf(" [2] Run Simulation (CPU, Memory, Banker's)\n");
        printf(" [3] Compare All 6 CPU Scheduling Algorithms\n");
        printf(" [4] Add New Process\n");
        printf(" [5] Load 5 Sample Processes\n");
        printf(" [6] View Saved Runs (History)\n");
        printf(" [7] Configure Settings (Algorithm, Quantum, Blocks, Totals)\n");
        printf(" [8] Virtual Machine Manager (ARM64 / QEMU)\n");
        printf(" [9] Host System Resource Monitor (macOS Apple Silicon)\n");
        printf(" [0] Exit\n");
        printf(ANSI_BOLD "Select option: " ANSI_RESET);

        if (!fgets(line, sizeof(line), stdin)) break;
        int opt = atoi(line);

        if (opt == 0) {
            cli_save_settings();
            vm_monitor_shutdown();
            if (s_vm_mgr) {
                vm_manager_shutdown(s_vm_mgr);
                s_vm_mgr = NULL;
            }
            printf("Exiting Virtual OS Simulator. Goodbye!\n");
            break;
        }

        switch (opt) {
        case 1: {
            cli_list_processes();
            printf("Enter process index (1-%d) to toggle tick, 'a' for all, 'n' for none, or Enter to return: ", s_cli.nprocs);
            if (fgets(line, sizeof(line), stdin) && line[0] != '\n') {
                if (line[0] == 'a' || line[0] == 'A') {
                    for (int i = 0; i < s_cli.nprocs; i++) s_cli.ticked[i] = 1;
                } else if (line[0] == 'n' || line[0] == 'N') {
                    for (int i = 0; i < s_cli.nprocs; i++) s_cli.ticked[i] = 0;
                } else {
                    int idx = atoi(line);
                    if (idx >= 1 && idx <= s_cli.nprocs) {
                        s_cli.ticked[idx - 1] = !s_cli.ticked[idx - 1];
                    }
                }
            }
            break;
        }
        case 2:
            cli_run_simulation();
            break;
        case 3:
            cli_compare_all();
            break;
        case 4:
            cli_add_process();
            break;
        case 5:
            cli_add_samples();
            break;
        case 6:
            cli_view_history();
            break;
        case 7: {
            printf("\n" ANSI_BOLD "Current Settings:" ANSI_RESET "\n");
            printf(" 1. Algorithm: %d (0:Auto, 1:FCFS, 2:SJF, 3:SRTF, 4:PrioNP, 5:PrioP, 6:RR)\n", (int)s_cli.algo);
            printf(" 2. RR Quantum: %d\n", s_cli.quantum);
            printf(" 3. Memory Strategy: %d (0:Auto, 1:First, 2:Best, 3:Worst)\n", (int)s_cli.mem_strat);
            printf(" 4. Memory Blocks: %s\n", s_cli.blocks_txt);
            printf(" 5. Resource Totals: %s\n", s_cli.totals_txt);
            printf(" 6. Save Runs to DB: %d\n", s_cli.save_run);
            printf("Change setting (1-6) or 0 to go back: ");
            if (fgets(line, sizeof(line), stdin)) {
                int c = atoi(line);
                if (c == 1) {
                    printf("New Algo (0-6): ");
                    if (fgets(line, sizeof(line), stdin)) s_cli.algo = (SchedAlgo)atoi(line);
                } else if (c == 2) {
                    printf("New Quantum (1-100): ");
                    if (fgets(line, sizeof(line), stdin)) s_cli.quantum = atoi(line);
                } else if (c == 3) {
                    printf("New Mem Strategy (0-3): ");
                    if (fgets(line, sizeof(line), stdin)) s_cli.mem_strat = (MemStrategy)atoi(line);
                } else if (c == 4) {
                    printf("New Memory Blocks (e.g. 100,500,200): ");
                    if (fgets(line, sizeof(line), stdin) && line[0] != '\n') {
                        line[strcspn(line, "\r\n")] = 0;
                        snprintf(s_cli.blocks_txt, sizeof(s_cli.blocks_txt), "%s", line);
                    }
                } else if (c == 5) {
                    printf("New Resource Totals (4 numbers, e.g. 10,5,7,6): ");
                    if (fgets(line, sizeof(line), stdin) && line[0] != '\n') {
                        line[strcspn(line, "\r\n")] = 0;
                        snprintf(s_cli.totals_txt, sizeof(s_cli.totals_txt), "%s", line);
                    }
                } else if (c == 6) {
                    s_cli.save_run = !s_cli.save_run;
                    printf("Save run toggled to: %d\n", s_cli.save_run);
                }
                cli_save_settings();
            }
            break;
        }
        case 8:
            cli_vm_menu();
            break;
        case 9:
            cli_host_monitor();
            break;
        default:
            printf("Unknown option.\n");
            break;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ Non-Interactive CLI Dispatch */

static void print_resource_help(void)
{
    printf(ANSI_BOLD "Virtual OS Simulator - Resource Management CLI" ANSI_RESET "\n");
    printf("Usage: virtualos resource <subcommand> [options]\n\n");
    printf("Subcommands:\n");
    printf("  capabilities <vm_id>                         Detect in-guest cgroups v2 controllers & capabilities\n");
    printf("  create <vm_id> <group_id> [group_name]       Create application cgroup under virtualos/applications/\n");
    printf("  set-cpu <vm_id> <group_id> <cores> [period]  Enforce CPU cores quota (e.g. 1.5, 0.5) and period\n");
    printf("  set-weight <vm_id> <group_id> <weight>       Enforce CPU proportional weight (1-10000)\n");
    printf("  set-memory <vm_id> <group_id> <bytes|mb>     Enforce memory.max ceiling (safe check vs current)\n");
    printf("  set-pids <vm_id> <group_id> <max_pids>       Enforce maximum active process count\n");
    printf("  set-io <vm_id> <group_id> <read_bps> <write> Enforce disk I/O limits (or report UNSUPPORTED)\n");
    printf("  attach <vm_id> <group_id> <pid>              Attach guest process PID to resource group\n");
    printf("  status <vm_id> <group_id>                    Inspect applied limits, verified state, and live usage\n");
    printf("  list <vm_id>                                 List active resource groups for virtual machine\n");
    printf("  remove <vm_id> <group_id>                    Safely remove resource group and restore processes\n");
    printf("  set-hardware <vm_id> <vcpus> <ram_mb>        Set VM hardware limits (requires VM restart)\n");
}

static int cli_handle_resource_command(int argc, char **argv)
{
    if (argc < 3) {
        print_resource_help();
        return 1;
    }

    const char *subcmd = argv[2];
    resource_manager_init(NULL);

    if (strcmp(subcmd, "capabilities") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: missing vm_id. Usage: virtualos resource capabilities <vm_id>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        AgentCapabilities caps;
        char err[256] = {0};
        ResourceResult res = resource_manager_detect_capabilities(vm_id, &caps, err, sizeof(err));
        if (res != RESOURCE_SUCCESS) {
            fprintf(stderr, "Error querying capabilities for VM '%s': %s\n", vm_id, err[0] ? err : resource_result_str(res));
            return 1;
        }
        printf(ANSI_BOLD "=== Virtual Machine '%s' Resource Capabilities ===" ANSI_RESET "\n", vm_id);
        printf(" Kernel Release        : %s\n", caps.kernel_release);
        printf(" Cgroup v2 Supported   : %s\n", caps.cgroup_v2_supported ? "YES" : "NO");
        printf(" Cgroup v2 Mounted     : %s (%s)\n", caps.cgroup_v2_mounted ? "YES" : "NO", caps.cgroup_root);
        printf(" CPU Controller        : %s\n", caps.cpu_controller ? "ACTIVE (+cpu)" : "DISABLED");
        printf(" Memory Controller     : %s\n", caps.memory_controller ? "ACTIVE (+memory)" : "DISABLED");
        printf(" PIDs Controller       : %s\n", caps.pids_controller ? "ACTIVE (+pids)" : "DISABLED");
        printf(" Disk I/O Controller   : %s\n", caps.io_controller ? "ACTIVE (+io)" : "DISABLED / UNSUPPORTED");
        printf(" GPU Virtualization    : %s (%s)\n", caps.gpu_supported ? "SUPPORTED" : "UNSUPPORTED", caps.gpu_status[0] ? caps.gpu_status : "UNSUPPORTED_BY_HOST_HYPERVISOR");
        return 0;
    }

    if (strcmp(subcmd, "create") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos resource create <vm_id> <group_id> [group_name]\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *group_id = argv[4];
        const char *group_name = (argc >= 6) ? argv[5] : group_id;
        char err[256] = {0};
        ResourceResult res = resource_manager_create_group(vm_id, group_id, group_name, err, sizeof(err));
        if (res != RESOURCE_SUCCESS) {
            fprintf(stderr, "Error creating group '%s': %s\n", group_id, err[0] ? err : resource_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Created resource group '%s' for VM '%s'\n" ANSI_RESET, group_id, vm_id);
        printf("Path: /sys/fs/cgroup/virtualos/applications/%s/ | Status: ACTIVE | Verified: YES\n", group_id);
        return 0;
    }

    if (strcmp(subcmd, "set-cpu") == 0) {
        if (argc < 6) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos resource set-cpu <vm_id> <group_id> <cores> [period_us]\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *group_id = argv[4];
        double cores = atof(argv[5]);
        int64_t period = (argc >= 7) ? atoll(argv[6]) : 100000;
        char err[256] = {0};
        ResourceResult res = resource_manager_set_cpu_limit(vm_id, group_id, cores, period, err, sizeof(err));
        if (res != RESOURCE_SUCCESS) {
            fprintf(stderr, "Error setting CPU limit: %s\n", err[0] ? err : resource_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Enforced CPU limit on group '%s'\n" ANSI_RESET, group_id);
        printf("Quota: %lld us | Period: %lld us | Effective Cores: %.2f | OS Readback: VERIFIED\n",
               (long long)(cores * (double)period), (long long)period, cores);
        return 0;
    }

    if (strcmp(subcmd, "set-weight") == 0) {
        if (argc < 6) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos resource set-weight <vm_id> <group_id> <weight>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *group_id = argv[4];
        int weight = atoi(argv[5]);
        char err[256] = {0};
        ResourceResult res = resource_manager_set_cpu_weight(vm_id, group_id, weight, err, sizeof(err));
        if (res != RESOURCE_SUCCESS) {
            fprintf(stderr, "Error setting CPU weight: %s\n", err[0] ? err : resource_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Enforced CPU weight %d on group '%s' (VERIFIED)\n" ANSI_RESET, weight, group_id);
        return 0;
    }

    if (strcmp(subcmd, "set-memory") == 0) {
        if (argc < 6) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos resource set-memory <vm_id> <group_id> <bytes|mb>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *group_id = argv[4];
        uint64_t val = (uint64_t)strtoull(argv[5], NULL, 10);
        if (val > 0 && val < 65536) val *= (1024ULL * 1024ULL); /* treat <=65536 as MB */
        char err[256] = {0};
        ResourceResult res = resource_manager_set_memory_limit(vm_id, group_id, val, err, sizeof(err));
        if (res == RESOURCE_ERR_LIMIT_BELOW_USAGE) {
            fprintf(stderr, ANSI_YELLOW "[REJECTED] %s\n" ANSI_RESET, err);
            return 2;
        } else if (res != RESOURCE_SUCCESS) {
            fprintf(stderr, "Error setting memory limit: %s\n", err[0] ? err : resource_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Enforced memory limit on group '%s'\n" ANSI_RESET, group_id);
        printf("Memory Ceiling: %llu bytes (%.2f MB) | OS Readback: VERIFIED\n",
               (unsigned long long)val, (double)val / (1024.0 * 1024.0));
        return 0;
    }

    if (strcmp(subcmd, "set-pids") == 0) {
        if (argc < 6) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos resource set-pids <vm_id> <group_id> <max_pids>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *group_id = argv[4];
        int64_t pids_max = atoll(argv[5]);
        char err[256] = {0};
        ResourceResult res = resource_manager_set_process_limit(vm_id, group_id, pids_max, err, sizeof(err));
        if (res != RESOURCE_SUCCESS) {
            fprintf(stderr, "Error setting pids limit: %s\n", err[0] ? err : resource_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Enforced process count limit %lld on group '%s' (VERIFIED)\n" ANSI_RESET,
               (long long)pids_max, group_id);
        return 0;
    }

    if (strcmp(subcmd, "set-io") == 0) {
        if (argc < 7) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos resource set-io <vm_id> <group_id> <read_bps> <write_bps>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *group_id = argv[4];
        uint64_t rbps = strtoull(argv[5], NULL, 10);
        uint64_t wbps = strtoull(argv[6], NULL, 10);
        char err[256] = {0};
        ResourceResult res = resource_manager_set_io_limit(vm_id, group_id, rbps, wbps, err, sizeof(err));
        if (res == RESOURCE_ERR_UNSUPPORTED) {
            printf(ANSI_YELLOW "[UNSUPPORTED_BY_GUEST_KERNEL] %s\n" ANSI_RESET, err[0] ? err : "io.max not active");
            return 0;
        } else if (res != RESOURCE_SUCCESS) {
            fprintf(stderr, "Error setting I/O limit: %s\n", err[0] ? err : resource_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Enforced I/O limit on group '%s' (rbps=%llu, wbps=%llu)\n" ANSI_RESET,
               group_id, (unsigned long long)rbps, (unsigned long long)wbps);
        return 0;
    }

    if (strcmp(subcmd, "attach") == 0) {
        if (argc < 6) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos resource attach <vm_id> <group_id> <pid>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *group_id = argv[4];
        pid_t pid = (pid_t)atoi(argv[5]);
        char err[256] = {0};
        ResourceResult res = resource_manager_attach_process(vm_id, group_id, pid, err, sizeof(err));
        if (res != RESOURCE_SUCCESS) {
            fprintf(stderr, "Error attaching PID %d: %s\n", (int)pid, err[0] ? err : resource_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Attached PID %d to group '%s' (VERIFIED in cgroup.procs)\n" ANSI_RESET, (int)pid, group_id);
        return 0;
    }

    if (strcmp(subcmd, "status") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos resource status <vm_id> <group_id>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *group_id = argv[4];
        AgentCgroupStatus st;
        char err[256] = {0};
        ResourceResult res = resource_manager_get_group_status(vm_id, group_id, &st, err, sizeof(err));
        if (res != RESOURCE_SUCCESS) {
            fprintf(stderr, "Error reading group status: %s\n", err[0] ? err : resource_result_str(res));
            return 1;
        }
        printf(ANSI_BOLD "=== Resource Group Status: '%s' (VM '%s') ===" ANSI_RESET "\n", group_id, vm_id);
        printf(" Exists in Cgroupfs    : %s\n", st.exists ? "YES" : "NO");
        printf(" CPU Quota / Period    : %lld us / %lld us (Weight: %d)\n", (long long)st.cpu_quota_us, (long long)st.cpu_period_us, st.cpu_weight);
        printf(" Total CPU Time Used   : %.3f sec (%llu us)\n", (double)st.cpu_usage_usec / 1000000.0, (unsigned long long)st.cpu_usage_usec);
        printf(" Memory Max Ceiling    : %llu bytes (%.2f MB)\n", (unsigned long long)st.memory_max_bytes, (double)st.memory_max_bytes / (1024.0 * 1024.0));
        printf(" Current Memory Usage  : %llu bytes (%.2f MB)\n", (unsigned long long)st.memory_current_bytes, (double)st.memory_current_bytes / (1024.0 * 1024.0));
        printf(" Memory OOM Kill Events: %llu\n", (unsigned long long)st.memory_oom_events);
        printf(" Maximum Allowed PIDs  : %lld\n", (long long)st.pids_max);
        printf(" Current Active PIDs   : %lld\n", (long long)st.pids_current);
        printf(" Attached PIDs Count   : %d\n", st.attached_pids_count);
        if (st.verified) {
            printf(" Verification State    : " ANSI_GREEN "OS_VERIFIED" ANSI_RESET "\n");
        } else {
            printf(" Verification State    : " ANSI_YELLOW "UNVERIFIED / PENDING" ANSI_RESET "\n");
        }
        return 0;
    }

    if (strcmp(subcmd, "list") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: missing vm_id. Usage: virtualos resource list <vm_id>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        char groups[64][64];
        int count = 0;
        ResourceResult res = resource_manager_list_groups(vm_id, groups, 64, &count);
        if (res != RESOURCE_SUCCESS) {
            fprintf(stderr, "Error listing groups for VM '%s'\n", vm_id);
            return 1;
        }
        printf(ANSI_BOLD "=== Active Resource Groups for VM '%s' (%d total) ===" ANSI_RESET "\n", vm_id, count);
        for (int i = 0; i < count; i++) {
            printf(" [%d] %s\n", i + 1, groups[i]);
        }
        return 0;
    }

    if (strcmp(subcmd, "remove") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos resource remove <vm_id> <group_id>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *group_id = argv[4];
        char err[256] = {0};
        ResourceResult res = resource_manager_remove_group(vm_id, group_id, err, sizeof(err));
        if (res != RESOURCE_SUCCESS) {
            if (res == RESOURCE_ERR_GROUP_BUSY) {
                fprintf(stderr, ANSI_RED "Error removing group '%s': Group is populated with active processes. Stop or migrate processes first.\n" ANSI_RESET, group_id);
            } else {
                fprintf(stderr, "Error removing group '%s': %s\n", group_id, err[0] ? err : resource_result_str(res));
            }
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Resource group '%s' removed from VM '%s'\n" ANSI_RESET, group_id, vm_id);
        return 0;
    }

    if (strcmp(subcmd, "set-hardware") == 0) {
        if (argc < 6) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos resource set-hardware <vm_id> <vcpus> <ram_mb>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        int vcpus = atoi(argv[4]);
        int ram_mb = atoi(argv[5]);
        int req_restart = 0;
        char err[256] = {0};
        ResourceResult res = resource_manager_set_vm_hardware(vm_id, vcpus, ram_mb, &req_restart, err, sizeof(err));
        if (res != RESOURCE_SUCCESS) {
            fprintf(stderr, "Error updating VM hardware: %s\n", err[0] ? err : resource_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Updated VM '%s' Hardware Configuration:\n" ANSI_RESET, vm_id);
        printf(" Allocated vCPUs : %d (Configured)\n", vcpus);
        printf(" Configured RAM  : %d MB (Configured)\n", ram_mb);
        printf(" Status          : SAVED TO DATABASE\n");
        if (req_restart) {
            printf(ANSI_YELLOW " Notice          : VM restart required for hypervisor to apply hardware boundaries.\n" ANSI_RESET);
        }
        return 0;
    }

    print_resource_help();
    return 1;
}

/* ------------------------------------------------------------------ Application Management CLI Dispatch */

static void print_app_help(void)
{
    printf(ANSI_BOLD "Virtual OS Simulator - Application Management CLI" ANSI_RESET "\n");
    printf("Usage: virtualos app <subcommand> [options]\n\n");
    printf("Subcommands:\n");
    printf("  discover <vm_id>                                   Discover in-guest applications (/bin, /usr/bin, /usr/local/bin, /opt)\n");
    printf("  register <vm_id> <app_id> <name> <path> [cat] [prof] Register an application for target VM in SQLite\n");
    printf("  list <vm_id> [category]                            List registered applications for VM\n");
    printf("  info <vm_id> <app_id>                              Display full metadata for registered application\n");
    printf("  unregister <vm_id> <app_id>                        Unregister application from SQLite (preserves guest files)\n");
    printf("  enable <vm_id> <app_id>                            Enable an application for execution\n");
    printf("  disable <vm_id> <app_id>                           Disable an application from execution\n");
    printf("  profile-create <id> <name> <cores> <mb> <pids> [io] Create a reusable resource profile in SQLite\n");
    printf("  profile-list                                       List all available resource profiles\n");
    printf("  profile-assign <vm_id> <app_id> <profile_id>       Assign resource profile to registered application\n");
    printf("  launch <vm_id> <app_id> [args...]                  Launch app inside VM with verified pre-exec cgroup bind\n");
    printf("  stop <vm_id> <instance_id> [--force]               Stop running instance (SIGTERM -> SIGKILL)\n");
    printf("  status <vm_id> <instance_id>                       Query state, PID, start time, and exit status\n");
    printf("  metrics <vm_id> <instance_id>                      Collect live cgroup metrics (CPU, RAM, PIDs, I/O)\n");
    printf("  ps <vm_id>                                         List active application instances for VM\n");
    printf("  reconcile <vm_id>                                  Reconcile stale instances against actual in-guest PIDs\n");
}

static int cli_handle_app_command(int argc, char **argv)
{
    if (argc < 3) {
        print_app_help();
        return 1;
    }

    const char *subcmd = argv[2];
    if (strcmp(subcmd, "help") == 0 || strcmp(subcmd, "--help") == 0) {
        print_app_help();
        return 0;
    }
    app_manager_init(NULL);

    if (strcmp(subcmd, "discover") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: missing vm_id. Usage: virtualos app discover <vm_id>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        AgentAppDiscoveryList discovery;
        char err[256] = {0};
        AppResult res = app_manager_discover_apps(vm_id, &discovery, err, sizeof(err));
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error discovering applications for VM '%s': %s\n", vm_id, err[0] ? err : app_result_str(res));
            return 1;
        }
        printf(ANSI_BOLD "=== Discovered Applications in VM '%s' (%d total) ===" ANSI_RESET "\n", vm_id, discovery.count);
        printf("%-4s %-20s %-16s %s\n", "#", "Application Name", "Package", "Executable Path");
        printf("----------------------------------------------------------------------\n");
        for (int i = 0; i < discovery.count; i++) {
            printf("[%2d] %-20s %-16s %s\n", i + 1,
                   discovery.apps[i].name,
                   discovery.apps[i].package,
                   discovery.apps[i].path);
        }
        return 0;
    }

    if (strcmp(subcmd, "register") == 0) {
        if (argc < 7) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos app register <vm_id> <app_id> <name> <path> [category] [profile_id]\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *app_id = argv[4];
        const char *name = argv[5];
        const char *path = argv[6];
        const char *cat = (argc > 7) ? argv[7] : "System Utility";
        const char *prof = (argc > 8) ? argv[8] : "default";
        char err[256] = {0};
        AppResult res = app_manager_register_app(vm_id, app_id, name, path, "", cat, "1.0", prof, err, sizeof(err));
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error registering application '%s': %s\n", app_id, err[0] ? err : app_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Registered application '%s' for VM '%s'\n" ANSI_RESET, app_id, vm_id);
        printf("  Name     : %s\n", name);
        printf("  Path     : %s\n", path);
        printf("  Category : %s\n", cat);
        printf("  Profile  : %s\n", prof);
        return 0;
    }

    if (strcmp(subcmd, "list") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: missing vm_id. Usage: virtualos app list <vm_id> [category]\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *cat = (argc > 4) ? argv[4] : NULL;
        ApplicationEntry apps[64];
        int count = 0;
        AppResult res = app_manager_list_apps(vm_id, cat, apps, 64, &count);
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error listing applications for VM '%s': %s\n", vm_id, app_result_str(res));
            return 1;
        }
        printf(ANSI_BOLD "=== Registered Applications for VM '%s' (%d total) ===" ANSI_RESET "\n", vm_id, count);
        printf("%-16s %-18s %-14s %-12s %-8s %-6s %s\n",
               "APP ID", "NAME", "CATEGORY", "PROFILE", "ENABLED", "LAUNCH", "PATH");
        printf("----------------------------------------------------------------------------------------\n");
        for (int i = 0; i < count; i++) {
            printf("%-16s %-18s %-14s %-12s %-8s %-6d %s\n",
                   apps[i].app_id, apps[i].name, apps[i].category,
                   apps[i].default_profile_id[0] ? apps[i].default_profile_id : "default",
                   apps[i].enabled ? "YES" : "NO",
                   apps[i].launch_count,
                   apps[i].executable_path);
        }
        return 0;
    }

    if (strcmp(subcmd, "info") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos app info <vm_id> <app_id>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *app_id = argv[4];
        ApplicationEntry app;
        char err[256] = {0};
        AppResult res = app_manager_get_app(vm_id, app_id, &app, err, sizeof(err));
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error getting application '%s': %s\n", app_id, err[0] ? err : app_result_str(res));
            return 1;
        }
        printf(ANSI_BOLD "=== Application Metadata: %s ===" ANSI_RESET "\n", app.app_id);
        printf("  App ID          : %s\n", app.app_id);
        printf("  VM ID           : %s\n", app.vm_id);
        printf("  Name            : %s\n", app.name);
        printf("  Executable Path : %s\n", app.executable_path);
        printf("  Category        : %s\n", app.category);
        printf("  Version         : %s\n", app.version);
        printf("  Default Profile : %s\n", app.default_profile_id);
        printf("  Enabled State   : %s\n", app.enabled ? "ENABLED" : "DISABLED");
        printf("  Launch Count    : %d\n", app.launch_count);
        printf("  Registered At   : %llu\n", (unsigned long long)app.registered_at);
        printf("  Last Launched   : %llu\n", (unsigned long long)app.last_launched_at);
        return 0;
    }

    if (strcmp(subcmd, "unregister") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos app unregister <vm_id> <app_id>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *app_id = argv[4];
        char err[256] = {0};
        AppResult res = app_manager_unregister_app(vm_id, app_id, err, sizeof(err));
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error unregistering application '%s': %s\n", app_id, err[0] ? err : app_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Unregistered application '%s' from SQLite database.\n" ANSI_RESET, app_id);
        printf(ANSI_DIM "Notice: Guest binary file remains intact at its original filesystem path.\n" ANSI_RESET);
        return 0;
    }

    if (strcmp(subcmd, "enable") == 0 || strcmp(subcmd, "disable") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos app %s <vm_id> <app_id>\n", subcmd);
            return 1;
        }
        const char *vm_id = argv[3];
        const char *app_id = argv[4];
        int en = (strcmp(subcmd, "enable") == 0) ? 1 : 0;
        char err[256] = {0};
        AppResult res = app_manager_set_enabled(vm_id, app_id, en, err, sizeof(err));
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error setting enabled state: %s\n", err[0] ? err : app_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Application '%s' %s successfully.\n" ANSI_RESET, app_id, en ? "enabled" : "disabled");
        return 0;
    }

    if (strcmp(subcmd, "profile-create") == 0) {
        if (argc < 8) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos app profile-create <profile_id> <name> <cores> <mem_mb> <pids> [io_mb_s]\n");
            return 1;
        }
        const char *pid = argv[3];
        const char *pname = argv[4];
        double cores = atof(argv[5]);
        uint64_t mem_mb = (uint64_t)strtoull(argv[6], NULL, 10);
        int64_t pids = (int64_t)atoll(argv[7]);
        uint64_t io_mb = (argc > 8) ? (uint64_t)strtoull(argv[8], NULL, 10) : 0;
        char err[256] = {0};
        AppResult res = app_manager_create_profile(pid, pname, "", cores, 100, mem_mb * 1024ULL * 1024ULL, pids, io_mb * 1024ULL * 1024ULL, io_mb * 1024ULL * 1024ULL, err, sizeof(err));
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error creating profile '%s': %s\n", pid, err[0] ? err : app_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Created application resource profile '%s'\n" ANSI_RESET, pid);
        printf("  Name     : %s\n", pname);
        printf("  CPU Cores: %.2f\n", cores);
        printf("  Max RAM  : %llu MB\n", (unsigned long long)mem_mb);
        printf("  Max PIDs : %lld\n", (long long)pids);
        printf("  Disk I/O : %llu MB/s\n", (unsigned long long)io_mb);
        return 0;
    }

    if (strcmp(subcmd, "profile-list") == 0) {
        ApplicationProfile profiles[32];
        int count = 0;
        AppResult res = app_manager_list_profiles(profiles, 32, &count);
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error listing profiles: %s\n", app_result_str(res));
            return 1;
        }
        printf(ANSI_BOLD "=== Available Resource Profiles (%d total) ===" ANSI_RESET "\n", count);
        printf("%-16s %-18s %-8s %-8s %-12s %-10s %s\n", "PROFILE ID", "NAME", "CORES", "WEIGHT", "MAX MEM(MB)", "MAX PIDS", "DISK I/O");
        printf("--------------------------------------------------------------------------------------------\n");
        for (int i = 0; i < count; i++) {
            printf("%-16s %-18s %-8.2f %-8d %-12llu %-10lld %llu MB/s\n",
                   profiles[i].profile_id, profiles[i].name,
                   profiles[i].cpu_cores, profiles[i].cpu_weight,
                   (unsigned long long)(profiles[i].memory_max_bytes / (1024 * 1024)),
                   (long long)profiles[i].pids_max,
                   (unsigned long long)(profiles[i].io_read_bps / (1024 * 1024)));
        }
        return 0;
    }

    if (strcmp(subcmd, "profile-assign") == 0) {
        if (argc < 6) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos app profile-assign <vm_id> <app_id> <profile_id>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *app_id = argv[4];
        const char *profile_id = argv[5];
        char err[256] = {0};
        AppResult res = app_manager_assign_profile(vm_id, app_id, profile_id, err, sizeof(err));
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error assigning profile: %s\n", err[0] ? err : app_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Assigned profile '%s' to application '%s' in VM '%s'\n" ANSI_RESET, profile_id, app_id, vm_id);
        return 0;
    }

    if (strcmp(subcmd, "launch") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos app launch <vm_id> <app_id> [args...]\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *app_id = argv[4];
        char args_buf[512] = {0};
        for (int i = 5; i < argc; i++) {
            if (i > 5) strncat(args_buf, " ", sizeof(args_buf) - strlen(args_buf) - 1);
            strncat(args_buf, argv[i], sizeof(args_buf) - strlen(args_buf) - 1);
        }
        char out_instance_id[96] = {0};
        char err[256] = {0};
        AppResult res = app_manager_launch_app(vm_id, app_id, args_buf[0] ? args_buf : NULL, NULL, out_instance_id, sizeof(out_instance_id), err, sizeof(err));
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error launching application '%s': %s\n", app_id, err[0] ? err : app_result_str(res));
            return 1;
        }
        ApplicationInstance inst;
        app_manager_get_instance(out_instance_id, &inst, NULL, 0);
        printf(ANSI_GREEN "[SUCCESS] Application '%s' launched successfully in VM '%s'\n" ANSI_RESET, app_id, vm_id);
        printf("  Instance ID  : %s\n", inst.instance_id);
        printf("  Guest PID    : %d\n", (int)inst.guest_pid);
        printf("  Start Time   : %llu (procfs verified)\n", (unsigned long long)inst.process_start_time);
        printf("  Cgroup Path  : %s (pre-execution attached)\n", inst.cgroup_path);
        printf("  Profile      : %s\n", inst.profile_id);
        printf("  Status       : %s\n", app_state_name(inst.state));
        return 0;
    }

    if (strcmp(subcmd, "stop") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos app stop <vm_id> <instance_id> [--force]\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *instance_id = argv[4];
        int force = (argc > 5 && strcmp(argv[5], "--force") == 0) ? 1 : 0;
        char err[256] = {0};
        AppResult res = app_manager_stop_app(vm_id, instance_id, 3000, force, err, sizeof(err));
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error stopping instance '%s': %s\n", instance_id, err[0] ? err : app_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Stopped application instance '%s' in VM '%s'\n" ANSI_RESET, instance_id, vm_id);
        return 0;
    }

    if (strcmp(subcmd, "status") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos app status <vm_id> <instance_id>\n");
            return 1;
        }
        const char *instance_id = argv[4];
        ApplicationInstance inst;
        char err[256] = {0};
        AppResult res = app_manager_get_instance(instance_id, &inst, err, sizeof(err));
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error querying instance '%s': %s\n", instance_id, err[0] ? err : app_result_str(res));
            return 1;
        }
        printf(ANSI_BOLD "=== Application Instance Status ===" ANSI_RESET "\n");
        printf("  Instance ID  : %s\n", inst.instance_id);
        printf("  App ID       : %s\n", inst.app_id);
        printf("  VM ID        : %s\n", inst.vm_id);
        printf("  State        : %s\n", app_state_name(inst.state));
        printf("  Guest PID    : %d\n", (int)inst.guest_pid);
        printf("  Start Time   : %llu\n", (unsigned long long)inst.process_start_time);
        printf("  Cgroup Path  : %s\n", inst.cgroup_path);
        printf("  Profile      : %s\n", inst.profile_id);
        printf("  Started At   : %llu\n", (unsigned long long)inst.started_at);
        printf("  Stopped At   : %llu\n", (unsigned long long)inst.stopped_at);
        printf("  Exit Code    : %d\n", inst.exit_code);
        if (inst.failure_reason[0]) {
            printf("  Failure      : %s\n", inst.failure_reason);
        }
        return 0;
    }

    if (strcmp(subcmd, "metrics") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: missing parameters. Usage: virtualos app metrics <vm_id> <instance_id>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        const char *instance_id = argv[4];
        ApplicationResourceSample sample;
        char err[256] = {0};
        AppResult res = app_manager_collect_metrics(vm_id, instance_id, &sample, err, sizeof(err));
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error collecting metrics for instance '%s': %s\n", instance_id, err[0] ? err : app_result_str(res));
            return 1;
        }
        printf(ANSI_BOLD "=== Live Application Resource Metrics ===" ANSI_RESET "\n");
        printf("  Instance ID     : %s\n", sample.instance_id);
        printf("  Application ID  : %s\n", sample.app_id);
        printf("  VM ID           : %s\n", sample.vm_id);
        printf("  CPU Usage       : %5.1f %%\n", sample.cpu_usage_pct);
        printf("  CPU Time Total  : %llu us\n", (unsigned long long)sample.cpu_time_usec);
        printf("  CPU Throttled   : %llu us\n", (unsigned long long)sample.throttled_usec);
        printf("  Memory Current  : %6.2f MB\n", (double)sample.memory_current_bytes / (1024.0 * 1024.0));
        printf("  Memory Peak     : %6.2f MB\n", (double)sample.memory_peak_bytes / (1024.0 * 1024.0));
        printf("  Active PIDs     : %lld / %lld\n", (long long)sample.pids_current, (long long)sample.pids_max);
        printf("  Disk I/O Read   : %6.2f KB\n", (double)sample.io_read_bytes / 1024.0);
        printf("  Disk I/O Write  : %6.2f KB\n", (double)sample.io_write_bytes / 1024.0);
        printf("  Telemetry State : %s (%s)\n", sample.freshness, sample.verification_state);
        return 0;
    }

    if (strcmp(subcmd, "ps") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: missing vm_id. Usage: virtualos app ps <vm_id>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        ApplicationInstance instances[64];
        int count = 0;
        AppResult res = app_manager_list_instances(vm_id, (AppState)-1, instances, 64, &count);
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error listing instances: %s\n", app_result_str(res));
            return 1;
        }
        printf(ANSI_BOLD "=== Application Instances for VM '%s' (%d total) ===" ANSI_RESET "\n", vm_id, count);
        printf("%-26s %-16s %-8s %-10s %-14s %s\n", "INSTANCE ID", "APP ID", "PID", "STATE", "PROFILE", "STARTED AT");
        printf("----------------------------------------------------------------------------------------------------\n");
        for (int i = 0; i < count; i++) {
            printf("%-26s %-16s %-8d %-10s %-14s %llu\n",
                   instances[i].instance_id, instances[i].app_id,
                   (int)instances[i].guest_pid, app_state_name(instances[i].state),
                   instances[i].profile_id,
                   (unsigned long long)instances[i].started_at);
        }
        return 0;
    }

    if (strcmp(subcmd, "reconcile") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: missing vm_id. Usage: virtualos app reconcile <vm_id>\n");
            return 1;
        }
        const char *vm_id = argv[3];
        int count = 0;
        AppResult res = app_manager_reconcile_instances(vm_id, &count);
        if (res != APP_SUCCESS) {
            fprintf(stderr, "Error reconciling instances: %s\n", app_result_str(res));
            return 1;
        }
        printf(ANSI_GREEN "[SUCCESS] Reconciled application instances for VM '%s'. Dead instances updated: %d\n" ANSI_RESET, vm_id, count);
        return 0;
    }

    print_app_help();
    return 1;
}

int cli_handle_command(int argc, char **argv)
{
    if (argc < 2) {
        print_resource_help();
        printf("\n");
        print_app_help();
        return 1;
    }

    if (strcmp(argv[1], "app") == 0 || strcmp(argv[1], "--app") == 0) {
        return cli_handle_app_command(argc, argv);
    } else if (strcmp(argv[1], "resource") == 0 || strcmp(argv[1], "--resource") == 0) {
        return cli_handle_resource_command(argc, argv);
    }

    print_resource_help();
    printf("\n");
    print_app_help();
    return 1;
}
