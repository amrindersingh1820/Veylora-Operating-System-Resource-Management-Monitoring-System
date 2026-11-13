/* cpu_monitor.c – Guest CPU Metric Collection
 *
 * Collects total and per-core CPU utilization strictly from Linux /proc/stat.
 * Computes delta ticks across sampling intervals.
 * Handles counter resets, first-sample state, and procfs absence cleanly.
 * Represents initial uncomputed state with interval_ms = 0.
 * Strictly avoids any host OS or Mach CPU calls.
 */

#include "metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

typedef struct {
    uint64_t user;
    uint64_t nice;
    uint64_t system;
    uint64_t idle;
    uint64_t iowait;
    uint64_t irq;
    uint64_t softirq;
    uint64_t steal;
} CpuTicks;

static CpuTicks s_prev_total;
static CpuTicks s_prev_cores[AGENT_MAX_CORES];
static int      s_prev_core_count = 0;
static uint64_t s_prev_time_ms = 0;
static int      s_has_prev = 0;

static uint64_t get_time_ms(void)
{
    struct timespec ts;
#if defined(CLOCK_REALTIME)
    clock_gettime(CLOCK_REALTIME, &ts);
#else
    clock_gettime(0, &ts);
#endif
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

int cpu_monitor_init(void)
{
    memset(&s_prev_total, 0, sizeof(s_prev_total));
    memset(s_prev_cores, 0, sizeof(s_prev_cores));
    s_prev_core_count = 0;
    s_prev_time_ms = 0;
    s_has_prev = 0;
    return 0;
}

void cpu_monitor_shutdown(void)
{
    s_has_prev = 0;
    s_prev_time_ms = 0;
    s_prev_core_count = 0;
    memset(&s_prev_total, 0, sizeof(s_prev_total));
    memset(s_prev_cores, 0, sizeof(s_prev_cores));
}

static uint64_t sum_ticks(const CpuTicks *t)
{
    return t->user + t->nice + t->system + t->idle + t->iowait +
           t->irq + t->softirq + t->steal;
}

static double calc_pct(uint64_t delta, uint64_t total_delta)
{
    if (total_delta == 0) return 0.0;
    double p = ((double)delta / (double)total_delta) * 100.0;
    return (p < 0.0) ? 0.0 : (p > 100.0 ? 100.0 : p);
}

int cpu_monitor_sample(GuestCpuMetrics *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    char stat_path[512];
    snprintf(stat_path, sizeof(stat_path), "%s/stat", agent_get_proc_root());

    FILE *fp = fopen(stat_path, "r");
    if (!fp) {
        /* /proc/stat is unavailable - collector cannot report guest metrics */
        return -1;
    }

    char line[512];
    CpuTicks cur_total = {0};
    CpuTicks cur_cores[AGENT_MAX_CORES] = {{0}};
    int num_cores = 0;
    int found_total = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "cpu ", 4) == 0) {
            sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu",
                   (unsigned long long *)&cur_total.user,
                   (unsigned long long *)&cur_total.nice,
                   (unsigned long long *)&cur_total.system,
                   (unsigned long long *)&cur_total.idle,
                   (unsigned long long *)&cur_total.iowait,
                   (unsigned long long *)&cur_total.irq,
                   (unsigned long long *)&cur_total.softirq,
                   (unsigned long long *)&cur_total.steal);
            found_total = 1;
        } else if (strncmp(line, "cpu", 3) == 0 && line[3] >= '0' && line[3] <= '9') {
            int core_idx = atoi(line + 3);
            if (core_idx >= 0 && core_idx < AGENT_MAX_CORES) {
                /* Find the start of numbers after cpu<id> */
                char *p = line + 3;
                while (*p >= '0' && *p <= '9') p++;
                while (*p == ' ' || *p == '\t') p++;

                sscanf(p, "%llu %llu %llu %llu %llu %llu %llu %llu",
                       (unsigned long long *)&cur_cores[core_idx].user,
                       (unsigned long long *)&cur_cores[core_idx].nice,
                       (unsigned long long *)&cur_cores[core_idx].system,
                       (unsigned long long *)&cur_cores[core_idx].idle,
                       (unsigned long long *)&cur_cores[core_idx].iowait,
                       (unsigned long long *)&cur_cores[core_idx].irq,
                       (unsigned long long *)&cur_cores[core_idx].softirq,
                       (unsigned long long *)&cur_cores[core_idx].steal);
                if (core_idx + 1 > num_cores) {
                    num_cores = core_idx + 1;
                }
            }
        }
    }
    fclose(fp);

    if (!found_total) {
        return -1;
    }

    out->core_count    = num_cores;
    out->user_ticks    = cur_total.user;
    out->nice_ticks    = cur_total.nice;
    out->system_ticks  = cur_total.system;
    out->idle_ticks    = cur_total.idle;
    out->iowait_ticks  = cur_total.iowait;
    out->irq_ticks     = cur_total.irq;
    out->softirq_ticks = cur_total.softirq;

    uint64_t now_ms = get_time_ms();
    uint64_t cur_sum = sum_ticks(&cur_total);

    /* Check for counter reset / system wrap */
    if (s_has_prev) {
        uint64_t prev_sum = sum_ticks(&s_prev_total);
        if (cur_sum < prev_sum || cur_total.idle < s_prev_total.idle) {
            /* Counter reset detected (e.g. reboot or overflow). Reset state. */
            s_has_prev = 0;
        }
    }

    if (s_has_prev) {
        uint64_t prev_sum = sum_ticks(&s_prev_total);
        uint64_t d_total  = cur_sum - prev_sum;

        uint64_t dt_ms = (now_ms > s_prev_time_ms) ? (now_ms - s_prev_time_ms) : 0;
        out->sample_interval_ms = dt_ms;

        if (d_total > 0) {
            uint64_t d_user   = (cur_total.user   >= s_prev_total.user)   ? (cur_total.user   - s_prev_total.user)   : 0;
            uint64_t d_nice   = (cur_total.nice   >= s_prev_total.nice)   ? (cur_total.nice   - s_prev_total.nice)   : 0;
            uint64_t d_sys    = (cur_total.system >= s_prev_total.system) ? (cur_total.system - s_prev_total.system) : 0;
            uint64_t d_idle   = (cur_total.idle   >= s_prev_total.idle)   ? (cur_total.idle   - s_prev_total.idle)   : 0;
            uint64_t d_iowait = (cur_total.iowait >= s_prev_total.iowait) ? (cur_total.iowait - s_prev_total.iowait) : 0;

            out->user_percent   = calc_pct(d_user + d_nice, d_total);
            out->system_percent = calc_pct(d_sys, d_total);
            out->idle_percent   = calc_pct(d_idle, d_total);
            out->iowait_percent = calc_pct(d_iowait, d_total);
            out->total_percent  = 100.0 - out->idle_percent;
            if (out->total_percent < 0.0) out->total_percent = 0.0;
            if (out->total_percent > 100.0) out->total_percent = 100.0;

            for (int c = 0; c < num_cores && c < AGENT_MAX_CORES; c++) {
                uint64_t c_cur  = sum_ticks(&cur_cores[c]);
                uint64_t c_prev = sum_ticks(&s_prev_cores[c]);
                if (c_cur >= c_prev && cur_cores[c].idle >= s_prev_cores[c].idle) {
                    uint64_t c_dt   = c_cur - c_prev;
                    uint64_t c_idle = cur_cores[c].idle - s_prev_cores[c].idle;
                    double c_idle_pct = calc_pct(c_idle, c_dt);
                    out->core_percents[c] = (100.0 >= c_idle_pct) ? (100.0 - c_idle_pct) : 0.0;
                } else {
                    out->core_percents[c] = 0.0;
                }
            }
        } else {
            /* Zero delta: CPU had 0 tick change */
            out->total_percent  = 0.0;
            out->idle_percent   = 100.0;
            out->user_percent   = 0.0;
            out->system_percent = 0.0;
            out->iowait_percent = 0.0;
            for (int c = 0; c < num_cores && c < AGENT_MAX_CORES; c++) {
                out->core_percents[c] = 0.0;
            }
        }
    } else {
        /* First sample condition: initial uncomputed state (interval = 0) */
        out->total_percent      = 0.0;
        out->idle_percent       = 100.0;
        out->user_percent       = 0.0;
        out->system_percent     = 0.0;
        out->iowait_percent     = 0.0;
        out->sample_interval_ms = 0;
        for (int c = 0; c < num_cores && c < AGENT_MAX_CORES; c++) {
            out->core_percents[c] = 0.0;
        }
    }

    s_prev_total = cur_total;
    for (int c = 0; c < num_cores && c < AGENT_MAX_CORES; c++) {
        s_prev_cores[c] = cur_cores[c];
    }
    s_prev_core_count = num_cores;
    s_prev_time_ms    = now_ms;
    s_has_prev        = 1;
    return 0;
}
