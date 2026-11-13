/* process_monitor.c – Application and In-Guest Process Monitoring
 *
 * Implements category classification, filtering, and metric queries for
 * processes running inside virtual machines.
 */

#include "process_monitor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static const char *s_group_names[APP_GROUP_COUNT] = {
    "All",
    "Creative",
    "Developer Tools",
    "Web Browsers",
    "System & Daemons",
    "Other"
};

const char *process_group_name(AppGroup group)
{
    if (group >= 0 && group < APP_GROUP_COUNT) {
        return s_group_names[group];
    }
    return "Unknown";
}

static bool str_contains_case_insensitive(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return false;
    if (needle[0] == '\0') return true;

    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return false;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        if (strncasecmp(haystack + i, needle, nlen) == 0) {
            return true;
        }
    }
    return false;
}

AppGroup process_classify_group(const char *name)
{
    if (!name || name[0] == '\0') return APP_GROUP_OTHER;

    /* Creative applications */
    static const char *creative_sigs[] = {
        "photoshop", "lightroom", "blender", "gimp", "inkscape",
        "krita", "darktable", "rawtherapee", "illustrator", "premiere",
        "aftereffects", "audacity", "kdenlive", "obs", NULL
    };
    for (int i = 0; creative_sigs[i]; i++) {
        if (str_contains_case_insensitive(name, creative_sigs[i])) {
            return APP_GROUP_CREATIVE;
        }
    }

    /* Developer Tools & IDEs */
    static const char *devtools_sigs[] = {
        "idea", "pycharm", "clion", "webstorm", "goland", "rustrover",
        "rider", "code", "vscode", "vscodium", "gcc", "clang", "javac",
        "rustc", "cargo", "make", "cmake", "ninja", "python", "node",
        "ruby", "flutter", "dart", "gradle", "mvn", "git", NULL
    };
    for (int i = 0; devtools_sigs[i]; i++) {
        if (str_contains_case_insensitive(name, devtools_sigs[i])) {
            return APP_GROUP_DEVTOOLS;
        }
    }

    /* Web Browsers */
    static const char *browser_sigs[] = {
        "firefox", "chrome", "chromium", "brave", "safari",
        "edge", "opera", "vivaldi", "epiphany", "webkit", NULL
    };
    for (int i = 0; browser_sigs[i]; i++) {
        if (str_contains_case_insensitive(name, browser_sigs[i])) {
            return APP_GROUP_BROWSER;
        }
    }

    /* System daemons & services */
    static const char *system_sigs[] = {
        "systemd", "init", "kworker", "kthreadd", "rcu_", "udevd",
        "sshd", "dbus", "syslog", "journald", "cron", "getty",
        "login", "bash", "sh", "zsh", "vos_guest_agent", NULL
    };
    for (int i = 0; system_sigs[i]; i++) {
        if (str_contains_case_insensitive(name, system_sigs[i])) {
            return APP_GROUP_SYSTEM;
        }
    }

    return APP_GROUP_OTHER;
}

int process_filter_list(const GuestProcessMetric *in, int in_count,
                        const ProcessFilter *filter,
                        GuestProcessMetric *out, int max_out,
                        int *out_count)
{
    if (!in || in_count < 0 || !out || max_out <= 0 || !out_count) {
        return -1;
    }
    *out_count = 0;

    int matched = 0;
    for (int i = 0; i < in_count && matched < max_out; i++) {
        const GuestProcessMetric *m = &in[i];

        if (filter) {
            /* PID filter */
            if (filter->pid_filter > 0 && m->pid != filter->pid_filter) {
                continue;
            }

            /* Substring name filter */
            if (filter->name_filter[0] != '\0' &&
                !str_contains_case_insensitive(m->name, filter->name_filter)) {
                continue;
            }

            /* Group filter */
            if (filter->group_filter != APP_GROUP_ALL) {
                AppGroup g = process_classify_group(m->name);
                if (g != filter->group_filter) {
                    continue;
                }
            }

            /* Minimum CPU % */
            if (filter->min_cpu_percent > 0.0 && m->cpu_percent < filter->min_cpu_percent) {
                continue;
            }

            /* Minimum Memory */
            if (filter->min_memory_bytes > 0 && m->memory_rss_bytes < filter->min_memory_bytes) {
                continue;
            }
        }

        out[matched++] = *m;
    }

    *out_count = matched;
    return 0;
}

int process_find_by_pid(const GuestProcessMetric *in, int in_count,
                        pid_t pid, GuestProcessMetric *out)
{
    if (!in || in_count <= 0 || !out) return -1;
    for (int i = 0; i < in_count; i++) {
        if (in[i].pid == pid) {
            *out = in[i];
            return 0;
        }
    }
    return -1;
}

static int compare_by_cpu(const void *a, const void *b)
{
    const GuestProcessMetric *pa = (const GuestProcessMetric *)a;
    const GuestProcessMetric *pb = (const GuestProcessMetric *)b;
    if (pb->cpu_percent > pa->cpu_percent) return 1;
    if (pb->cpu_percent < pa->cpu_percent) return -1;
    return 0;
}

static int compare_by_mem(const void *a, const void *b)
{
    const GuestProcessMetric *pa = (const GuestProcessMetric *)a;
    const GuestProcessMetric *pb = (const GuestProcessMetric *)b;
    if (pb->memory_rss_bytes > pa->memory_rss_bytes) return 1;
    if (pb->memory_rss_bytes < pa->memory_rss_bytes) return -1;
    return 0;
}

int process_get_top(const GuestProcessMetric *in, int in_count,
                    int by_memory, GuestProcessMetric *out, int max_out,
                    int *out_count)
{
    if (!in || in_count <= 0 || !out || max_out <= 0 || !out_count) {
        return -1;
    }

    GuestProcessMetric *temp = malloc(sizeof(GuestProcessMetric) * (size_t)in_count);
    if (!temp) return -1;
    memcpy(temp, in, sizeof(GuestProcessMetric) * (size_t)in_count);

    if (by_memory) {
        qsort(temp, (size_t)in_count, sizeof(GuestProcessMetric), compare_by_mem);
    } else {
        qsort(temp, (size_t)in_count, sizeof(GuestProcessMetric), compare_by_cpu);
    }

    int copy_count = (in_count < max_out) ? in_count : max_out;
    memcpy(out, temp, sizeof(GuestProcessMetric) * (size_t)copy_count);
    free(temp);

    *out_count = copy_count;
    return 0;
}
