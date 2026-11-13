/* memory_monitor.c – Guest Memory Metric Collection
 *
 * Collects memory and swap metrics strictly from Linux /proc/meminfo.
 * Converts kB to bytes and derives accurate available and used memory.
 * Never accesses host macOS memory or sysctl interfaces.
 */

#include "metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int memory_monitor_init(void)
{
    return 0;
}

void memory_monitor_shutdown(void)
{
}

int memory_monitor_sample(GuestMemoryMetrics *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    char meminfo_path[512];
    snprintf(meminfo_path, sizeof(meminfo_path), "%s/meminfo", agent_get_proc_root());

    FILE *fp = fopen(meminfo_path, "r");
    if (!fp) {
        /* /proc/meminfo unavailable - strictly return failure */
        return -1;
    }

    char line[256];
    uint64_t total_kb = 0, free_kb = 0, avail_kb = 0;
    uint64_t buffers_kb = 0, cached_kb = 0, swap_total_kb = 0, swap_free_kb = 0;
    int has_avail = 0;
    int found_total = 0;

    while (fgets(line, sizeof(line), fp)) {
        unsigned long long val = 0;
        if (strncmp(line, "MemTotal:", 9) == 0) {
            if (sscanf(line + 9, "%llu", &val) == 1) {
                total_kb = val;
                found_total = 1;
            }
        } else if (strncmp(line, "MemFree:", 8) == 0) {
            if (sscanf(line + 8, "%llu", &val) == 1) free_kb = val;
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            if (sscanf(line + 13, "%llu", &val) == 1) {
                avail_kb = val;
                has_avail = 1;
            }
        } else if (strncmp(line, "Buffers:", 8) == 0) {
            if (sscanf(line + 8, "%llu", &val) == 1) buffers_kb = val;
        } else if (strncmp(line, "Cached:", 7) == 0) {
            if (sscanf(line + 7, "%llu", &val) == 1) cached_kb = val;
        } else if (strncmp(line, "SwapTotal:", 10) == 0) {
            if (sscanf(line + 10, "%llu", &val) == 1) swap_total_kb = val;
        } else if (strncmp(line, "SwapFree:", 9) == 0) {
            if (sscanf(line + 9, "%llu", &val) == 1) swap_free_kb = val;
        }
    }
    fclose(fp);

    if (!found_total) {
        return -1;
    }

    out->total_bytes       = total_kb * 1024ULL;
    out->free_bytes        = free_kb * 1024ULL;
    out->buffers_bytes     = buffers_kb * 1024ULL;
    out->cached_bytes      = cached_kb * 1024ULL;
    out->swap_total_bytes  = swap_total_kb * 1024ULL;
    out->swap_free_bytes   = swap_free_kb * 1024ULL;
    out->swap_used_bytes   = (swap_total_kb >= swap_free_kb) ?
                             ((swap_total_kb - swap_free_kb) * 1024ULL) : 0;

    if (has_avail) {
        out->available_bytes = avail_kb * 1024ULL;
    } else {
        out->available_bytes = (free_kb + buffers_kb + cached_kb) * 1024ULL;
    }

    if (out->total_bytes >= out->available_bytes) {
        out->used_bytes = out->total_bytes - out->available_bytes;
    } else {
        out->used_bytes = (out->total_bytes >= out->free_bytes) ?
                          (out->total_bytes - out->free_bytes) : 0;
    }

    return 0;
}
