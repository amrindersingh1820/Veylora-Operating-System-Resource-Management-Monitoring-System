/* process_monitor.h – Application and In-Guest Process Monitoring
 *
 * Classifies, filters, and analyzes processes running inside virtual machines.
 * Clearly separates application metrics from guest OS and host QEMU metrics.
 */

#ifndef VOS_PROCESS_MONITOR_H
#define VOS_PROCESS_MONITOR_H

#include "agent_protocol.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_GROUP_ALL = 0,
    APP_GROUP_CREATIVE,    /* Photoshop, Lightroom, Blender, GIMP, Inkscape */
    APP_GROUP_DEVTOOLS,    /* IntelliJ IDEA, PyCharm, CLion, VS Code, compilers */
    APP_GROUP_BROWSER,     /* Firefox, Chromium, Chrome, WebKit */
    APP_GROUP_SYSTEM,      /* init, systemd, sshd, dbus, kernel threads */
    APP_GROUP_OTHER,
    APP_GROUP_COUNT
} AppGroup;

typedef struct {
    char     name_filter[64];      /* Substring match (case-insensitive); empty = all */
    pid_t    pid_filter;           /* Specific PID match; 0 = all */
    AppGroup group_filter;         /* Specific group or APP_GROUP_ALL */
    double   min_cpu_percent;      /* e.g. 0.1% minimum */
    uint64_t min_memory_bytes;     /* e.g. 1048576 (1 MB) minimum */
} ProcessFilter;

/* Classify an executable or process command name into an application category. */
AppGroup process_classify_group(const char *process_name);

/* Return human-readable label for an application category. */
const char *process_group_name(AppGroup group);

/* Filter a list of process metrics according to specified criteria.
 * Returns 0 on success, -1 on invalid argument. */
int process_filter_list(const GuestProcessMetric *in, int in_count,
                        const ProcessFilter *filter,
                        GuestProcessMetric *out, int max_out,
                        int *out_count);

/* Find a process metric by exact PID. Returns 0 if found, -1 if not found. */
int process_find_by_pid(const GuestProcessMetric *in, int in_count,
                        pid_t pid, GuestProcessMetric *out);

/* Find top consuming processes by CPU or memory.
 * by_memory: 0 for CPU ranking, 1 for RSS memory ranking. */
int process_get_top(const GuestProcessMetric *in, int in_count,
                    int by_memory, GuestProcessMetric *out, int max_out,
                    int *out_count);

#ifdef __cplusplus
}
#endif

#endif /* VOS_PROCESS_MONITOR_H */

