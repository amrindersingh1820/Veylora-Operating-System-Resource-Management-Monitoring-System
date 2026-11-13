/* resource_manager.h – Virtual OS Simulator Resource Management & Enforcement
 *
 * Implements VM-level hardware boundaries, guest application cgroups v2
 * enforcement via virtio-serial RPC, SQLite persistence, and audit logging.
 */

#ifndef VOS_RESOURCE_MANAGER_H
#define VOS_RESOURCE_MANAGER_H

#include "agent_protocol.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard Result Codes */
typedef enum {
    RESOURCE_SUCCESS                = 0,
    RESOURCE_ERR_INVALID_CONFIG     = -1,
    RESOURCE_ERR_UNSUPPORTED        = -2,
    RESOURCE_ERR_PERMISSION_DENIED  = -3,
    RESOURCE_ERR_GROUP_NOT_FOUND    = -4,
    RESOURCE_ERR_PROCESS_NOT_FOUND  = -5,
    RESOURCE_ERR_COMMUNICATION_FAILED = -6,
    RESOURCE_ERR_VERIFICATION_FAILED  = -7,
    RESOURCE_ERR_LIMIT_BELOW_USAGE  = -8,
    RESOURCE_ERR_GROUP_BUSY         = -9,
    RESOURCE_ERR_FAILED             = -10,
    RESOURCE_ERR_PROCESS_IDENTITY_CHANGED = -11,
    RESOURCE_ERR_UNSUPPORTED_BY_HOST_HYPERVISOR = -12,
    RESOURCE_ERR_UNSUPPORTED_PLATFORM = -13,
    RESOURCE_ERR_UNSUPPORTED_HARDWARE = -14
} ResourceResult;

const char *resource_result_str(ResourceResult res);

/* Subsystem Lifecycle */
int resource_manager_init(const char *db_path);
void resource_manager_shutdown(void);

/* Direct Host-to-Guest RPC Communication */
ResourceResult resource_manager_rpc(const char *vm_id,
                                    const AgentResourceRequest *req,
                                    AgentResourceResponse *out_resp,
                                    int timeout_ms);

/* In-Guest Cgroups v2 Resource Management */
ResourceResult resource_manager_detect_capabilities(const char *vm_id,
                                                    AgentCapabilities *out_caps,
                                                    char *err, size_t err_sz);

ResourceResult resource_manager_create_group(const char *vm_id,
                                             const char *group_id,
                                             const char *group_name,
                                             char *err, size_t err_sz);

ResourceResult resource_manager_set_cpu_limit(const char *vm_id,
                                              const char *group_id,
                                              double cpu_cores,
                                              int64_t period_us,
                                              char *err, size_t err_sz);

ResourceResult resource_manager_set_cpu_weight(const char *vm_id,
                                               const char *group_id,
                                               int weight,
                                               char *err, size_t err_sz);

ResourceResult resource_manager_set_memory_limit(const char *vm_id,
                                                 const char *group_id,
                                                 uint64_t memory_bytes,
                                                 char *err, size_t err_sz);

ResourceResult resource_manager_set_process_limit(const char *vm_id,
                                                  const char *group_id,
                                                  int64_t max_pids,
                                                  char *err, size_t err_sz);

ResourceResult resource_manager_set_io_limit(const char *vm_id,
                                             const char *group_id,
                                             uint64_t read_bps,
                                             uint64_t write_bps,
                                             char *err, size_t err_sz);

ResourceResult resource_manager_attach_process(const char *vm_id,
                                               const char *group_id,
                                               pid_t pid,
                                               char *err, size_t err_sz);

ResourceResult resource_manager_attach_process_with_identity(const char *vm_id,
                                                             const char *group_id,
                                                             pid_t pid,
                                                             uint64_t expected_start_time,
                                                             char *err, size_t err_sz);

ResourceResult resource_manager_get_group_status(const char *vm_id,
                                                 const char *group_id,
                                                 AgentCgroupStatus *out_status,
                                                 char *err, size_t err_sz);

ResourceResult resource_manager_remove_group(const char *vm_id,
                                             const char *group_id,
                                             char *err, size_t err_sz);

ResourceResult resource_manager_list_groups(const char *vm_id,
                                            char out_groups[][64],
                                            int max_groups,
                                            int *out_count);

/* VM Hardware Boundary Configuration (QEMU level) */
ResourceResult resource_manager_set_vm_hardware(const char *vm_id,
                                                int cpu_count,
                                                int memory_mb,
                                                int *out_requires_restart,
                                                char *err, size_t err_sz);

/* SQLite Audit Logging */
int resource_manager_audit_log(const char *vm_id,
                               const char *group_id,
                               const char *operation,
                               const char *req_val,
                               const char *applied_val,
                               const char *status,
                               int verified,
                               const char *err_msg);

#ifdef __cplusplus
}
#endif

#endif /* VOS_RESOURCE_MANAGER_H */

