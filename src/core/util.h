#ifndef VOS_UTIL_H
#define VOS_UTIL_H
#include <stddef.h>

/* Parses "10, 5,7" into out[]. Returns the count, or -1 if the text is not a list of integers. */
int parse_int_list(const char *s, int *out, int max);

/* Resolves the default database path (next to executable, or current dir). */
void get_default_db_path(const char *argv0, char *out, size_t outsz);

#endif
