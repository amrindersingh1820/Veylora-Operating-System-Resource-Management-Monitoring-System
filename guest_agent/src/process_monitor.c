/* process_monitor.c – Guest Process Metric Collection
 *
 * Enumerates Linux /proc entries to collect in-guest process metrics.
 * Safely parses process names containing spaces and parentheses.
 * Uses /proc/[pid]/stat and /proc/[pid]/status.
 * Calculates delta CPU utilization per process across sampling intervals.
 * Distinguishes total discovered processes from filtered/reported processes.
 * Never accesses host macOS libproc or process tables.
 */

#include "metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>

#define MAX_PROC_TRACK 2048

typedef struct {
    pid_t    pid;
    uint64_t last_ticks;
    uint64_t last_sample_time_ms;
    int      in_use;
} ProcTrackEntry;

static ProcTrackEntry s_proc_track[MAX_PROC_TRACK];
static uint64_t       s_prev_time_ms = 0;

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

int process_monitor_init(void)
{
    memset(s_proc_track, 0, sizeof(s_proc_track));
    s_prev_time_ms = 0;
    return 0;
}

void process_monitor_shutdown(void)
{
    memset(s_proc_track, 0, sizeof(s_proc_track));
    s_prev_time_ms = 0;
}

static int compare_procs(const void *a, const void *b)
{
    const GuestProcessMetric *pa = (const GuestProcessMetric *)a;
    const GuestProcessMetric *pb = (const GuestProcessMetric *)b;
    /* Sort descending by CPU %, then by RSS bytes */
    if (pb->cpu_percent > pa->cpu_percent) return 1;
    if (pb->cpu_percent < pa->cpu_percent) return -1;
    if (pb->memory_rss_bytes > pa->memory_rss_bytes) return 1;
    if (pb->memory_rss_bytes < pa->memory_rss_bytes) return -1;
    return 0;
}

int process_monitor_sample_ex(GuestProcessMetric *out_list, int max_procs, int *out_count,
                              int *out_total_discovered, uint64_t *out_snapshot_time_ms)
{
    if (!out_list || max_procs <= 0 || !out_count) return -1;
    *out_count = 0;
    if (out_total_discovered) *out_total_discovered = 0;

    const char *proc_root = agent_get_proc_root();
    DIR *dir = opendir(proc_root);
    if (!dir) {
        /* /proc directory is not available on host */
        return -1;
    }

    uint64_t now_ms = get_time_ms();
    if (out_snapshot_time_ms) *out_snapshot_time_ms = now_ms;

    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100;
    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 4096;

    struct dirent *ent;
    int count = 0;
    int total_discovered = 0;

    while ((ent = readdir(dir)) != NULL) {
        if (!isdigit((unsigned char)ent->d_name[0])) continue;

        pid_t pid = (pid_t)atoi(ent->d_name);
        if (pid <= 0) continue;

        total_discovered++;

        char stat_path[512];
        snprintf(stat_path, sizeof(stat_path), "%s/%d/stat", proc_root, pid);

        FILE *fp = fopen(stat_path, "r");
        if (!fp) {
            /* Process terminated or permission denied during inspection – safely skip */
            continue;
        }

        char buf[1024];
        if (!fgets(buf, sizeof(buf), fp)) {
            fclose(fp);
            continue;
        }
        fclose(fp);

        /* Parse /proc/[pid]/stat:
         * The process name is enclosed between the FIRST '(' and the LAST ')' */
        char *open_p = strchr(buf, '(');
        char *close_p = strrchr(buf, ')');
        if (!open_p || !close_p || close_p <= open_p) continue;

        GuestProcessMetric m;
        memset(&m, 0, sizeof(m));
        m.pid = pid;

        size_t name_len = (size_t)(close_p - open_p - 1);
        if (name_len >= sizeof(m.name)) name_len = sizeof(m.name) - 1;
        memcpy(m.name, open_p + 1, name_len);
        m.name[name_len] = '\0';

        char state_ch = 'R';
        int ppid = 0;
        unsigned long long utime = 0, stime = 0, starttime = 0, vsize = 0;
        long long rss_pages = 0;

        if (sscanf(close_p + 1, " %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu %*d %*d %*d %*d %*d %*d %llu %llu %lld",
                   &state_ch, &ppid, &utime, &stime, &starttime, &vsize, &rss_pages) >= 7) {
            m.ppid = ppid;
            m.state[0] = state_ch;
            m.state[1] = '\0';
            m.memory_rss_bytes  = (rss_pages > 0) ? ((uint64_t)rss_pages * (uint64_t)page_sz) : 0;
            m.memory_virt_bytes = vsize;
            m.start_time_s      = starttime / (uint64_t)clk_tck;

            /* Check /proc/[pid]/status for more accurate VmRSS / VmSize if available */
            char status_path[512];
            snprintf(status_path, sizeof(status_path), "%s/%d/status", proc_root, pid);
            FILE *sfp = fopen(status_path, "r");
            if (sfp) {
                char sline[256];
                while (fgets(sline, sizeof(sline), sfp)) {
                    unsigned long long kb = 0;
                    if (strncmp(sline, "VmRSS:", 6) == 0) {
                        if (sscanf(sline + 6, "%llu", &kb) == 1) {
                            m.memory_rss_bytes = kb * 1024ULL;
                        }
                    } else if (strncmp(sline, "VmSize:", 7) == 0) {
                        if (sscanf(sline + 7, "%llu", &kb) == 1) {
                            m.memory_virt_bytes = kb * 1024ULL;
                        }
                    }
                }
                fclose(sfp);
            }

            /* Calculate delta CPU % */
            uint64_t cur_ticks = utime + stime;
            double cpu_pct = 0.0;

            int track_idx = -1;
            for (int i = 0; i < MAX_PROC_TRACK; i++) {
                if (s_proc_track[i].in_use && s_proc_track[i].pid == pid) {
                    track_idx = i;
                    break;
                }
            }

            if (track_idx >= 0) {
                uint64_t dt_ms = (now_ms > s_proc_track[track_idx].last_sample_time_ms) ?
                                 (now_ms - s_proc_track[track_idx].last_sample_time_ms) : 0;
                uint64_t dticks = (cur_ticks >= s_proc_track[track_idx].last_ticks) ?
                                  (cur_ticks - s_proc_track[track_idx].last_ticks) : 0;
                if (dt_ms > 0) {
                    cpu_pct = ((double)dticks / (double)clk_tck) / ((double)dt_ms / 1000.0) * 100.0;
                    if (cpu_pct < 0.0) cpu_pct = 0.0;
                }
                s_proc_track[track_idx].last_ticks = cur_ticks;
                s_proc_track[track_idx].last_sample_time_ms = now_ms;
            } else {
                for (int i = 0; i < MAX_PROC_TRACK; i++) {
                    if (!s_proc_track[i].in_use) {
                        track_idx = i;
                        break;
                    }
                }
                if (track_idx >= 0) {
                    s_proc_track[track_idx].pid = pid;
                    s_proc_track[track_idx].last_ticks = cur_ticks;
                    s_proc_track[track_idx].last_sample_time_ms = now_ms;
                    s_proc_track[track_idx].in_use = 1;
                }
            }
            m.cpu_percent = cpu_pct;

            if (count < max_procs) {
                out_list[count++] = m;
            }
        }
    }
    closedir(dir);

    /* Sort descending by CPU and memory */
    qsort(out_list, count, sizeof(GuestProcessMetric), compare_procs);
    *out_count = count;
    if (out_total_discovered) *out_total_discovered = total_discovered;
    s_prev_time_ms = now_ms;
    return 0;
}

int process_monitor_sample(GuestProcessMetric *out_list, int max_procs, int *out_count)
{
    return process_monitor_sample_ex(out_list, max_procs, out_count, NULL, NULL);
}
