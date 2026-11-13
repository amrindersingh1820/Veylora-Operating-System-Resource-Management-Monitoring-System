#ifndef VOS_SCHEDULER_H
#define VOS_SCHEDULER_H
#include <stddef.h>
#include "types.h"

const char *sched_algo_name(SchedAlgo a);    /* full name, e.g. "SRTF (Shortest Remaining Time First)" */
const char *sched_algo_short(SchedAlgo a);   /* short name, e.g. "SRTF" */
const char *sched_algo_info(SchedAlgo a);    /* one-line description */

/* Runs one algorithm (not ALGO_AUTO) on n processes. Returns 0 on success. */
int sched_run(SchedAlgo algo, const Process *p, int n, int quantum, SchedResult *out);

/* Chooses an algorithm from the process set itself: number of processes, spread of
   priorities, spread of burst times and arrival pattern. Writes the explanation to reason. */
SchedAlgo sched_auto_select(const Process *p, int n, int *quantum, char *reason, size_t rsize);
#endif
