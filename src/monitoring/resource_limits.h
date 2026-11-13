/* resource_limits.h – Resource Management and Limit Enforcement
 *
 * Implements VM-level restrictions (QEMU hardware caps), guest application-level
 * restrictions (Linux cgroups v2), and macOS host process priority controls.
 */

#ifndef VOS_RESOURCE_LIMITS_H
#define VOS_RESOURCE_LIMITS_H

#include "../virtualization/vm_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LIMIT_STATUS_PENDING = 0,
    LIMIT_STATUS_ENFORCED,
    LIMIT_STATUS_UNSUPPORTED,         /* cgroups v2 not present or non-Linux */
    LIMIT_STATUS_PERMISSION_DENIED,    /* e.g. insufficient privileges in guest */
    LIMIT_STATUS_PROTECTED_PROCESS,   /* cannot constrain init / PID 1 */
    LIMIT_STATUS_FAILED
} ResourceLimitStatus;

const char *resource_limit_status_name(ResourceLimitStatus status);

/* Application-level resource limit specification */
typedef struct {
    pid_t    pid;                     /* Target PID */
    char     process_name[64];        /* Executable name */
    double   cpu_quota_percent;       /* e.g. 50.0% -> 50000 100000 in cpu.max (-1.0 = none) */
    int      cpu_weight;              /* 1 - 10000 (-1 = default 100) */
    uint64_t memory_max_bytes;        /* Max RAM in bytes (0 = unconstrained) */
    bool     kill_on_oom;             /* true: kill on exceed; false: throttle/swap */
} AppResourceLimit;

/* Persisted or live enforcement record */
typedef struct {
    char                vm_id[64];
    char                process_identifier[64];
    AppResourceLimit    limit;
    ResourceLimitStatus status;
    char                detail[256];
    uint64_t            updated_at_ms;
} ResourceEnforcementRecord;

/* ------------------------------------------------------------------ API */

/* 1. VM-level: Verify that the VM configuration enforces hard hardware boundaries
 * via QEMU parameters (-smp and -m). Formats human-readable verification report. */
int resource_limits_verify_vm(const VmConfig *cfg, char *report_buf, size_t report_sz);

/* 2. Guest-level: Check whether cgroups v2 is mounted and accessible.
 * Pass NULL to check default "/sys/fs/cgroup".
 * Returns 1 if cgroups v2 is available, 0 if not available, -1 on error. */
int resource_limits_check_cgroup_v2(const char *cgroup_root);

/* 3. Guest-level: Apply cgroups v2 restrictions to a guest process.
 * Writes to a dedicated project hierarchy: <cgroup_root>/virtualos/<pid>/
 * Validates process PID (refuses PID 1 or kernel processes).
 * Confirms with OS write verification before returning LIMIT_STATUS_ENFORCED.
 * Returns ResourceLimitStatus. */
ResourceLimitStatus resource_limits_apply_cgroup_v2(const char *cgroup_root,
                                                    const AppResourceLimit *limit,
                                                    char *err_buf, size_t err_sz);

/* 4. Host-level: Apply safe host-side scheduling policy to QEMU process on macOS.
 * Uses setpriority() to yield host CPU priority.
 * Returns 0 on success, -1 on failure. */
int resource_limits_apply_host_priority(pid_t qemu_pid, int nice_value,
                                        char *err_buf, size_t err_sz);

#ifdef __cplusplus
}
#endif

#endif /* VOS_RESOURCE_LIMITS_H */

