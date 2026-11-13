/* cgroup_manager.h – Linux cgroups v2 Resource Enforcement Subsystem
 *
 * Implements real hierarchical resource management for the Virtual OS Simulator
 * inside Linux ARM64 guests (and POSIX test harnesses).
 */

#ifndef VOS_CGROUP_MANAGER_H
#define VOS_CGROUP_MANAGER_H

#include "agent_protocol.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CGROUP_DEFAULT_ROOT "/sys/fs/cgroup"
#define CGROUP_VOS_BASE     "virtualos"
#define CGROUP_VOS_APPS     "virtualos/applications"

/* Initialize cgroup subsystem. Checks mount, mounts cgroup2 if needed,
 * enables subtree controllers (+cpu +memory +pids +io).
 * Returns 0 on success, -1 on failure. */
int cgroup_manager_init(const char *root_override);

/* Query cgroups v2 capabilities of the guest kernel */
int cgroup_manager_get_capabilities(const char *root_override, AgentCapabilities *out_caps);

/* Execute an RPC resource request and produce a verified response */
int cgroup_manager_handle_request(const char *root_override,
                                  const AgentResourceRequest *req,
                                  AgentResourceResponse *out_resp);

/* Direct C API functions */
int cgroup_is_safe_name(const char *name);
int cgroup_read_process_starttime(const char *procfs_root, pid_t pid, uint64_t *out_starttime);
int cgroup_resolve_block_device(const char *dev_name_or_path,
                                unsigned int *out_major,
                                unsigned int *out_minor,
                                char *out_canon_path,
                                size_t canon_sz);

AgentStatusCode cgroup_create_group(const char *root_override, const char *group_id, char *err, size_t err_sz);
AgentStatusCode cgroup_set_cpu_limit(const char *root_override, const char *group_id, int64_t quota_us, int64_t period_us, char *err, size_t err_sz);
AgentStatusCode cgroup_set_cpu_weight(const char *root_override, const char *group_id, int weight, char *err, size_t err_sz);
AgentStatusCode cgroup_set_memory_limit(const char *root_override, const char *group_id, uint64_t max_bytes, char *err, size_t err_sz);
AgentStatusCode cgroup_set_pids_limit(const char *root_override, const char *group_id, int64_t pids_max, char *err, size_t err_sz);
AgentStatusCode cgroup_set_io_limit(const char *root_override, const char *group_id, uint64_t read_bps, uint64_t write_bps, char *err, size_t err_sz);
AgentStatusCode cgroup_attach_process(const char *root_override, const char *group_id, pid_t pid, char *err, size_t err_sz);
AgentStatusCode cgroup_attach_process_with_identity(const char *root_override, const char *group_id, pid_t pid, uint64_t expected_starttime, char *err, size_t err_sz);
AgentStatusCode cgroup_get_status(const char *root_override, const char *group_id, AgentCgroupStatus *out_status, char *err, size_t err_sz);
AgentStatusCode cgroup_remove_group(const char *root_override, const char *group_id, char *err, size_t err_sz);

#ifdef __cplusplus
}
#endif

#endif /* VOS_CGROUP_MANAGER_H */

