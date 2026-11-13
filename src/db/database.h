#ifndef VOS_DATABASE_H
#define VOS_DATABASE_H
#include <stddef.h>
#include "../core/types.h"

typedef struct { int run_id; char created[24]; char algo[48]; int nproc; double avg_wt, avg_tat; } RunSummary;

int  db_open(const char *path);             /* creates the file and tables if needed; 0 = ok */
void db_close(void);
const char *db_error(void);

/* process library (kept between sessions) */
int  db_load_processes(Process *out, int max);   /* returns count */
int  db_save_process(Process *p);                /* pid == 0 inserts and fills p->pid, otherwise updates */
int  db_delete_process(int pid);

/* simulation history */
int  db_save_run(RunBundle *b, const char *mem_blocks, const char *res_totals);  /* returns run id or -1 */
int  db_list_runs(RunSummary *out, int max);
int  db_load_run(int run_id, RunBundle *b, char *mem_blocks, size_t mbsz, char *res_totals, size_t rtsz);
int  db_delete_run(int run_id);

/* small key/value settings */
void db_get_setting(const char *key, char *out, size_t n, const char *def);
void db_set_setting(const char *key, const char *val);
#endif
