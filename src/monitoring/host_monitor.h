/* host_monitor.h – macOS Apple Silicon & Host Resource Monitoring
 *
 * Responsibilities:
 *   1. Collect host-wide CPU metrics (total %, user, system, idle, core count, architecture).
 *   2. Collect host-wide memory metrics (total, used, free, available, wired, compressed).
 *   3. Collect target process metrics (QEMU process CPU %, RSS memory, virtual memory, state).
 *   4. Use direct Mach kernel and sysctl APIs on macOS (no shell command execution).
 *   5. Distinguish host-wide metrics from QEMU process and in-guest metrics.
 *
 * Platform: macOS (Apple Silicon ARM64 & Intel), with portable fallbacks.
 */

#ifndef VOS_HOST_MONITOR_H
#define VOS_HOST_MONITOR_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ Structures */

typedef struct {
    /* CPU metrics */
    double   cpu_total_percent;       /* 0.0% - 100.0% overall host CPU */
    double   cpu_user_percent;        /* User-space CPU % */
    double   cpu_system_percent;      /* Kernel / system CPU % */
    double   cpu_idle_percent;        /* Idle CPU % */
    uint64_t cpu_user_ticks;          /* Raw cumulative ticks */
    uint64_t cpu_system_ticks;
    uint64_t cpu_idle_ticks;
    uint64_t cpu_nice_ticks;
    int      cpu_core_count;          /* Physical / logical core count */
    char     cpu_arch[32];            /* "arm64", "x86_64" */
    char     host_model[64];          /* Hardware model name */

    /* Memory metrics */
    uint64_t memory_total_bytes;      /* Total physical RAM */
    uint64_t memory_used_bytes;       /* Used RAM (wire + active + compressed) */
    uint64_t memory_free_bytes;       /* Purely unallocated RAM */
    uint64_t memory_available_bytes;  /* Free + purgeable + inactive */
    uint64_t memory_wire_bytes;       /* Wired / unpageable kernel memory */
    uint64_t memory_compressed_bytes; /* Memory compressor storage */
    int      memory_pressure_level;   /* 0=Normal, 1=Warning, 2=Critical, -1=Unavailable */

    /* Temporal */
    uint64_t timestamp_ms;            /* Milliseconds since Unix epoch */
} HostMetrics;

typedef struct {
    pid_t    pid;                     /* Target process ID */
    int      is_alive;                /* 1 if process is running, 0 if missing/dead */
    double   cpu_percent;             /* Process CPU utilization (0.0% - (cores*100)%) */
    uint64_t cpu_user_ns;             /* Cumulative user CPU time in nanoseconds */
    uint64_t cpu_system_ns;           /* Cumulative system CPU time in nanoseconds */
    uint64_t memory_rss_bytes;        /* Resident Set Size (physical RAM used) */
    uint64_t memory_virtual_bytes;    /* Virtual address space size */
    char     state[16];               /* "running", "sleeping", "stopped", "dead" */
    uint64_t timestamp_ms;            /* Milliseconds since Unix epoch */
} ProcessHostMetrics;

/* ------------------------------------------------------------------ API */

/* Initialise the host monitoring subsystem.
 * Returns 0 on success, -1 on failure. */
int host_monitor_init(void);

/* Release host monitoring state. */
void host_monitor_shutdown(void);

/* Sample current host-wide CPU and memory metrics.
 * Uses Mach host APIs and sysctl on macOS.
 * Successive calls compute delta CPU percentages based on elapsed tick counts.
 * Returns 0 on success, -1 on error. */
int host_monitor_sample(HostMetrics *out);

/* Sample metrics for a specific host process (e.g. QEMU child process).
 * Uses proc_pidinfo(PROC_PIDTASKINFO) on macOS.
 * Calculates delta CPU utilization based on elapsed CPU time since previous sample.
 * If the process has exited or cannot be inspected, sets is_alive=0 and returns 0.
 * Returns 0 on success (or dead process), -1 on unexpected system failure. */
int host_monitor_sample_process(pid_t pid, ProcessHostMetrics *out);

/* Helper to get current epoch timestamp in milliseconds. */
uint64_t host_monitor_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* VOS_HOST_MONITOR_H */

