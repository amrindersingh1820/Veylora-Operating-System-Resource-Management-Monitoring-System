/* vos_api.h – Virtual OS Simulator Stable C ABI & FFI Interface
 *
 * Universal C11 interface for Flutter/Dart frontend and external consumers.
 * Exposes thread-safe APIs for:
 *   - System lifecycle and host telemetry
 *   - Virtual Machine management (QEMU lifecycle, states, configurations)
 *   - In-Guest Application Management (discovery, registry, profiles, launch, stop)
 *   - In-Guest Resource Enforcement (cgroups v2 limits and telemetry)
 *   - OS Algorithm Simulator (CPU scheduling, memory management, Banker's algorithm)
 *   - Persistent Audit and Lifecycle Event Logs
 */

#ifndef VOS_API_H
#define VOS_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef VOS_BUILD_SHARED
    #define VOS_EXPORT __declspec(dllexport)
  #else
    #define VOS_EXPORT __declspec(dllimport)
  #endif
#else
  #if __GNUC__ >= 4
    #define VOS_EXPORT __attribute__((visibility("default")))
  #else
    #define VOS_EXPORT
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ Return Codes */
typedef enum {
    VOS_OK                      = 0,
    VOS_ERR_INVALID_PARAM       = -1,
    VOS_ERR_NOT_INITIALIZED     = -2,
    VOS_ERR_DB                  = -3,
    VOS_ERR_NOT_FOUND           = -4,
    VOS_ERR_ALREADY_EXISTS      = -5,
    VOS_ERR_VM_NOT_RUNNING      = -6,
    VOS_ERR_VM_ALREADY_RUNNING  = -7,
    VOS_ERR_COMMUNICATION       = -8,
    VOS_ERR_CGROUP              = -9,
    VOS_ERR_PID_IDENTITY        = -10,
    VOS_ERR_INTERNAL            = -11
} VosApiResult;

/* ------------------------------------------------------------------ System Lifecycle */

/* Initializes database and backend managers with the specified SQLite DB path.
 * If db_path is NULL or empty, defaults to "virtual_os.db". */
VOS_EXPORT int vos_api_init(const char *db_path);

/* Gracefully flushes and closes active managers and database connections. */
VOS_EXPORT int vos_api_shutdown(void);

/* Returns 1 if backend is currently initialized, 0 otherwise. */
VOS_EXPORT int vos_api_is_initialized(void);

/* Version string of the Virtual OS Simulator engine. */
VOS_EXPORT const char *vos_api_version(void);

/* ------------------------------------------------------------------ Host Telemetry */

/* Retrieves current host OS resource metrics (macOS Mach / sysctl / proc) in JSON format.
 * Format:
 * {
 *   "cpu_count": 10,
 *   "cpu_usage_pct": 14.5,
 *   "total_ram_bytes": 17179869184,
 *   "used_ram_bytes": 10737418240,
 *   "free_ram_bytes": 6442450944,
 *   "load_avg_1m": 2.15,
 *   "load_avg_5m": 1.80,
 *   "load_avg_15m": 1.50,
 *   "uptime_seconds": 124500,
 *   "host_platform": "macOS Apple Silicon (Darwin arm64)"
 * }
 */
VOS_EXPORT int vos_api_get_host_telemetry(char *out_json, size_t out_sz);

/* ------------------------------------------------------------------ Virtual Machines */

/* Returns JSON array of all registered and active VMs.
 * Includes configuration, current state, PID, uptime, and guest readiness. */
VOS_EXPORT int vos_api_vm_list(char *out_json, size_t out_sz);

/* Retrieves JSON details for a single VM by ID. */
VOS_EXPORT int vos_api_vm_get(const char *vm_id, char *out_json, size_t out_sz);

/* Creates or updates a virtual machine configuration.
 * config_json schema:
 * {
 *   "id": "alpine-arm64",
 *   "name": "Alpine Linux Testbed",
 *   "vcpu_count": 2,
 *   "ram_mb": 1024,
 *   "disk_path": "vm_images/alpine.qcow2",
 *   "iso_path": "vm_images/alpine-arm64.iso",
 *   "arch": "aarch64",
 *   "enable_network": true
 * }
 */
VOS_EXPORT int vos_api_vm_create(const char *config_json, char *out_err, size_t err_sz);

/* Deletes a VM configuration from the database (only if not currently running). */
VOS_EXPORT int vos_api_vm_delete(const char *vm_id, char *out_err, size_t err_sz);

/* Starts a virtual machine with QEMU HVF acceleration. */
VOS_EXPORT int vos_api_vm_start(const char *vm_id, char *out_err, size_t err_sz);

/* Stops a virtual machine gracefully (force=0 sends SIGTERM; force=1 sends SIGKILL). */
VOS_EXPORT int vos_api_vm_stop(const char *vm_id, int force, char *out_err, size_t err_sz);

/* Retrieves latest segregated telemetry for a given VM (Host QEMU RSS vs Guest OS telemetry). */
VOS_EXPORT int vos_api_vm_telemetry(const char *vm_id, char *out_json, size_t out_sz);

/* ------------------------------------------------------------------ Application Management */

/* Triggers in-guest executable discovery via guest agent RPC over virtio-serial.
 * Returns JSON list of found executables in /bin, /usr/bin, etc. */
VOS_EXPORT int vos_api_app_discover(const char *vm_id, char *out_json, size_t out_sz);

/* Registers an application in the VM application registry. */
VOS_EXPORT int vos_api_app_register(const char *vm_id,
                                    const char *app_id,
                                    const char *name,
                                    const char *executable_path,
                                    const char *description,
                                    const char *category,
                                    const char *version,
                                    const char *default_profile_id,
                                    char *out_err, size_t err_sz);

/* Unregisters an application from the VM application registry. */
VOS_EXPORT int vos_api_app_unregister(const char *vm_id,
                                      const char *app_id,
                                      char *out_err, size_t err_sz);

/* Returns JSON array of registered applications for the specified VM. */
VOS_EXPORT int vos_api_app_list(const char *vm_id, char *out_json, size_t out_sz);

/* Launches an application with verified pre-execution cgroup assignment handshake.
 * Returns unique instance ID upon success. */
VOS_EXPORT int vos_api_app_launch(const char *vm_id,
                                  const char *app_id,
                                  const char *args_str,
                                  const char *profile_override,
                                  char *out_instance_id, size_t id_sz,
                                  char *out_err, size_t err_sz);

/* Terminates an application instance with PID reuse validation. */
VOS_EXPORT int vos_api_app_stop(const char *vm_id,
                                const char *instance_id,
                                int timeout_ms,
                                int force,
                                char *out_err, size_t err_sz);

/* Returns JSON array of application execution instances (state_filter: "RUNNING", "STOPPED", or NULL). */
VOS_EXPORT int vos_api_app_instances_list(const char *vm_id,
                                          const char *state_filter,
                                          char *out_json, size_t out_sz);

/* Collects live cgroup resource metrics for an active application instance. */
VOS_EXPORT int vos_api_app_collect_metrics(const char *vm_id,
                                           const char *instance_id,
                                           char *out_json, size_t out_sz);

/* ------------------------------------------------------------------ Resource Profiles */

/* Returns JSON array of all defined application resource profiles. */
VOS_EXPORT int vos_api_profile_list(char *out_json, size_t out_sz);

/* Retrieves a single resource profile by ID in JSON format. */
VOS_EXPORT int vos_api_profile_get(const char *profile_id, char *out_json, size_t out_sz);

/* Creates or updates a resource profile. */
VOS_EXPORT int vos_api_profile_save(const char *profile_id,
                                    const char *name,
                                    const char *description,
                                    double cpu_cores,
                                    int cpu_weight,
                                    uint64_t memory_max_bytes,
                                    int64_t pids_max,
                                    uint64_t io_read_bps,
                                    uint64_t io_write_bps,
                                    char *out_err, size_t err_sz);

/* Deletes a resource profile (blocked if referenced by registered applications). */
VOS_EXPORT int vos_api_profile_delete(const char *profile_id, char *out_err, size_t err_sz);

/* ------------------------------------------------------------------ OS Algorithm Simulator */

/* Runs CPU scheduling algorithm on the provided process list JSON.
 * algo: 0=AUTO, 1=FCFS, 2=SJF, 3=SRTF, 4=PRIO_NP, 5=PRIO_P, 6=RR
 * Returns detailed Gantt chart, metrics, and step-by-step trace in out_json. */
VOS_EXPORT int vos_api_sim_run_scheduler(int algo,
                                         const char *processes_json,
                                         int quantum,
                                         char *out_json, size_t out_sz);

/* Runs Memory Allocation algorithm (First Fit, Best Fit, Worst Fit).
 * strategy: 0=AUTO, 1=FIRST, 2=BEST, 3=WORST
 * Returns memory block assignments, fragmentation, and layout in out_json. */
VOS_EXPORT int vos_api_sim_run_memory(int strategy,
                                      const char *processes_json,
                                      const char *blocks_json,
                                      char *out_json, size_t out_sz);

/* Runs Banker's Deadlock Avoidance Algorithm.
 * Returns safety verdict, safe sequence array, and need matrices in out_json. */
VOS_EXPORT int vos_api_sim_run_bankers(const char *processes_json,
                                       const char *avail_json,
                                       char *out_json, size_t out_sz);

/* ------------------------------------------------------------------ Audit & Event History */

/* Records an audit and lifecycle event for a VM, application, or test run in SQLite. */
VOS_EXPORT int vos_api_event_log(const char *instance_id,
                                 const char *app_id,
                                 const char *vm_id,
                                 const char *event_type,
                                 const char *details);

/* Queries persistent audit and lifecycle events for VM and apps in JSON format. */
VOS_EXPORT int vos_api_events_list(const char *vm_id,
                                   int limit,
                                   char *out_json, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif /* VOS_API_H */

