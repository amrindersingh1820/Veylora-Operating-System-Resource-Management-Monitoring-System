/* host_monitor.c – macOS Apple Silicon Host Resource Monitor
 *
 * Implements direct Mach kernel and sysctl queries for host CPU, memory,
 * and target process resource metrics.
 */

#include "host_monitor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>

#if defined(__APPLE__)
#  include <mach/mach.h>
#  include <mach/mach_host.h>
#  include <mach/host_info.h>
#  include <mach/vm_statistics.h>
#  include <sys/sysctl.h>
#  include <libproc.h>
#  include <sys/proc_info.h>
#  include <signal.h>
#elif defined(__linux__)
#  include <sys/sysinfo.h>
#  include <sys/types.h>
#  include <signal.h>
#  include <unistd.h>
#endif

/* ------------------------------------------------------------------ Internal state */

static pthread_mutex_t s_host_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_initialized = 0;

/* Cache for host CPU delta calculation */
#if defined(__APPLE__)
static host_cpu_load_info_data_t s_prev_cpu;
static uint64_t                  s_prev_cpu_time_ms = 0;
static int                       s_has_prev_cpu = 0;
#elif defined(__linux__)
static unsigned long long        s_prev_user = 0, s_prev_nice = 0, s_prev_sys = 0, s_prev_idle = 0;
static int                       s_has_prev_cpu = 0;
#endif

/* Process CPU cache */
#define MAX_PROC_CACHE 64
typedef struct {
    pid_t    pid;
    uint64_t last_time_ms;
    uint64_t last_cpu_ns;
    int      in_use;
} ProcCacheEntry;

static ProcCacheEntry s_proc_cache[MAX_PROC_CACHE];

/* ------------------------------------------------------------------ Helpers */

uint64_t host_monitor_now_ms(void)
{
    struct timespec ts;
#if defined(CLOCK_REALTIME)
    clock_gettime(CLOCK_REALTIME, &ts);
#else
    clock_gettime(0, &ts);
#endif
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

/* ------------------------------------------------------------------ Init / Shutdown */

int host_monitor_init(void)
{
    pthread_mutex_lock(&s_host_lock);
    memset(s_proc_cache, 0, sizeof(s_proc_cache));
#if defined(__APPLE__)
    s_has_prev_cpu = 0;
    s_prev_cpu_time_ms = 0;
#elif defined(__linux__)
    s_has_prev_cpu = 0;
#endif
    s_initialized = 1;
    pthread_mutex_unlock(&s_host_lock);
    return 0;
}

void host_monitor_shutdown(void)
{
    pthread_mutex_lock(&s_host_lock);
    s_initialized = 0;
    pthread_mutex_unlock(&s_host_lock);
}

/* ------------------------------------------------------------------ Host Sample */

int host_monitor_sample(HostMetrics *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->timestamp_ms = host_monitor_now_ms();

#if defined(__APPLE__)
    /* 1. Core count and architecture via sysctl */
    int ncpu = 1;
    size_t len = sizeof(ncpu);
    if (sysctlbyname("hw.ncpu", &ncpu, &len, NULL, 0) == 0) {
        out->cpu_core_count = ncpu;
    } else {
        out->cpu_core_count = 1;
    }

    len = sizeof(out->cpu_arch) - 1;
    if (sysctlbyname("hw.machine", out->cpu_arch, &len, NULL, 0) != 0) {
        snprintf(out->cpu_arch, sizeof(out->cpu_arch), "arm64");
    }

    len = sizeof(out->host_model) - 1;
    if (sysctlbyname("hw.model", out->host_model, &len, NULL, 0) != 0) {
        snprintf(out->host_model, sizeof(out->host_model), "Apple Mac");
    }

    /* 2. Physical Memory via sysctl and Mach VM statistics */
    uint64_t memsize = 0;
    len = sizeof(memsize);
    if (sysctlbyname("hw.memsize", &memsize, &len, NULL, 0) == 0) {
        out->memory_total_bytes = memsize;
    }

    vm_size_t page_size = 4096;
    host_page_size(mach_host_self(), &page_size);

    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t vm_count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm_stat, &vm_count) == KERN_SUCCESS) {
        out->memory_free_bytes       = (uint64_t)vm_stat.free_count * page_size;
        out->memory_wire_bytes       = (uint64_t)vm_stat.wire_count * page_size;
        out->memory_compressed_bytes = (uint64_t)vm_stat.compressor_page_count * page_size;

        /* Used = wired + active + compressed */
        out->memory_used_bytes       = ((uint64_t)vm_stat.active_count +
                                        (uint64_t)vm_stat.wire_count +
                                        (uint64_t)vm_stat.compressor_page_count) * page_size;

        /* Available = free + inactive + purgeable */
        out->memory_available_bytes  = ((uint64_t)vm_stat.free_count +
                                        (uint64_t)vm_stat.inactive_count +
                                        (uint64_t)vm_stat.purgeable_count) * page_size;
    }

    /* Memory pressure level from sysctl */
    int pressure = -1;
    len = sizeof(pressure);
    if (sysctlbyname("kern.memorystatus_vm_pressure_level", &pressure, &len, NULL, 0) == 0) {
        out->memory_pressure_level = pressure;
    } else {
        out->memory_pressure_level = -1;
    }

    /* 3. Host CPU ticks & utilization */
    pthread_mutex_lock(&s_host_lock);

    host_cpu_load_info_data_t cpu_info;
    mach_msg_type_number_t cpu_count_mach = HOST_CPU_LOAD_INFO_COUNT;
    kern_return_t kr = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                                       (host_info_t)&cpu_info, &cpu_count_mach);

    if (kr == KERN_SUCCESS) {
        out->cpu_user_ticks   = cpu_info.cpu_ticks[CPU_STATE_USER];
        out->cpu_system_ticks = cpu_info.cpu_ticks[CPU_STATE_SYSTEM];
        out->cpu_idle_ticks   = cpu_info.cpu_ticks[CPU_STATE_IDLE];
        out->cpu_nice_ticks   = cpu_info.cpu_ticks[CPU_STATE_NICE];

        if (s_has_prev_cpu) {
            uint64_t u_delta = (cpu_info.cpu_ticks[CPU_STATE_USER]   >= s_prev_cpu.cpu_ticks[CPU_STATE_USER]) ?
                               (cpu_info.cpu_ticks[CPU_STATE_USER]   -  s_prev_cpu.cpu_ticks[CPU_STATE_USER]) : 0;
            uint64_t s_delta = (cpu_info.cpu_ticks[CPU_STATE_SYSTEM] >= s_prev_cpu.cpu_ticks[CPU_STATE_SYSTEM]) ?
                               (cpu_info.cpu_ticks[CPU_STATE_SYSTEM] -  s_prev_cpu.cpu_ticks[CPU_STATE_SYSTEM]) : 0;
            uint64_t i_delta = (cpu_info.cpu_ticks[CPU_STATE_IDLE]   >= s_prev_cpu.cpu_ticks[CPU_STATE_IDLE]) ?
                               (cpu_info.cpu_ticks[CPU_STATE_IDLE]   -  s_prev_cpu.cpu_ticks[CPU_STATE_IDLE]) : 0;
            uint64_t n_delta = (cpu_info.cpu_ticks[CPU_STATE_NICE]   >= s_prev_cpu.cpu_ticks[CPU_STATE_NICE]) ?
                               (cpu_info.cpu_ticks[CPU_STATE_NICE]   -  s_prev_cpu.cpu_ticks[CPU_STATE_NICE]) : 0;

            uint64_t total_delta = u_delta + s_delta + i_delta + n_delta;
            if (total_delta > 0) {
                out->cpu_user_percent   = ((double)u_delta / (double)total_delta) * 100.0;
                out->cpu_system_percent = ((double)s_delta / (double)total_delta) * 100.0;
                out->cpu_idle_percent   = ((double)i_delta / (double)total_delta) * 100.0;
                out->cpu_total_percent  = 100.0 - out->cpu_idle_percent;
                if (out->cpu_total_percent < 0.0) out->cpu_total_percent = 0.0;
                if (out->cpu_total_percent > 100.0) out->cpu_total_percent = 100.0;
            }
        } else {
            /* First sample baseline */
            uint64_t total = out->cpu_user_ticks + out->cpu_system_ticks + out->cpu_idle_ticks + out->cpu_nice_ticks;
            if (total > 0) {
                out->cpu_idle_percent   = ((double)out->cpu_idle_ticks / (double)total) * 100.0;
                out->cpu_total_percent  = 100.0 - out->cpu_idle_percent;
            }
        }
        s_prev_cpu = cpu_info;
        s_prev_cpu_time_ms = out->timestamp_ms;
        s_has_prev_cpu = 1;
    }
    pthread_mutex_unlock(&s_host_lock);
    return 0;

#elif defined(__linux__)
    /* Linux fallback */
    out->cpu_core_count = sysconf(_SC_NPROCESSORS_ONLN);
    snprintf(out->cpu_arch, sizeof(out->cpu_arch), "arm64");
    snprintf(out->host_model, sizeof(out->host_model), "Linux Host");

    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        out->memory_total_bytes     = (uint64_t)si.totalram * si.mem_unit;
        out->memory_free_bytes      = (uint64_t)si.freeram * si.mem_unit;
        out->memory_used_bytes      = out->memory_total_bytes - out->memory_free_bytes;
        out->memory_available_bytes = out->memory_free_bytes;
    }

    FILE *fp = fopen("/proc/stat", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            unsigned long long u, n, s, i;
            if (sscanf(line, "cpu %llu %llu %llu %llu", &u, &n, &s, &i) == 4) {
                out->cpu_user_ticks   = u;
                out->cpu_nice_ticks   = n;
                out->cpu_system_ticks = s;
                out->cpu_idle_ticks   = i;
                if (s_has_prev_cpu) {
                    unsigned long long total_delta = (u - s_prev_user) + (n - s_prev_nice) +
                                                     (s - s_prev_sys) + (i - s_prev_idle);
                    if (total_delta > 0) {
                        out->cpu_total_percent = 100.0 * (double)((u - s_prev_user) + (s - s_prev_sys)) / (double)total_delta;
                        out->cpu_idle_percent  = 100.0 * (double)(i - s_prev_idle) / (double)total_delta;
                    }
                }
                s_prev_user = u; s_prev_nice = n; s_prev_sys = s; s_prev_idle = i;
                s_has_prev_cpu = 1;
            }
        }
        fclose(fp);
    }
    return 0;
#else
    snprintf(out->cpu_arch, sizeof(out->cpu_arch), "unknown");
    out->cpu_core_count = 1;
    return 0;
#endif
}

/* ------------------------------------------------------------------ Process Sample */

int host_monitor_sample_process(pid_t pid, ProcessHostMetrics *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->pid = pid;
    out->timestamp_ms = host_monitor_now_ms();

    if (pid <= 0) {
        out->is_alive = 0;
        snprintf(out->state, sizeof(out->state), "dead");
        return 0;
    }

#if defined(__APPLE__)
    /* Check if process exists */
    if (kill(pid, 0) != 0 && errno == ESRCH) {
        out->is_alive = 0;
        snprintf(out->state, sizeof(out->state), "dead");
        return 0;
    }

    struct proc_taskinfo pti;
    int sz = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &pti, sizeof(pti));
    if (sz != (int)sizeof(pti)) {
        /* Process died between kill() and proc_pidinfo() or permission denied */
        out->is_alive = 0;
        snprintf(out->state, sizeof(out->state), "dead");
        return 0;
    }

    out->is_alive              = 1;
    out->cpu_user_ns           = pti.pti_total_user;
    out->cpu_system_ns         = pti.pti_total_system;
    out->memory_rss_bytes      = (uint64_t)pti.pti_resident_size;
    out->memory_virtual_bytes  = (uint64_t)pti.pti_virtual_size;

    /* Process state from BSD info */
    struct proc_bsdinfo pbi;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &pbi, sizeof(pbi)) == (int)sizeof(pbi)) {
        switch (pbi.pbi_status) {
            case SIDL:   snprintf(out->state, sizeof(out->state), "idle"); break;
            case SRUN:   snprintf(out->state, sizeof(out->state), "running"); break;
            case SSLEEP: snprintf(out->state, sizeof(out->state), "sleeping"); break;
            case SSTOP:  snprintf(out->state, sizeof(out->state), "stopped"); break;
            case SZOMB:  snprintf(out->state, sizeof(out->state), "zombie"); break;
            default:     snprintf(out->state, sizeof(out->state), "running"); break;
        }
    } else {
        snprintf(out->state, sizeof(out->state), "running");
    }

    /* Compute delta CPU % using cache */
    pthread_mutex_lock(&s_host_lock);

    uint64_t now_ms = out->timestamp_ms;
    uint64_t total_cpu_ns = out->cpu_user_ns + out->cpu_system_ns;
    double calc_cpu = 0.0;

    int slot = -1;
    for (int i = 0; i < MAX_PROC_CACHE; i++) {
        if (s_proc_cache[i].in_use && s_proc_cache[i].pid == pid) {
            slot = i;
            break;
        }
    }

    if (slot >= 0) {
        uint64_t dt_ms = (now_ms > s_proc_cache[slot].last_time_ms) ?
                         (now_ms - s_proc_cache[slot].last_time_ms) : 0;
        uint64_t d_cpu_ns = (total_cpu_ns >= s_proc_cache[slot].last_cpu_ns) ?
                            (total_cpu_ns - s_proc_cache[slot].last_cpu_ns) : 0;

        if (dt_ms > 0) {
            uint64_t dt_ns = dt_ms * 1000000ULL;
            calc_cpu = ((double)d_cpu_ns / (double)dt_ns) * 100.0;
            if (calc_cpu < 0.0) calc_cpu = 0.0;
        }
        s_proc_cache[slot].last_time_ms = now_ms;
        s_proc_cache[slot].last_cpu_ns  = total_cpu_ns;
    } else {
        /* Allocate a slot */
        for (int i = 0; i < MAX_PROC_CACHE; i++) {
            if (!s_proc_cache[i].in_use) {
                slot = i;
                break;
            }
        }
        if (slot >= 0) {
            s_proc_cache[slot].pid          = pid;
            s_proc_cache[slot].last_time_ms = now_ms;
            s_proc_cache[slot].last_cpu_ns  = total_cpu_ns;
            s_proc_cache[slot].in_use       = 1;
        }
    }
    pthread_mutex_unlock(&s_host_lock);

    out->cpu_percent = calc_cpu;
    return 0;

#elif defined(__linux__)
    if (kill(pid, 0) != 0) {
        out->is_alive = 0;
        snprintf(out->state, sizeof(out->state), "dead");
        return 0;
    }
    out->is_alive = 1;
    snprintf(out->state, sizeof(out->state), "running");
    return 0;
#else
    out->is_alive = 0;
    return 0;
#endif
}

