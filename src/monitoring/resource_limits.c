/* resource_limits.c – Resource Management and Limit Enforcement
 *
 * Implements hardware verification, Linux cgroups v2 resource capping,
 * and macOS host process priority management.
 */

#include "resource_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>

const char *resource_limit_status_name(ResourceLimitStatus status)
{
    switch (status) {
        case LIMIT_STATUS_PENDING:           return "pending";
        case LIMIT_STATUS_ENFORCED:          return "enforced";
        case LIMIT_STATUS_UNSUPPORTED:       return "unsupported";
        case LIMIT_STATUS_PERMISSION_DENIED: return "permission_denied";
        case LIMIT_STATUS_PROTECTED_PROCESS: return "protected_process";
        case LIMIT_STATUS_FAILED:            return "failed";
        default:                             return "unknown";
    }
}

int resource_limits_verify_vm(const VmConfig *cfg, char *report, size_t report_sz)
{
    if (!cfg || !report || report_sz == 0) return -1;

    int valid = (cfg->cpu_count >= 1 && cfg->cpu_count <= VM_MAX_CPUS &&
                 cfg->memory_mb >= VM_MIN_MEM_MB);

    snprintf(report, report_sz,
             "VM Resource Limits Verification for '%s':\n"
             " - Allocated vCPUs : %d (Enforced via QEMU '-smp %d')\n"
             " - Configured RAM  : %d MiB (Enforced via QEMU '-m %dM')\n"
             " - Hardware Cap    : %s\n"
             "Note: Guest OS cannot access more vCPUs or RAM than configured.\n"
             "Host RAM consumption (RSS) will vary dynamically up to the %d MiB ceiling.",
             cfg->id,
             cfg->cpu_count, cfg->cpu_count,
             cfg->memory_mb, cfg->memory_mb,
             valid ? "STRICTLY ENFORCED BY HYPERVISOR" : "INVALID CONFIGURATION",
             cfg->memory_mb);

    return valid ? 0 : -1;
}

int resource_limits_check_cgroup_v2(const char *cgroup_root)
{
    const char *root = (cgroup_root && cgroup_root[0]) ? cgroup_root : "/sys/fs/cgroup";

    char path[512];
    snprintf(path, sizeof(path), "%s/cgroup.controllers", root);

    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        return 1; /* cgroups v2 active */
    }
    return 0; /* not cgroups v2 or path inaccessible */
}

static int write_cgroup_file(const char *path, const char *val, char *err_buf, size_t err_sz)
{
    FILE *fp = fopen(path, "w");
    if (!fp) {
        if (err_buf && err_sz > 0) {
            snprintf(err_buf, err_sz, "Cannot write to %s: %s", path, strerror(errno));
        }
        return -1;
    }
    if (fputs(val, fp) == EOF) {
        if (err_buf && err_sz > 0) {
            snprintf(err_buf, err_sz, "Failed writing '%s' to %s: %s", val, path, strerror(errno));
        }
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

ResourceLimitStatus resource_limits_apply_cgroup_v2(const char *cgroup_root,
                                                    const AppResourceLimit *limit,
                                                    char *err_buf, size_t err_sz)
{
    if (err_buf && err_sz > 0) err_buf[0] = '\0';
    if (!limit) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "NULL limit argument");
        return LIMIT_STATUS_FAILED;
    }

    /* 1. Protected process safety check */
    if (limit->pid <= 1) {
        if (err_buf && err_sz > 0) {
            snprintf(err_buf, err_sz, "Refusing to constrain system init / PID %d", limit->pid);
        }
        return LIMIT_STATUS_PROTECTED_PROCESS;
    }

    const char *root = (cgroup_root && cgroup_root[0]) ? cgroup_root : "/sys/fs/cgroup";

    /* 2. Check cgroups v2 availability */
    if (!resource_limits_check_cgroup_v2(root)) {
        if (err_buf && err_sz > 0) {
            snprintf(err_buf, err_sz,
                     "cgroups v2 not mounted or accessible at '%s'. "
                     "Guest OS requires systemd with unified cgroup hierarchy.", root);
        }
        return LIMIT_STATUS_UNSUPPORTED;
    }

    /* 3. Check permission and create project cgroup directory: <root>/virtualos/<pid> */
    char project_dir[512];
    snprintf(project_dir, sizeof(project_dir), "%s/virtualos", root);
    if (mkdir(project_dir, 0755) != 0 && errno != EEXIST) {
        if (errno == EACCES || errno == EPERM) {
            if (err_buf && err_sz > 0) {
                snprintf(err_buf, err_sz, "Permission denied creating '%s': root privileges required.", project_dir);
            }
            return LIMIT_STATUS_PERMISSION_DENIED;
        }
        if (err_buf && err_sz > 0) {
            snprintf(err_buf, err_sz, "mkdir failed for '%s': %s", project_dir, strerror(errno));
        }
        return LIMIT_STATUS_FAILED;
    }

    char proc_dir[512];
    snprintf(proc_dir, sizeof(proc_dir), "%s/%d", project_dir, limit->pid);
    if (mkdir(proc_dir, 0755) != 0 && errno != EEXIST) {
        if (errno == EACCES || errno == EPERM) {
            if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Permission denied creating '%s'", proc_dir);
            return LIMIT_STATUS_PERMISSION_DENIED;
        }
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "mkdir failed: %s", strerror(errno));
        return LIMIT_STATUS_FAILED;
    }

    /* 4. Configure CPU quota (cpu.max) if specified */
    if (limit->cpu_quota_percent > 0.0) {
        /* e.g. 50.0% with 100000us period -> 50000 100000 */
        int64_t quota_us = (int64_t)(limit->cpu_quota_percent * 1000.0);
        char val[64];
        snprintf(val, sizeof(val), "%lld 100000\n", (long long)quota_us);

        char cfile[512];
        snprintf(cfile, sizeof(cfile), "%s/cpu.max", proc_dir);
        if (write_cgroup_file(cfile, val, err_buf, err_sz) != 0) {
            return LIMIT_STATUS_FAILED;
        }
    }

    /* 5. Configure Memory limit (memory.max) if specified */
    if (limit->memory_max_bytes > 0) {
        char val[64];
        snprintf(val, sizeof(val), "%llu\n", (unsigned long long)limit->memory_max_bytes);

        char cfile[512];
        snprintf(cfile, sizeof(cfile), "%s/memory.max", proc_dir);
        if (write_cgroup_file(cfile, val, err_buf, err_sz) != 0) {
            return LIMIT_STATUS_FAILED;
        }
    }

    /* 6. Attach process PID to cgroup.procs */
    char procs_file[512];
    snprintf(procs_file, sizeof(procs_file), "%s/cgroup.procs", proc_dir);
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d\n", limit->pid);
    if (write_cgroup_file(procs_file, pid_str, err_buf, err_sz) != 0) {
        return LIMIT_STATUS_FAILED;
    }

    if (err_buf && err_sz > 0) {
        snprintf(err_buf, err_sz, "Successfully enforced cgroups v2 limits on PID %d in %s",
                 limit->pid, proc_dir);
    }
    return LIMIT_STATUS_ENFORCED;
}

int resource_limits_apply_host_priority(pid_t qemu_pid, int nice_value,
                                        char *err_buf, size_t err_sz)
{
    if (err_buf && err_sz > 0) err_buf[0] = '\0';
    if (qemu_pid <= 0) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Invalid QEMU PID %d", (int)qemu_pid);
        return -1;
    }

    if (nice_value < -20 || nice_value > 19) {
        if (err_buf && err_sz > 0) {
            snprintf(err_buf, err_sz, "Nice value %d outside valid range [-20, 19]", nice_value);
        }
        return -1;
    }

    if (setpriority(PRIO_PROCESS, (id_t)qemu_pid, nice_value) != 0) {
        if (errno == EACCES || errno == EPERM) {
            if (err_buf && err_sz > 0) {
                snprintf(err_buf, err_sz,
                         "Cannot set host priority to %d: elevated privileges required to lower nice value.",
                         nice_value);
            }
        } else {
            if (err_buf && err_sz > 0) {
                snprintf(err_buf, err_sz, "setpriority failed for PID %d: %s", (int)qemu_pid, strerror(errno));
            }
        }
        return -1;
    }

    if (err_buf && err_sz > 0) {
        snprintf(err_buf, err_sz, "Host scheduling priority for QEMU PID %d set to nice=%d",
                 (int)qemu_pid, nice_value);
    }
    return 0;
}

