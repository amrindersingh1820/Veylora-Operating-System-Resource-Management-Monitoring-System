#ifndef VOS_RESOURCES_H
#define VOS_RESOURCES_H
#include "types.h"

/* Parses "10, 5, 7, 6" (exactly NRES numbers). Returns 0 on success. */
int res_parse_totals(const char *text, int total[NRES]);

/* Computes available units and Banker's-algorithm safety for the given processes.
   Returns 0 when the input is consistent (out->valid == 1), -1 otherwise (see out->message). */
int res_run(const int total[NRES], const Process *procs, int n, ResResult *out);
#endif
