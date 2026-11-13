/* Shared data structures for the Virtual OS simulator (pure C, no Windows code here). */
#ifndef VOS_TYPES_H
#define VOS_TYPES_H

#define MAX_PROC    64
#define NRES        4          /* number of resource types */
#define MAX_BLOCKS  32
#define MAX_GANTT   16384
#define NAME_LEN    32

extern const char *RES_NAMES[NRES];   /* Printer, Scanner, Disk, Network */

/* ---------- a process as entered by the user ---------- */
typedef struct {
    int  pid;                  /* database id, unique */
    char name[NAME_LEN];
    int  arrival;              /* arrival time */
    int  burst;                /* CPU burst time */
    int  priority;             /* 1 = highest priority */
    int  mem_kb;               /* memory the process needs */
    int  max[NRES];            /* maximum units of each resource it may ask for */
    int  held[NRES];           /* units of each resource it holds right now */
} Process;

/* ---------- CPU scheduling ---------- */
typedef enum {
    ALGO_AUTO = 0, ALGO_FCFS, ALGO_SJF, ALGO_SRTF,
    ALGO_PRIO_NP, ALGO_PRIO_P, ALGO_RR, ALGO_COUNT
} SchedAlgo;

typedef struct {
    Process p;
    int completion, tat, wt, rt;          /* completion, turnaround, waiting, response time */
} ProcResult;

typedef struct { int pid; int start, end; } GanttSeg;   /* pid = -1 means CPU idle */

typedef struct {
    SchedAlgo algo;
    char  algo_name[48];
    int   quantum;                         /* only for Round Robin, else 0 */
    char  reason[256];                     /* why this algorithm was used */
    int   n;
    ProcResult r[MAX_PROC];
    int   ng;
    GanttSeg g[MAX_GANTT];
    int   total_time, busy_time, ctx_switches;
    double avg_wt, avg_tat, avg_rt, cpu_util, throughput;
} SchedResult;

/* ---------- memory ---------- */
typedef enum { MEM_AUTO = 0, MEM_FIRST, MEM_BEST, MEM_WORST, MEM_COUNT } MemStrategy;

typedef struct {
    int  pid; char name[NAME_LEN];
    int  size;            /* KB requested */
    int  block;           /* 1-based block number, 0 if not allocated */
    int  block_size;      /* KB of that block */
    int  frag;            /* wasted KB inside the block (internal fragmentation) */
    int  allocated;       /* 1 = placed, 0 = no block was large enough */
} MemAlloc;

typedef struct {
    MemStrategy strategy;
    char strategy_name[24];
    char reason[200];
    int  nblocks, blocks[MAX_BLOCKS];
    int  n; MemAlloc a[MAX_PROC];
    int  total_mem, used, internal_frag, free_kb, failed;
} MemResult;

/* ---------- resources / deadlock check ---------- */
typedef struct {
    int  valid;                    /* 0 = input inconsistent, see message */
    char message[200];
    int  total[NRES], avail[NRES];
    int  n; int need[MAX_PROC][NRES];
    int  safe;                     /* Banker's algorithm verdict */
    int  seq_len; int seq[MAX_PROC];   /* safe order (indexes into the process list) */
} ResResult;

/* ---------- one complete simulation run (what gets saved) ---------- */
typedef struct {
    int  run_id;            /* 0 = not saved */
    char created[24];
    SchedResult sched;
    MemResult   mem;
    ResResult   res;
} RunBundle;

#endif
