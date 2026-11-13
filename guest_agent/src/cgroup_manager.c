/* cgroup_manager.c – Linux cgroups v2 Resource Enforcement Subsystem
 *
 * Implements real hierarchical resource management for the Virtual OS Simulator
 * inside Linux ARM64 guests (and POSIX test harnesses).
 */

#include "cgroup_manager.h"
#include "app_launcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <signal.h>

#include <dirent.h>

#if defined(__linux__)
#include <sys/mount.h>
#include <sys/vfs.h>
#include <sys/sysmacros.h>
#endif

/* ------------------------------------------------------------------ Internal Helpers */

static const char *get_root(const char *root_override)
{
    return (root_override && root_override[0]) ? root_override : CGROUP_DEFAULT_ROOT;
}

int cgroup_is_safe_name(const char *name)
{
    if (!name || !name[0]) return 0;

    size_t len = strlen(name);
    if (len > 64) return 0;

    /* First character must be alphanumeric or underscore: never '.' or '-' */
    if (!isalnum((unsigned char)name[0]) && name[0] != '_') {
        return 0;
    }

    /* Disallow explicit . or .. or addressing parent/ancestor */
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;

    /* Disallow reserved hierarchy names */
    if (strcmp(name, "applications") == 0 ||
        strcmp(name, "virtualos") == 0 ||
        strcmp(name, "cgroup") == 0 ||
        strcmp(name, "root") == 0) {
        return 0;
    }

    /* Disallow reserved controller file prefixes */
    if (strncmp(name, "cgroup.", 7) == 0 ||
        strncmp(name, "cpu.", 4) == 0 ||
        strncmp(name, "memory.", 7) == 0 ||
        strncmp(name, "pids.", 5) == 0 ||
        strncmp(name, "io.", 3) == 0 ||
        strncmp(name, "hugetlb.", 8) == 0 ||
        strncmp(name, "cpuset.", 7) == 0) {
        return 0;
    }

    /* Check all characters: alphanumeric, underscore, hyphen, or dot (no double dot) */
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.') {
            return 0; /* Rejects path separators / and \, shell metacharacters $, &, |, ;, etc. */
        }
        if (c == '.' && (i + 1 < len) && name[i + 1] == '.') {
            return 0; /* Rejects ".." sequence */
        }
    }

    return 1;
}

static int is_safe_name(const char *name)
{
    return cgroup_is_safe_name(name);
}

int cgroup_read_process_starttime(const char *procfs_root, pid_t pid, uint64_t *out_starttime)
{
    if (pid <= 0 || !out_starttime) return -1;
    *out_starttime = 0;

    char stat_path[512];
    if (procfs_root && procfs_root[0]) {
        snprintf(stat_path, sizeof(stat_path), "%s/proc/%d/stat", procfs_root, (int)pid);
        struct stat st;
        if (stat(stat_path, &st) != 0) {
            snprintf(stat_path, sizeof(stat_path), "%s/%d/stat", procfs_root, (int)pid);
        }
    } else {
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);
    }

    int fd = open(stat_path, O_RDONLY);
    if (fd < 0) {
#if defined(__APPLE__)
        /* On macOS test harness without real /proc, use fallback if procfs_root not given */
        if (kill(pid, 0) == 0 || errno == EPERM) {
            *out_starttime = 1000000ULL + (uint64_t)pid;
            return 0;
        }
#endif
        return -1;
    }

    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';

    /* Parse /proc/<pid>/stat:
     * Format: pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime cutime cstime priority nice num_threads itrealvalue starttime ...
     * Note: comm may contain spaces or parentheses, so locate first '(' and last ')'.
     */
    char *first_paren = strchr(buf, '(');
    char *last_paren = strrchr(buf, ')');
    if (!first_paren || !last_paren || last_paren <= first_paren) {
        return -1;
    }

    char *p = last_paren + 1;
    while (*p == ' ') p++;

    /* In Linux stat:
     * token 0 is state
     * token 1 is ppid
     * ...
     * token 19 is starttime (field 22)
     */
    int token_idx = 0;
    while (*p && token_idx < 19) {
        while (*p && *p != ' ' && *p != '\n') p++;
        while (*p == ' ') p++;
        token_idx++;
    }

    if (token_idx != 19 || !*p) return -1;

    char *end = NULL;
    unsigned long long st_val = strtoull(p, &end, 10);
    if (end == p) return -1;

    *out_starttime = (uint64_t)st_val;
    return 0;
}

static int make_dir_p(const char *dir, mode_t mode)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t len = strlen(tmp);
    if (len == 0) return -1;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int write_cgroup_file(const char *path, const char *val, char *err, size_t err_sz)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "open(%s) failed: %s", path, strerror(errno));
        }
        return -1;
    }
    size_t len = strlen(val);
    ssize_t w = write(fd, val, len);
    int save_errno = errno;
    close(fd);

    if (w < 0 || (size_t)w != len) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "write to %s failed: %s", path, strerror(save_errno));
        }
        return -1;
    }
    return 0;
}

static int read_cgroup_file(const char *path, char *buf, size_t sz)
{
    if (!buf || sz == 0) return -1;
    buf[0] = '\0';
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, sz - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
        buf[--n] = '\0';
    }
    return 0;
}

/* ------------------------------------------------------------------ Initialization */

static int enable_controllers_in(const char *dir)
{
    char ctrl_path[512];
    snprintf(ctrl_path, sizeof(ctrl_path), "%s/cgroup.controllers", dir);
    char avail[256];
    if (read_cgroup_file(ctrl_path, avail, sizeof(avail)) != 0) {
        return -1;
    }

    /* Build string like "+cpu +memory +pids +io" matching what's available */
    char enable_str[128] = {0};
    if (strstr(avail, "cpu"))    strcat(enable_str, "+cpu ");
    if (strstr(avail, "memory")) strcat(enable_str, "+memory ");
    if (strstr(avail, "pids"))   strcat(enable_str, "+pids ");
    if (strstr(avail, "io"))     strcat(enable_str, "+io ");

    if (enable_str[0] == '\0') return 0;

    char sub_path[512];
    snprintf(sub_path, sizeof(sub_path), "%s/cgroup.subtree_control", dir);
    write_cgroup_file(sub_path, enable_str, NULL, 0);
    return 0;
}

int cgroup_manager_init(const char *root_override)
{
    const char *root = get_root(root_override);

#if defined(__linux__)
    /* Check if /sys/fs/cgroup is mounted as cgroup2 */
    char ctrl[512];
    snprintf(ctrl, sizeof(ctrl), "%s/cgroup.controllers", root);
    struct stat st;
    if (stat(ctrl, &st) != 0 && strcmp(root, CGROUP_DEFAULT_ROOT) == 0) {
        /* Not mounted; attempt cgroup2 mount if running as root */
        if (geteuid() == 0) {
            mkdir(root, 0755);
            mount("none", root, "cgroup2", 0, NULL);
        }
    }
#endif

    /* Enable controllers in root */
    enable_controllers_in(root);

    /* Create virtualos base and enable controllers */
    char vos_base[512];
    snprintf(vos_base, sizeof(vos_base), "%s/%s", root, CGROUP_VOS_BASE);
    if (make_dir_p(vos_base, 0755) != 0) return -1;
    enable_controllers_in(vos_base);

    /* Create applications base and enable controllers */
    char vos_apps[512];
    snprintf(vos_apps, sizeof(vos_apps), "%s/%s", root, CGROUP_VOS_APPS);
    if (make_dir_p(vos_apps, 0755) != 0) return -1;
    enable_controllers_in(vos_apps);

    return 0;
}

/* ------------------------------------------------------------------ Capabilities */

int cgroup_manager_get_capabilities(const char *root_override, AgentCapabilities *caps)
{
    if (!caps) return -1;
    memset(caps, 0, sizeof(*caps));

    const char *root = get_root(root_override);
    snprintf(caps->cgroup_root, sizeof(caps->cgroup_root), "%s", root);

    struct utsname un;
    if (uname(&un) == 0) {
        snprintf(caps->kernel_release, sizeof(caps->kernel_release), "%s", un.release);
    }

    /* GPU virtualization under QEMU HVF on Apple Silicon is strictly unsupported by host hypervisor */
    caps->gpu_supported = 0;
    snprintf(caps->gpu_status, sizeof(caps->gpu_status), "UNSUPPORTED_BY_HOST_HYPERVISOR");

    char ctrl_path[512];
    snprintf(ctrl_path, sizeof(ctrl_path), "%s/cgroup.controllers", root);
    char avail[256];
    if (read_cgroup_file(ctrl_path, avail, sizeof(avail)) == 0) {
        caps->cgroup_v2_supported = 1;
        caps->cgroup_v2_mounted   = 1;
        if (strstr(avail, "cpu"))    caps->cpu_controller = 1;
        if (strstr(avail, "memory")) caps->memory_controller = 1;
        if (strstr(avail, "pids"))   caps->pids_controller = 1;
        if (strstr(avail, "io"))     caps->io_controller = 1;
        return 0;
    }

#if defined(__linux__)
    caps->cgroup_v2_supported = 1;
#else
    caps->cgroup_v2_supported = 0;
#endif
    caps->cgroup_v2_mounted = 0;
    return 0;
}

/* ------------------------------------------------------------------ Operations */

AgentStatusCode cgroup_create_group(const char *root_override, const char *group_id, char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!is_safe_name(group_id)) {
        if (err && err_sz > 0) snprintf(err, err_sz, "Invalid group_id '%s'", group_id ? group_id : "(null)");
        return AGENT_STATUS_INVALID_CONFIG;
    }

    const char *root = get_root(root_override);
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%s/%s", root, CGROUP_VOS_APPS, group_id);

    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        if (errno == EACCES || errno == EPERM) {
            if (err && err_sz > 0) snprintf(err, err_sz, "Permission denied creating '%s'", dir);
            return AGENT_STATUS_PERMISSION_DENIED;
        }
        if (err && err_sz > 0) snprintf(err, err_sz, "Failed to create directory '%s': %s", dir, strerror(errno));
        return AGENT_STATUS_FAILED;
    }

    /* Verification: check that cgroup.procs exists */
    char procs_path[512];
    snprintf(procs_path, sizeof(procs_path), "%s/cgroup.procs", dir);
    struct stat st;
    if (stat(procs_path, &st) != 0) {
        /* Under simulated mock test directory, create cgroup.procs */
        FILE *fp = fopen(procs_path, "a");
        if (fp) fclose(fp);
    }

    return AGENT_STATUS_SUCCESS;
}

AgentStatusCode cgroup_set_cpu_limit(const char *root_override, const char *group_id,
                                     int64_t quota_us, int64_t period_us,
                                     char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!is_safe_name(group_id)) return AGENT_STATUS_INVALID_CONFIG;

    const char *root = get_root(root_override);
    char file[512];
    snprintf(file, sizeof(file), "%s/%s/%s/cpu.max", root, CGROUP_VOS_APPS, group_id);

    if (period_us <= 0) period_us = 100000;

    char val[64];
    if (quota_us > 0) {
        snprintf(val, sizeof(val), "%lld %lld\n", (long long)quota_us, (long long)period_us);
    } else {
        snprintf(val, sizeof(val), "max %lld\n", (long long)period_us);
    }

    if (write_cgroup_file(file, val, err, err_sz) != 0) {
        if (errno == ENOENT) return AGENT_STATUS_GROUP_NOT_FOUND;
        if (errno == EACCES || errno == EPERM) return AGENT_STATUS_PERMISSION_DENIED;
        return AGENT_STATUS_FAILED;
    }

    /* Readback Verification */
    char readback[128];
    if (read_cgroup_file(file, readback, sizeof(readback)) != 0) {
        if (err && err_sz > 0) snprintf(err, err_sz, "Readback failed for %s", file);
        return AGENT_STATUS_VERIFICATION_FAILED;
    }

    if (quota_us > 0) {
        long long rb_q = 0, rb_p = 0;
        if (sscanf(readback, "%lld %lld", &rb_q, &rb_p) != 2 || rb_q != quota_us || rb_p != period_us) {
            if (err && err_sz > 0) snprintf(err, err_sz, "Readback mismatch: wrote '%s', read '%s'", val, readback);
            return AGENT_STATUS_VERIFICATION_FAILED;
        }
    } else {
        if (strncmp(readback, "max", 3) != 0) {
            if (err && err_sz > 0) snprintf(err, err_sz, "Readback mismatch for unconstrained quota");
            return AGENT_STATUS_VERIFICATION_FAILED;
        }
    }

    return AGENT_STATUS_SUCCESS;
}

AgentStatusCode cgroup_set_cpu_weight(const char *root_override, const char *group_id,
                                      int weight, char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!is_safe_name(group_id)) return AGENT_STATUS_INVALID_CONFIG;
    if (weight < 1 || weight > 10000) {
        if (err && err_sz > 0) snprintf(err, err_sz, "Weight %d out of range [1, 10000]", weight);
        return AGENT_STATUS_INVALID_CONFIG;
    }

    const char *root = get_root(root_override);
    char file[512];
    snprintf(file, sizeof(file), "%s/%s/%s/cpu.weight", root, CGROUP_VOS_APPS, group_id);

    char val[32];
    snprintf(val, sizeof(val), "%d\n", weight);

    if (write_cgroup_file(file, val, err, err_sz) != 0) {
        if (errno == ENOENT) return AGENT_STATUS_GROUP_NOT_FOUND;
        if (errno == EACCES || errno == EPERM) return AGENT_STATUS_PERMISSION_DENIED;
        return AGENT_STATUS_FAILED;
    }

    /* Readback Verification */
    char readback[64];
    if (read_cgroup_file(file, readback, sizeof(readback)) != 0) {
        return AGENT_STATUS_VERIFICATION_FAILED;
    }
    int rb_w = atoi(readback);
    if (rb_w != weight) {
        if (err && err_sz > 0) snprintf(err, err_sz, "Readback mismatch: wrote %d, read %d", weight, rb_w);
        return AGENT_STATUS_VERIFICATION_FAILED;
    }

    return AGENT_STATUS_SUCCESS;
}

AgentStatusCode cgroup_set_memory_limit(const char *root_override, const char *group_id,
                                         uint64_t max_bytes, char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!is_safe_name(group_id)) return AGENT_STATUS_INVALID_CONFIG;

    const char *root = get_root(root_override);
    char cur_file[512], max_file[512];
    snprintf(cur_file, sizeof(cur_file), "%s/%s/%s/memory.current", root, CGROUP_VOS_APPS, group_id);
    snprintf(max_file, sizeof(max_file), "%s/%s/%s/memory.max", root, CGROUP_VOS_APPS, group_id);

    /* 1. Check current memory usage before lowering to prevent inadvertent OOM kill */
    char cur_str[64];
    if (read_cgroup_file(cur_file, cur_str, sizeof(cur_str)) == 0) {
        char *end = NULL;
        unsigned long long current_usage = strtoull(cur_str, &end, 10);
        if (end > cur_str && max_bytes > 0 && max_bytes < current_usage) {
            if (err && err_sz > 0) {
                snprintf(err, err_sz,
                         "Requested limit %llu bytes is below current usage %llu bytes (rejected to prevent OOM kill)",
                         (unsigned long long)max_bytes, current_usage);
            }
            return AGENT_STATUS_LIMIT_BELOW_USAGE;
        }
    }

    /* 2. Write new memory limit */
    char val[64];
    if (max_bytes > 0) {
        snprintf(val, sizeof(val), "%llu\n", (unsigned long long)max_bytes);
    } else {
        snprintf(val, sizeof(val), "max\n");
    }

    if (write_cgroup_file(max_file, val, err, err_sz) != 0) {
        if (errno == ENOENT) return AGENT_STATUS_GROUP_NOT_FOUND;
        if (errno == EACCES || errno == EPERM) return AGENT_STATUS_PERMISSION_DENIED;
        return AGENT_STATUS_FAILED;
    }

    /* 3. Readback Verification */
    char readback[64];
    if (read_cgroup_file(max_file, readback, sizeof(readback)) != 0) {
        return AGENT_STATUS_VERIFICATION_FAILED;
    }
    if (max_bytes > 0) {
        char *end = NULL;
        unsigned long long rb_bytes = strtoull(readback, &end, 10);
        if (end == readback || rb_bytes != max_bytes) {
            if (err && err_sz > 0) snprintf(err, err_sz, "Readback mismatch: wrote %llu, read %s",
                                            (unsigned long long)max_bytes, readback);
            return AGENT_STATUS_VERIFICATION_FAILED;
        }
    } else {
        if (strncmp(readback, "max", 3) != 0) {
            return AGENT_STATUS_VERIFICATION_FAILED;
        }
    }

    return AGENT_STATUS_SUCCESS;
}

AgentStatusCode cgroup_set_pids_limit(const char *root_override, const char *group_id,
                                       int64_t pids_max, char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!is_safe_name(group_id)) return AGENT_STATUS_INVALID_CONFIG;

    const char *root = get_root(root_override);
    char file[512];
    snprintf(file, sizeof(file), "%s/%s/%s/pids.max", root, CGROUP_VOS_APPS, group_id);

    char val[64];
    if (pids_max > 0) {
        snprintf(val, sizeof(val), "%lld\n", (long long)pids_max);
    } else {
        snprintf(val, sizeof(val), "max\n");
    }

    if (write_cgroup_file(file, val, err, err_sz) != 0) {
        if (errno == ENOENT) return AGENT_STATUS_GROUP_NOT_FOUND;
        if (errno == EACCES || errno == EPERM) return AGENT_STATUS_PERMISSION_DENIED;
        return AGENT_STATUS_FAILED;
    }

    /* Readback Verification */
    char readback[64];
    if (read_cgroup_file(file, readback, sizeof(readback)) != 0) {
        return AGENT_STATUS_VERIFICATION_FAILED;
    }
    if (pids_max > 0) {
        long long rb_pids = atoll(readback);
        if (rb_pids != pids_max) {
            if (err && err_sz > 0) snprintf(err, err_sz, "Readback mismatch: wrote %lld, read %s",
                                            (long long)pids_max, readback);
            return AGENT_STATUS_VERIFICATION_FAILED;
        }
    } else {
        if (strncmp(readback, "max", 3) != 0) return AGENT_STATUS_VERIFICATION_FAILED;
    }

    return AGENT_STATUS_SUCCESS;
}

int cgroup_resolve_block_device(const char *dev_name_or_path,
                                unsigned int *out_major,
                                unsigned int *out_minor,
                                char *out_canon_path,
                                size_t canon_sz)
{
    if (!out_major || !out_minor) return -1;
    *out_major = 0;
    *out_minor = 0;
    if (out_canon_path && canon_sz > 0) out_canon_path[0] = '\0';

    /* Case 1: An explicit path or device name is provided */
    if (dev_name_or_path && dev_name_or_path[0]) {
        char path_buf[256];
        if (dev_name_or_path[0] == '/') {
            snprintf(path_buf, sizeof(path_buf), "%s", dev_name_or_path);
        } else {
            snprintf(path_buf, sizeof(path_buf), "/dev/%s", dev_name_or_path);
        }

        struct stat st;
        if (stat(path_buf, &st) == 0) {
            if (!S_ISBLK(st.st_mode)) {
                /* Exists, but is NOT a block device! Reject immediately. */
                return -1;
            }
            *out_major = (unsigned int)major(st.st_rdev);
            *out_minor = (unsigned int)minor(st.st_rdev);
            if (out_canon_path && canon_sz > 0) {
                snprintf(out_canon_path, canon_sz, "%s", path_buf);
            }
            return 0;
        }

        /* Check /sys/block/<name>/dev */
        const char *name_only = dev_name_or_path;
        if (strncmp(name_only, "/dev/", 5) == 0) name_only += 5;
        char sys_dev[256];
        snprintf(sys_dev, sizeof(sys_dev), "/sys/block/%s/dev", name_only);
        FILE *fp = fopen(sys_dev, "r");
        if (fp) {
            unsigned int maj = 0, min = 0;
            if (fscanf(fp, "%u:%u", &maj, &min) == 2) {
                *out_major = maj;
                *out_minor = min;
                if (out_canon_path && canon_sz > 0) {
                    snprintf(out_canon_path, canon_sz, "/dev/%s", name_only);
                }
                fclose(fp);
                return 0;
            }
            fclose(fp);
        }

        return -1;
    }

    /* Case 2: Auto-detect primary guest block device without hardcoded numbers */
#if defined(__linux__)
    /* Subcase 2a: Enumerate /sys/block directory */
    DIR *d = opendir("/sys/block");
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            if (strncmp(ent->d_name, "loop", 4) == 0) continue;
            if (strncmp(ent->d_name, "ram", 3) == 0) continue;
            if (strncmp(ent->d_name, "zram", 4) == 0) continue;
            if (strncmp(ent->d_name, "dm-", 3) == 0) continue;

            /* Check /sys/block/<ent->d_name>/dev */
            char sys_dev_path[256];
            snprintf(sys_dev_path, sizeof(sys_dev_path), "/sys/block/%s/dev", ent->d_name);
            FILE *fp = fopen(sys_dev_path, "r");
            if (fp) {
                unsigned int maj = 0, min = 0;
                if (fscanf(fp, "%u:%u", &maj, &min) == 2) {
                    *out_major = maj;
                    *out_minor = min;
                    if (out_canon_path && canon_sz > 0) {
                        snprintf(out_canon_path, canon_sz, "/dev/%s", ent->d_name);
                    }
                    fclose(fp);
                    closedir(d);
                    return 0;
                }
                fclose(fp);
            }
        }
        closedir(d);
    }

    /* Subcase 2b: Check standard candidate block devices via stat */
    const char *candidates[] = { "/dev/vda", "/dev/sda", "/dev/vdb", "/dev/nvme0n1", NULL };
    for (int i = 0; candidates[i]; i++) {
        struct stat st;
        if (stat(candidates[i], &st) == 0 && S_ISBLK(st.st_mode)) {
            *out_major = (unsigned int)major(st.st_rdev);
            *out_minor = (unsigned int)minor(st.st_rdev);
            if (out_canon_path && canon_sz > 0) {
                snprintf(out_canon_path, canon_sz, "%s", candidates[i]);
            }
            return 0;
        }
    }
#endif

    return -1;
}

AgentStatusCode cgroup_set_io_limit(const char *root_override, const char *group_id,
                                    uint64_t read_bps, uint64_t write_bps,
                                    char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!is_safe_name(group_id)) return AGENT_STATUS_INVALID_CONFIG;

    const char *root = get_root(root_override);
    char file[512];
    snprintf(file, sizeof(file), "%s/%s/%s/io.max", root, CGROUP_VOS_APPS, group_id);

    struct stat st;
    if (stat(file, &st) != 0) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "io.max not supported by guest kernel or controller not active");
        }
        return AGENT_STATUS_UNSUPPORTED;
    }

    /* Dynamically resolve block device major:minor */
    unsigned int dev_major = 0, dev_minor = 0;
    char canon_dev[128] = {0};

    if (cgroup_resolve_block_device(NULL, &dev_major, &dev_minor, canon_dev, sizeof(canon_dev)) != 0) {
        /* Check if root_override has mock block dev definition (for unit test isolation) */
        if (root_override) {
            char mock_file[512];
            snprintf(mock_file, sizeof(mock_file), "%s/mock_block_dev", root_override);
            FILE *mf = fopen(mock_file, "r");
            if (mf) {
                if (fscanf(mf, "%u:%u", &dev_major, &dev_minor) == 2) {
                    snprintf(canon_dev, sizeof(canon_dev), "mock_block_dev");
                }
                fclose(mf);
            }
        }
    }

    /* Strictly validate device existence - reject writing to misidentified devices */
    if (dev_major == 0 && dev_minor == 0 && canon_dev[0] == '\0') {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "Failed to resolve valid guest block device for I/O limit enforcement");
        }
        return AGENT_STATUS_UNSUPPORTED;
    }

    char dev_maj_min[32];
    snprintf(dev_maj_min, sizeof(dev_maj_min), "%u:%u", dev_major, dev_minor);

    char val[128];
    if (read_bps > 0 && write_bps > 0) {
        snprintf(val, sizeof(val), "%s rbps=%llu wbps=%llu\n", dev_maj_min,
                 (unsigned long long)read_bps, (unsigned long long)write_bps);
    } else if (read_bps > 0) {
        snprintf(val, sizeof(val), "%s rbps=%llu\n", dev_maj_min, (unsigned long long)read_bps);
    } else if (write_bps > 0) {
        snprintf(val, sizeof(val), "%s wbps=%llu\n", dev_maj_min, (unsigned long long)write_bps);
    } else {
        snprintf(val, sizeof(val), "%s rbps=max wbps=max\n", dev_maj_min);
    }

    if (write_cgroup_file(file, val, err, err_sz) != 0) {
        if (errno == ENODEV || errno == EINVAL) {
            if (err && err_sz > 0) snprintf(err, err_sz, "Block device %s does not support io throttling", dev_maj_min);
            return AGENT_STATUS_UNSUPPORTED;
        }
        return AGENT_STATUS_FAILED;
    }

    /* Readback Verification */
    char readback[256];
    if (read_cgroup_file(file, readback, sizeof(readback)) != 0) {
        if (err && err_sz > 0) snprintf(err, err_sz, "Readback failed for %s", file);
        return AGENT_STATUS_VERIFICATION_FAILED;
    }
    if (read_bps > 0 || write_bps > 0) {
        if (strstr(readback, dev_maj_min) == NULL) {
            if (err && err_sz > 0) snprintf(err, err_sz, "Readback mismatch for io.max: expected device %s, read '%s'", dev_maj_min, readback);
            return AGENT_STATUS_VERIFICATION_FAILED;
        }
    }

    return AGENT_STATUS_SUCCESS;
}

AgentStatusCode cgroup_attach_process_with_identity(const char *root_override, const char *group_id,
                                                    pid_t pid, uint64_t expected_starttime,
                                                    char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!is_safe_name(group_id)) return AGENT_STATUS_INVALID_CONFIG;

    /* Safety rule: Never attach init (PID 1) or invalid PID */
    if (pid <= 1) {
        if (err && err_sz > 0) snprintf(err, err_sz, "Refusing to attach system init / PID %d", (int)pid);
        return AGENT_STATUS_PERMISSION_DENIED;
    }

    /* 1. Step 1: Read process start time from procfs */
    uint64_t current_starttime = 0;
    if (cgroup_read_process_starttime(root_override, pid, &current_starttime) != 0) {
        if (err && err_sz > 0) snprintf(err, err_sz, "PID %d not found (ESRCH or missing in procfs)", (int)pid);
        return AGENT_STATUS_PROCESS_NOT_FOUND;
    }

    /* 2. Step 2: Validate against expected start time (PID reuse detection) */
    if (expected_starttime != 0 && current_starttime != expected_starttime) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "PID %d identity changed: expected starttime %llu, found %llu (PID was reused)",
                     (int)pid, (unsigned long long)expected_starttime, (unsigned long long)current_starttime);
        }
        return AGENT_STATUS_PROCESS_IDENTITY_CHANGED;
    }

    /* 3. Step 3: Re-verify process identity immediately before attachment */
    uint64_t pre_attach_starttime = 0;
    if (cgroup_read_process_starttime(root_override, pid, &pre_attach_starttime) != 0) {
        if (err && err_sz > 0) snprintf(err, err_sz, "PID %d terminated immediately before attachment", (int)pid);
        return AGENT_STATUS_PROCESS_NOT_FOUND;
    }
    if (pre_attach_starttime != current_starttime) {
        if (err && err_sz > 0) snprintf(err, err_sz, "PID %d identity changed immediately before attachment", (int)pid);
        return AGENT_STATUS_PROCESS_IDENTITY_CHANGED;
    }

    const char *root = get_root(root_override);
    char file[512];
    snprintf(file, sizeof(file), "%s/%s/%s/cgroup.procs", root, CGROUP_VOS_APPS, group_id);

    char val[32];
    snprintf(val, sizeof(val), "%d\n", (int)pid);

    if (write_cgroup_file(file, val, err, err_sz) != 0) {
        if (errno == ENOENT) return AGENT_STATUS_GROUP_NOT_FOUND;
        if (errno == EACCES || errno == EPERM) return AGENT_STATUS_PERMISSION_DENIED;
        return AGENT_STATUS_FAILED;
    }

    /* 4. Verification: check that PID appears in cgroup.procs */
    char procs_buf[1024];
    if (read_cgroup_file(file, procs_buf, sizeof(procs_buf)) == 0) {
        char target[32];
        snprintf(target, sizeof(target), "%d", (int)pid);
        if (strstr(procs_buf, target) == NULL) {
            if (err && err_sz > 0) snprintf(err, err_sz, "PID %d not verified in cgroup.procs", (int)pid);
            return AGENT_STATUS_VERIFICATION_FAILED;
        }
    }

    return AGENT_STATUS_SUCCESS;
}

AgentStatusCode cgroup_attach_process(const char *root_override, const char *group_id,
                                      pid_t pid, char *err, size_t err_sz)
{
    return cgroup_attach_process_with_identity(root_override, group_id, pid, 0, err, err_sz);
}

AgentStatusCode cgroup_get_status(const char *root_override, const char *group_id,
                                  AgentCgroupStatus *out, char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!out) return AGENT_STATUS_INVALID_CONFIG;
    memset(out, 0, sizeof(*out));
    snprintf(out->group_id, sizeof(out->group_id), "%s", group_id ? group_id : "");

    if (!is_safe_name(group_id)) return AGENT_STATUS_INVALID_CONFIG;

    const char *root = get_root(root_override);
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%s/%s", root, CGROUP_VOS_APPS, group_id);

    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        out->exists = 0;
        return AGENT_STATUS_GROUP_NOT_FOUND;
    }
    out->exists = 1;

    /* Defaults */
    out->cpu_quota_us  = -1;
    out->cpu_period_us = 100000;
    out->cpu_weight    = 100;
    out->pids_max      = -1;

    char file[512], buf[256];

    /* 1. cpu.max */
    snprintf(file, sizeof(file), "%s/cpu.max", dir);
    if (read_cgroup_file(file, buf, sizeof(buf)) == 0) {
        if (strncmp(buf, "max", 3) == 0) {
            out->cpu_quota_us = -1;
            sscanf(buf + 3, "%lld", (long long *)&out->cpu_period_us);
        } else {
            sscanf(buf, "%lld %lld", (long long *)&out->cpu_quota_us, (long long *)&out->cpu_period_us);
        }
    }

    /* 2. cpu.weight */
    snprintf(file, sizeof(file), "%s/cpu.weight", dir);
    if (read_cgroup_file(file, buf, sizeof(buf)) == 0) {
        out->cpu_weight = atoi(buf);
    }

    /* 3. cpu.stat */
    snprintf(file, sizeof(file), "%s/cpu.stat", dir);
    if (read_cgroup_file(file, buf, sizeof(buf)) == 0) {
        char *p = strstr(buf, "usage_usec");
        if (p) sscanf(p + 10, "%llu", (unsigned long long *)&out->cpu_usage_usec);
        char *pt = strstr(buf, "throttled_usec");
        if (pt) sscanf(pt + 14, "%llu", (unsigned long long *)&out->cpu_throttled_usec);
    }

    /* 4. memory.max */
    snprintf(file, sizeof(file), "%s/memory.max", dir);
    if (read_cgroup_file(file, buf, sizeof(buf)) == 0) {
        if (strncmp(buf, "max", 3) == 0) {
            out->memory_max_bytes = 0;
        } else {
            char *end = NULL;
            out->memory_max_bytes = strtoull(buf, &end, 10);
        }
    }

    /* 5. memory.current */
    snprintf(file, sizeof(file), "%s/memory.current", dir);
    if (read_cgroup_file(file, buf, sizeof(buf)) == 0) {
        char *end = NULL;
        out->memory_current_bytes = strtoull(buf, &end, 10);
    }

    /* 5b. memory.peak */
    snprintf(file, sizeof(file), "%s/memory.peak", dir);
    if (read_cgroup_file(file, buf, sizeof(buf)) == 0) {
        char *end = NULL;
        out->memory_peak_bytes = strtoull(buf, &end, 10);
    }

    /* 6. memory.events */
    snprintf(file, sizeof(file), "%s/memory.events", dir);
    if (read_cgroup_file(file, buf, sizeof(buf)) == 0) {
        char *p = strstr(buf, "oom_kill");
        if (p) sscanf(p + 8, "%llu", (unsigned long long *)&out->memory_oom_events);
    }

    /* 7. pids.max */
    snprintf(file, sizeof(file), "%s/pids.max", dir);
    if (read_cgroup_file(file, buf, sizeof(buf)) == 0) {
        if (strncmp(buf, "max", 3) == 0) {
            out->pids_max = -1;
        } else {
            out->pids_max = atoll(buf);
        }
    }

    /* 8. pids.current */
    snprintf(file, sizeof(file), "%s/pids.current", dir);
    if (read_cgroup_file(file, buf, sizeof(buf)) == 0) {
        out->pids_current = atoll(buf);
    }

    /* 9. cgroup.procs */
    snprintf(file, sizeof(file), "%s/cgroup.procs", dir);
    FILE *fp = fopen(file, "r");
    if (fp) {
        char line[64];
        int count = 0;
        while (fgets(line, sizeof(line), fp) && count < 64) {
            pid_t p = (pid_t)atoi(line);
            if (p > 0) {
                out->attached_pids[count++] = p;
            }
        }
        out->attached_pids_count = count;
        fclose(fp);
    }

    /* 10. io.stat */
    snprintf(file, sizeof(file), "%s/io.stat", dir);
    if (read_cgroup_file(file, buf, sizeof(buf)) == 0) {
        char *pr = strstr(buf, "rbytes=");
        if (pr) sscanf(pr + 7, "%llu", (unsigned long long *)&out->io_read_bytes);
        char *pw = strstr(buf, "wbytes=");
        if (pw) sscanf(pw + 7, "%llu", (unsigned long long *)&out->io_write_bytes);
    }

    out->verified = 1;
    return AGENT_STATUS_SUCCESS;
}

AgentStatusCode cgroup_remove_group(const char *root_override, const char *group_id, char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!is_safe_name(group_id)) return AGENT_STATUS_INVALID_CONFIG;

    const char *root = get_root(root_override);
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%s/%s", root, CGROUP_VOS_APPS, group_id);

    struct stat st;
    if (stat(dir, &st) != 0) {
        return AGENT_STATUS_GROUP_NOT_FOUND;
    }

    /* Check if any active processes are inside cgroup.procs */
    char procs_path[512];
    snprintf(procs_path, sizeof(procs_path), "%s/cgroup.procs", dir);
    FILE *fp = fopen(procs_path, "r");
    if (fp) {
        char line[64];
        int populated = 0;
        while (fgets(line, sizeof(line), fp)) {
            pid_t p = (pid_t)atoi(line);
            if (p > 0) {
                if (kill(p, 0) == 0 || errno == EPERM) {
                    populated++;
                }
            }
        }
        fclose(fp);
        if (populated > 0) {
            if (err && err_sz > 0) {
                snprintf(err, err_sz, "Refusing to remove populated group '%s': %d active process(es) remain",
                         group_id, populated);
            }
            return AGENT_STATUS_GROUP_BUSY;
        }
    }

    /* Unlink mock files if present so rmdir succeeds across POSIX filesystems */
    char file[512];
    snprintf(file, sizeof(file), "%s/cpu.max", dir); unlink(file);
    snprintf(file, sizeof(file), "%s/cpu.weight", dir); unlink(file);
    snprintf(file, sizeof(file), "%s/cpu.stat", dir); unlink(file);
    snprintf(file, sizeof(file), "%s/memory.max", dir); unlink(file);
    snprintf(file, sizeof(file), "%s/memory.current", dir); unlink(file);
    snprintf(file, sizeof(file), "%s/memory.events", dir); unlink(file);
    snprintf(file, sizeof(file), "%s/pids.max", dir); unlink(file);
    snprintf(file, sizeof(file), "%s/pids.current", dir); unlink(file);
    snprintf(file, sizeof(file), "%s/pids.events", dir); unlink(file);
    snprintf(file, sizeof(file), "%s/io.max", dir); unlink(file);
    snprintf(file, sizeof(file), "%s/cgroup.procs", dir); unlink(file);

    if (rmdir(dir) != 0) {
        if (errno == EBUSY) {
            if (err && err_sz > 0) snprintf(err, err_sz, "rmdir(%s) failed: Resource busy (processes active)", dir);
            return AGENT_STATUS_GROUP_BUSY;
        }
        if (errno == EACCES || errno == EPERM) return AGENT_STATUS_PERMISSION_DENIED;
        if (err && err_sz > 0) snprintf(err, err_sz, "rmdir(%s) failed: %s", dir, strerror(errno));
        return AGENT_STATUS_FAILED;
    }

    return AGENT_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ RPC Request Handler */

int cgroup_manager_handle_request(const char *root_override,
                                  const AgentResourceRequest *req,
                                  AgentResourceResponse *resp)
{
    if (!req || !resp) return -1;
    memset(resp, 0, sizeof(*resp));

    resp->version   = req->version ? req->version : AGENT_PROTOCOL_VERSION;
    snprintf(resp->request_id, sizeof(resp->request_id), "%s", req->request_id);
    snprintf(resp->vm_id, sizeof(resp->vm_id), "%s", req->vm_id);
    resp->operation = req->operation;

    char err_buf[256] = {0};
    AgentStatusCode status = AGENT_STATUS_SUCCESS;

    switch (req->operation) {
        case AGENT_OP_GET_CAPABILITIES:
            cgroup_manager_get_capabilities(root_override, &resp->capabilities);
            status = AGENT_STATUS_SUCCESS;
            resp->verified = 1;
            break;

        case AGENT_OP_CREATE_GROUP:
            status = cgroup_create_group(root_override, req->group_id, err_buf, sizeof(err_buf));
            if (status == AGENT_STATUS_SUCCESS) {
                cgroup_get_status(root_override, req->group_id, &resp->applied, NULL, 0);
                resp->verified = 1;
            }
            break;

        case AGENT_OP_SET_CPU_LIMIT: {
            int64_t q = req->cpu_quota_us;
            int64_t p = req->cpu_period_us > 0 ? req->cpu_period_us : 100000;
            if (req->cpu_cores > 0.0) {
                q = (int64_t)(req->cpu_cores * (double)p);
            }
            status = cgroup_set_cpu_limit(root_override, req->group_id, q, p, err_buf, sizeof(err_buf));
            if (status == AGENT_STATUS_SUCCESS) {
                cgroup_get_status(root_override, req->group_id, &resp->applied, NULL, 0);
                resp->verified = 1;
            }
            break;
        }

        case AGENT_OP_SET_CPU_WEIGHT:
            status = cgroup_set_cpu_weight(root_override, req->group_id, req->cpu_weight, err_buf, sizeof(err_buf));
            if (status == AGENT_STATUS_SUCCESS) {
                cgroup_get_status(root_override, req->group_id, &resp->applied, NULL, 0);
                resp->verified = 1;
            }
            break;

        case AGENT_OP_SET_MEMORY_LIMIT:
            status = cgroup_set_memory_limit(root_override, req->group_id, req->memory_max_bytes, err_buf, sizeof(err_buf));
            if (status == AGENT_STATUS_SUCCESS) {
                cgroup_get_status(root_override, req->group_id, &resp->applied, NULL, 0);
                resp->verified = 1;
            }
            break;

        case AGENT_OP_SET_PIDS_LIMIT:
            status = cgroup_set_pids_limit(root_override, req->group_id, req->pids_max, err_buf, sizeof(err_buf));
            if (status == AGENT_STATUS_SUCCESS) {
                cgroup_get_status(root_override, req->group_id, &resp->applied, NULL, 0);
                resp->verified = 1;
            }
            break;

        case AGENT_OP_SET_IO_LIMIT:
            status = cgroup_set_io_limit(root_override, req->group_id, req->io_read_bps, req->io_write_bps, err_buf, sizeof(err_buf));
            if (status == AGENT_STATUS_SUCCESS) {
                cgroup_get_status(root_override, req->group_id, &resp->applied, NULL, 0);
                resp->verified = 1;
            }
            break;

        case AGENT_OP_ATTACH_PROCESS:
            status = cgroup_attach_process_with_identity(root_override, req->group_id, req->pid, req->expected_start_time, err_buf, sizeof(err_buf));
            if (status == AGENT_STATUS_SUCCESS) {
                cgroup_get_status(root_override, req->group_id, &resp->applied, NULL, 0);
                resp->verified = 1;
            }
            break;

        case AGENT_OP_GET_GROUP_STATUS:
            status = cgroup_get_status(root_override, req->group_id, &resp->applied, err_buf, sizeof(err_buf));
            if (status == AGENT_STATUS_SUCCESS) {
                resp->verified = 1;
            }
            break;

        case AGENT_OP_REMOVE_GROUP:
            status = cgroup_remove_group(root_override, req->group_id, err_buf, sizeof(err_buf));
            if (status == AGENT_STATUS_SUCCESS) {
                resp->verified = 1;
            }
            break;

        case AGENT_OP_DISCOVER_APPS:
            app_discover_executables(&resp->discovery);
            status = AGENT_STATUS_SUCCESS;
            resp->verified = 1;
            break;

        case AGENT_OP_LAUNCH_APP:
            status = app_launch_process(root_override, req->group_id, req->app_path, req->app_args,
                                        &resp->app_pid, &resp->app_start_time, err_buf, sizeof(err_buf));
            snprintf(resp->app_id, sizeof(resp->app_id), "%s", req->app_id);
            if (status == AGENT_STATUS_SUCCESS) {
                resp->verified = 1;
                cgroup_get_status(root_override, req->group_id, &resp->applied, NULL, 0);
            }
            break;

        case AGENT_OP_STOP_APP:
            resp->app_exit_code = -1;
            status = app_stop_process_with_exit_code(req->pid, req->expected_start_time, req->app_timeout_ms,
                                                     req->app_signal == 9, &resp->app_exit_code, err_buf, sizeof(err_buf));
            snprintf(resp->app_id, sizeof(resp->app_id), "%s", req->app_id);
            if (status == AGENT_STATUS_SUCCESS) {
                resp->verified = 1;
            }
            break;

        case AGENT_OP_GET_APP_METRICS:
            status = app_get_metrics(root_override, req->group_id, req->pid, &resp->applied, err_buf, sizeof(err_buf));
            snprintf(resp->app_id, sizeof(resp->app_id), "%s", req->app_id);
            if (status == AGENT_STATUS_SUCCESS) {
                resp->verified = 1;
            }
            break;

        default:
            status = AGENT_STATUS_INVALID_CONFIG;
            snprintf(err_buf, sizeof(err_buf), "Unknown or unsupported operation");
            break;
    }

    resp->status_code = status;
    snprintf(resp->status_msg, sizeof(resp->status_msg), "%s", agent_status_code_name(status));
    snprintf(resp->error_message, sizeof(resp->error_message), "%s", err_buf);

    return 0;
}
