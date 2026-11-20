#include "database.h"
#include "sqlite3.h"
#include "../core/util.h"
#include "../core/memory.h"
#include "../core/resources.h"
#include <stdio.h>
#include <string.h>

static sqlite3 *DB;
static char ERR[256];

const char *db_error(void) { return ERR; }

static void set_err(const char *ctx)
{
    snprintf(ERR, sizeof ERR, "%s: %s", ctx, DB ? sqlite3_errmsg(DB) : "database not open");
}

static int run_sql(const char *sql)
{
    char *e = NULL;
    if (sqlite3_exec(DB, sql, NULL, NULL, &e) != SQLITE_OK) {
        snprintf(ERR, sizeof ERR, "%s", e ? e : "sql error");
        sqlite3_free(e);
        return -1;
    }
    return 0;
}

static const char *SCHEMA =
 "PRAGMA foreign_keys=ON;"
 "CREATE TABLE IF NOT EXISTS processes("
 "  pid INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL,"
 "  arrival INTEGER NOT NULL CHECK(arrival>=0), burst INTEGER NOT NULL CHECK(burst>0),"
 "  priority INTEGER NOT NULL CHECK(priority>0), mem_kb INTEGER NOT NULL CHECK(mem_kb>0),"
 "  res_max TEXT NOT NULL, res_held TEXT NOT NULL);"
 "CREATE TABLE IF NOT EXISTS runs("
 "  run_id INTEGER PRIMARY KEY AUTOINCREMENT,"
 "  created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),"
 "  algo_id INTEGER, algorithm TEXT, quantum INTEGER, reason TEXT,"
 "  mem_id INTEGER, mem_strategy TEXT, mem_reason TEXT, mem_blocks TEXT,"
 "  res_totals TEXT, res_safe INTEGER, res_sequence TEXT,"
 "  nproc INTEGER, avg_wt REAL, avg_tat REAL, avg_rt REAL, cpu_util REAL,"
 "  throughput REAL, total_time INTEGER, ctx_switches INTEGER);"
 "CREATE TABLE IF NOT EXISTS run_results("
 "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
 "  run_id INTEGER NOT NULL REFERENCES runs(run_id) ON DELETE CASCADE,"
 "  pid INTEGER, name TEXT, arrival INTEGER, burst INTEGER, priority INTEGER, mem_kb INTEGER,"
 "  res_max TEXT, res_held TEXT, completion INTEGER, tat INTEGER, wt INTEGER, rt INTEGER);"
 "CREATE TABLE IF NOT EXISTS run_gantt("
 "  run_id INTEGER NOT NULL REFERENCES runs(run_id) ON DELETE CASCADE,"
 "  seq INTEGER, pid INTEGER, start_t INTEGER, end_t INTEGER);"
 "CREATE TABLE IF NOT EXISTS run_memory("
 "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
 "  run_id INTEGER NOT NULL REFERENCES runs(run_id) ON DELETE CASCADE,"
 "  pid INTEGER, name TEXT, size_kb INTEGER, block_no INTEGER, block_size INTEGER,"
 "  frag INTEGER, allocated INTEGER);"
 "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT);"
 "CREATE INDEX IF NOT EXISTS idx_results_run ON run_results(run_id);"
 "CREATE INDEX IF NOT EXISTS idx_gantt_run ON run_gantt(run_id);"
 "CREATE INDEX IF NOT EXISTS idx_mem_run ON run_memory(run_id);";

int db_open(const char *path)
{
    if (sqlite3_open(path, &DB) != SQLITE_OK) { set_err("open"); return -1; }
    return run_sql(SCHEMA);
}

void db_close(void)
{
    if (DB) {
        int rc = sqlite3_close(DB);
        if (rc == SQLITE_OK) {
            DB = NULL;
        } else {
            set_err("close");
        }
    }
}

/* "1,2,3,4" <-> int[NRES] */
static void pack_vec(const int *v, char *out, size_t n)
{
    size_t len = 0; out[0] = 0;
    for (int j = 0; j < NRES; j++)
        len += snprintf(out + len, n - len, j ? ",%d" : "%d", v[j]);
}
static void unpack_vec(const char *s, int *v)
{
    memset(v, 0, sizeof(int) * NRES);
    if (s) parse_int_list(s, v, NRES);
}
static const char *col_text(sqlite3_stmt *st, int i)
{
    const unsigned char *t = sqlite3_column_text(st, i);
    return t ? (const char *)t : "";
}
static void copy_str(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src); }

/* ---------------- processes ---------------- */
int db_load_processes(Process *out, int max)
{
    sqlite3_stmt *st; int n = 0;
    if (sqlite3_prepare_v2(DB, "SELECT pid,name,arrival,burst,priority,mem_kb,res_max,res_held FROM processes ORDER BY pid",
                           -1, &st, NULL) != SQLITE_OK) { set_err("load processes"); return -1; }
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        Process *p = &out[n++];
        memset(p, 0, sizeof *p);
        p->pid = sqlite3_column_int(st, 0);
        copy_str(p->name, sizeof p->name, col_text(st, 1));
        p->arrival = sqlite3_column_int(st, 2); p->burst = sqlite3_column_int(st, 3);
        p->priority = sqlite3_column_int(st, 4); p->mem_kb = sqlite3_column_int(st, 5);
        unpack_vec(col_text(st, 6), p->max); unpack_vec(col_text(st, 7), p->held);
    }
    sqlite3_finalize(st);
    return n;
}

int db_save_process(Process *p)
{
    char mx[48], hd[48]; sqlite3_stmt *st;
    pack_vec(p->max, mx, sizeof mx); pack_vec(p->held, hd, sizeof hd);
    const char *sql = p->pid == 0
        ? "INSERT INTO processes(name,arrival,burst,priority,mem_kb,res_max,res_held) VALUES(?,?,?,?,?,?,?)"
        : "UPDATE processes SET name=?,arrival=?,burst=?,priority=?,mem_kb=?,res_max=?,res_held=? WHERE pid=?";
    if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) { set_err("save process"); return -1; }
    sqlite3_bind_text(st, 1, p->name, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, p->arrival); sqlite3_bind_int(st, 3, p->burst);
    sqlite3_bind_int(st, 4, p->priority); sqlite3_bind_int(st, 5, p->mem_kb);
    sqlite3_bind_text(st, 6, mx, -1, SQLITE_STATIC); sqlite3_bind_text(st, 7, hd, -1, SQLITE_STATIC);
    if (p->pid) sqlite3_bind_int(st, 8, p->pid);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) { set_err("save process"); return -1; }
    if (p->pid == 0) p->pid = (int)sqlite3_last_insert_rowid(DB);
    return 0;
}

int db_delete_process(int pid)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, "DELETE FROM processes WHERE pid=?", -1, &st, NULL) != SQLITE_OK) { set_err("delete process"); return -1; }
    sqlite3_bind_int(st, 1, pid);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) { set_err("delete process"); return -1; }
    return 0;
}

/* ---------------- runs ---------------- */
int db_save_run(RunBundle *b, const char *mem_blocks, const char *res_totals)
{
    sqlite3_stmt *st = NULL;
    const SchedResult *s = &b->sched;
    char seqtxt[512] = "";
    for (int k = 0; k < b->res.seq_len; k++) {
        size_t l = strlen(seqtxt);
        snprintf(seqtxt + l, sizeof seqtxt - l, k ? " -> %s" : "%s", s->r[b->res.seq[k]].p.name);
    }
    if (run_sql("BEGIN")) return -1;

    if (sqlite3_prepare_v2(DB,
        "INSERT INTO runs(algo_id,algorithm,quantum,reason,mem_id,mem_strategy,mem_reason,mem_blocks,res_totals,"
        "res_safe,res_sequence,nproc,avg_wt,avg_tat,avg_rt,cpu_util,throughput,total_time,ctx_switches) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", -1, &st, NULL) != SQLITE_OK) goto fail;
    sqlite3_bind_int(st, 1, s->algo);  sqlite3_bind_text(st, 2, s->algo_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 3, s->quantum); sqlite3_bind_text(st, 4, s->reason, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 5, b->mem.strategy); sqlite3_bind_text(st, 6, b->mem.strategy_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 7, b->mem.reason, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 8, mem_blocks, -1, SQLITE_STATIC); sqlite3_bind_text(st, 9, res_totals, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 10, b->res.safe); sqlite3_bind_text(st, 11, seqtxt, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 12, s->n);
    sqlite3_bind_double(st, 13, s->avg_wt); sqlite3_bind_double(st, 14, s->avg_tat); sqlite3_bind_double(st, 15, s->avg_rt);
    sqlite3_bind_double(st, 16, s->cpu_util); sqlite3_bind_double(st, 17, s->throughput);
    sqlite3_bind_int(st, 18, s->total_time); sqlite3_bind_int(st, 19, s->ctx_switches);
    if (sqlite3_step(st) != SQLITE_DONE) goto fail;
    sqlite3_finalize(st); st = NULL;
    int run_id = (int)sqlite3_last_insert_rowid(DB);

    if (sqlite3_prepare_v2(DB,
        "INSERT INTO run_results(run_id,pid,name,arrival,burst,priority,mem_kb,res_max,res_held,completion,tat,wt,rt) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)", -1, &st, NULL) != SQLITE_OK) goto fail;
    for (int i = 0; i < s->n; i++) {
        const ProcResult *r = &s->r[i]; char mx[48], hd[48];
        pack_vec(r->p.max, mx, sizeof mx); pack_vec(r->p.held, hd, sizeof hd);
        sqlite3_reset(st);
        sqlite3_bind_int(st, 1, run_id); sqlite3_bind_int(st, 2, r->p.pid);
        sqlite3_bind_text(st, 3, r->p.name, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 4, r->p.arrival); sqlite3_bind_int(st, 5, r->p.burst);
        sqlite3_bind_int(st, 6, r->p.priority); sqlite3_bind_int(st, 7, r->p.mem_kb);
        sqlite3_bind_text(st, 8, mx, -1, SQLITE_TRANSIENT); sqlite3_bind_text(st, 9, hd, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 10, r->completion); sqlite3_bind_int(st, 11, r->tat);
        sqlite3_bind_int(st, 12, r->wt); sqlite3_bind_int(st, 13, r->rt);
        if (sqlite3_step(st) != SQLITE_DONE) goto fail;
    }
    sqlite3_finalize(st); st = NULL;

    if (sqlite3_prepare_v2(DB, "INSERT INTO run_gantt(run_id,seq,pid,start_t,end_t) VALUES(?,?,?,?,?)", -1, &st, NULL) != SQLITE_OK) goto fail;
    for (int i = 0; i < s->ng; i++) {
        sqlite3_reset(st);
        sqlite3_bind_int(st, 1, run_id); sqlite3_bind_int(st, 2, i); sqlite3_bind_int(st, 3, s->g[i].pid);
        sqlite3_bind_int(st, 4, s->g[i].start); sqlite3_bind_int(st, 5, s->g[i].end);
        if (sqlite3_step(st) != SQLITE_DONE) goto fail;
    }
    sqlite3_finalize(st); st = NULL;

    if (sqlite3_prepare_v2(DB,
        "INSERT INTO run_memory(run_id,pid,name,size_kb,block_no,block_size,frag,allocated) VALUES(?,?,?,?,?,?,?,?)",
        -1, &st, NULL) != SQLITE_OK) goto fail;
    for (int i = 0; i < b->mem.n; i++) {
        const MemAlloc *a = &b->mem.a[i];
        sqlite3_reset(st);
        sqlite3_bind_int(st, 1, run_id); sqlite3_bind_int(st, 2, a->pid);
        sqlite3_bind_text(st, 3, a->name, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 4, a->size); sqlite3_bind_int(st, 5, a->block);
        sqlite3_bind_int(st, 6, a->block_size); sqlite3_bind_int(st, 7, a->frag); sqlite3_bind_int(st, 8, a->allocated);
        if (sqlite3_step(st) != SQLITE_DONE) goto fail;
    }
    sqlite3_finalize(st); st = NULL;
    if (run_sql("COMMIT")) goto fail_nomsg;

    b->run_id = run_id;
    if (sqlite3_prepare_v2(DB, "SELECT created_at FROM runs WHERE run_id=?", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, run_id);
        if (sqlite3_step(st) == SQLITE_ROW) copy_str(b->created, sizeof b->created, col_text(st, 0));
        sqlite3_finalize(st);
    }
    return run_id;
fail:
    set_err("save run");
fail_nomsg:
    if (st) sqlite3_finalize(st);
    sqlite3_exec(DB, "ROLLBACK", NULL, NULL, NULL);
    return -1;
}

int db_list_runs(RunSummary *out, int max)
{
    sqlite3_stmt *st; int n = 0;
    if (sqlite3_prepare_v2(DB, "SELECT run_id,created_at,algorithm,nproc,avg_wt,avg_tat FROM runs ORDER BY run_id DESC",
                           -1, &st, NULL) != SQLITE_OK) { set_err("list runs"); return -1; }
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        RunSummary *r = &out[n++];
        r->run_id = sqlite3_column_int(st, 0);
        copy_str(r->created, sizeof r->created, col_text(st, 1));
        copy_str(r->algo, sizeof r->algo, col_text(st, 2));
        r->nproc = sqlite3_column_int(st, 3);
        r->avg_wt = sqlite3_column_double(st, 4); r->avg_tat = sqlite3_column_double(st, 5);
    }
    sqlite3_finalize(st);
    return n;
}

int db_load_run(int run_id, RunBundle *b, char *mem_blocks, size_t mbsz, char *res_totals, size_t rtsz)
{
    sqlite3_stmt *st; SchedResult *s = &b->sched; MemResult *m = &b->mem;
    memset(b, 0, sizeof *b);
    if (sqlite3_prepare_v2(DB,
        "SELECT created_at,algo_id,algorithm,quantum,reason,mem_id,mem_strategy,mem_reason,mem_blocks,res_totals,"
        "avg_wt,avg_tat,avg_rt,cpu_util,throughput,total_time,ctx_switches FROM runs WHERE run_id=?",
        -1, &st, NULL) != SQLITE_OK) { set_err("load run"); return -1; }
    sqlite3_bind_int(st, 1, run_id);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); snprintf(ERR, sizeof ERR, "Run %d not found.", run_id); return -1; }
    b->run_id = run_id;
    copy_str(b->created, sizeof b->created, col_text(st, 0));
    s->algo = (SchedAlgo)sqlite3_column_int(st, 1);
    copy_str(s->algo_name, sizeof s->algo_name, col_text(st, 2));
    s->quantum = sqlite3_column_int(st, 3);
    copy_str(s->reason, sizeof s->reason, col_text(st, 4));
    m->strategy = (MemStrategy)sqlite3_column_int(st, 5);
    copy_str(m->strategy_name, sizeof m->strategy_name, col_text(st, 6));
    copy_str(m->reason, sizeof m->reason, col_text(st, 7));
    copy_str(mem_blocks, mbsz, col_text(st, 8));
    copy_str(res_totals, rtsz, col_text(st, 9));
    s->avg_wt = sqlite3_column_double(st, 10); s->avg_tat = sqlite3_column_double(st, 11);
    s->avg_rt = sqlite3_column_double(st, 12); s->cpu_util = sqlite3_column_double(st, 13);
    s->throughput = sqlite3_column_double(st, 14); s->total_time = sqlite3_column_int(st, 15);
    s->ctx_switches = sqlite3_column_int(st, 16);
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(DB,
        "SELECT pid,name,arrival,burst,priority,mem_kb,res_max,res_held,completion,tat,wt,rt "
        "FROM run_results WHERE run_id=? ORDER BY id", -1, &st, NULL) != SQLITE_OK) { set_err("load results"); return -1; }
    sqlite3_bind_int(st, 1, run_id);
    while (s->n < MAX_PROC && sqlite3_step(st) == SQLITE_ROW) {
        ProcResult *r = &s->r[s->n++];
        r->p.pid = sqlite3_column_int(st, 0); copy_str(r->p.name, NAME_LEN, col_text(st, 1));
        r->p.arrival = sqlite3_column_int(st, 2); r->p.burst = sqlite3_column_int(st, 3);
        r->p.priority = sqlite3_column_int(st, 4); r->p.mem_kb = sqlite3_column_int(st, 5);
        unpack_vec(col_text(st, 6), r->p.max); unpack_vec(col_text(st, 7), r->p.held);
        r->completion = sqlite3_column_int(st, 8); r->tat = sqlite3_column_int(st, 9);
        r->wt = sqlite3_column_int(st, 10); r->rt = sqlite3_column_int(st, 11);
        s->busy_time += r->p.burst;
    }
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(DB, "SELECT pid,start_t,end_t FROM run_gantt WHERE run_id=? ORDER BY seq", -1, &st, NULL) != SQLITE_OK) { set_err("load gantt"); return -1; }
    sqlite3_bind_int(st, 1, run_id);
    while (s->ng < MAX_GANTT && sqlite3_step(st) == SQLITE_ROW) {
        s->g[s->ng].pid = sqlite3_column_int(st, 0);
        s->g[s->ng].start = sqlite3_column_int(st, 1); s->g[s->ng].end = sqlite3_column_int(st, 2);
        s->ng++;
    }
    sqlite3_finalize(st);

    m->nblocks = mem_parse_blocks(mem_blocks, m->blocks, MAX_BLOCKS);
    if (m->nblocks < 0) m->nblocks = 0;
    if (sqlite3_prepare_v2(DB,
        "SELECT pid,name,size_kb,block_no,block_size,frag,allocated FROM run_memory WHERE run_id=? ORDER BY id",
        -1, &st, NULL) != SQLITE_OK) { set_err("load memory"); return -1; }
    sqlite3_bind_int(st, 1, run_id);
    while (m->n < MAX_PROC && sqlite3_step(st) == SQLITE_ROW) {
        MemAlloc *a = &m->a[m->n++];
        a->pid = sqlite3_column_int(st, 0); copy_str(a->name, NAME_LEN, col_text(st, 1));
        a->size = sqlite3_column_int(st, 2); a->block = sqlite3_column_int(st, 3);
        a->block_size = sqlite3_column_int(st, 4); a->frag = sqlite3_column_int(st, 5);
        a->allocated = sqlite3_column_int(st, 6);
    }
    sqlite3_finalize(st);
    mem_finalize(m);

    /* resource verdict is derived from the stored vectors, so it is rebuilt rather than stored twice */
    int totals[NRES]; Process procs[MAX_PROC];
    for (int i = 0; i < s->n; i++) procs[i] = s->r[i].p;
    if (res_parse_totals(res_totals, totals) == 0) res_run(totals, procs, s->n, &b->res);
    return 0;
}

int db_delete_run(int run_id)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, "DELETE FROM runs WHERE run_id=?", -1, &st, NULL) != SQLITE_OK) { set_err("delete run"); return -1; }
    sqlite3_bind_int(st, 1, run_id);
    int rc = sqlite3_step(st); sqlite3_finalize(st);
    if (rc != SQLITE_DONE) { set_err("delete run"); return -1; }
    return 0;
}

/* ---------------- settings ---------------- */
void db_get_setting(const char *key, char *out, size_t n, const char *def)
{
    sqlite3_stmt *st;
    copy_str(out, n, def);
    if (sqlite3_prepare_v2(DB, "SELECT value FROM settings WHERE key=?", -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) copy_str(out, n, col_text(st, 0));
    sqlite3_finalize(st);
}

void db_set_setting(const char *key, const char *val)
{
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, "INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)", -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC); sqlite3_bind_text(st, 2, val, -1, SQLITE_STATIC);
    sqlite3_step(st); sqlite3_finalize(st);
}
