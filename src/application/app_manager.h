/* app_manager.h – Phase 5 Application Management & Virtual Environment Integration
 *
 * Implements:
 *   - Application Registry (CRUD, VM-isolated, enable/disable)
 *   - Application Discovery via guest agent
 *   - Application Resource Profiles (cgroups v2 mapping, SQLite storage)
 *   - Application Launcher with verified pre-execution cgroup handshake
 *   - Application Lifecycle Management (PID reuse protected, reconciliation)
 *   - Application Resource Monitoring & Time-Series Sample Persistence
 */

#ifndef VOS_APP_MANAGER_H
#define VOS_APP_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include "monitoring/agent_protocol.h"
#include "monitoring/resource_manager.h"
#include "sqlite3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ Constants & Return Codes */

#define APP_ID_MAX_LEN 64
#define APP_NAME_MAX_LEN 128
#define APP_PATH_MAX_LEN 256
#define APP_DESC_MAX_LEN 256
#define APP_CAT_MAX_LEN 64
#define APP_VER_MAX_LEN 32
#define APP_PROFILE_ID_MAX_LEN 64
#define APP_INSTANCE_ID_MAX_LEN 96

typedef enum {
    APP_SUCCESS = 0,
    APP_ERR_INVALID_PARAM = -1,
    APP_ERR_NOT_FOUND = -2,
    APP_ERR_ALREADY_EXISTS = -3,
    APP_ERR_VM_NOT_FOUND = -4,
    APP_ERR_VM_NOT_RUNNING = -5,
    APP_ERR_AGENT_COMM = -6,
    APP_ERR_CGROUP_FAILED = -7,
    APP_ERR_LAUNCH_FAILED = -8,
    APP_ERR_STOP_FAILED = -9,
    APP_ERR_PID_IDENTITY_CHANGED = -10,
    APP_ERR_DB = -11,
    APP_ERR_UNSUPPORTED = -12
} AppResult;

typedef enum {
    APP_STATE_REGISTERED = 0,
    APP_STATE_STARTING,
    APP_STATE_RUNNING,
    APP_STATE_STOPPING,
    APP_STATE_STOPPED,
    APP_STATE_FAILED,
    APP_STATE_UNKNOWN
} AppState;

/* ------------------------------------------------------------------ Data Structures */

/* Application Registry Entry */
typedef struct {
    int64_t  db_id;
    char     app_id[APP_ID_MAX_LEN];
    char     vm_id[64];
    char     name[APP_NAME_MAX_LEN];
    char     executable_path[APP_PATH_MAX_LEN];
    char     description[APP_DESC_MAX_LEN];
    char     category[APP_CAT_MAX_LEN];
    char     version[APP_VER_MAX_LEN];
    int      enabled;
    char     default_profile_id[APP_PROFILE_ID_MAX_LEN];
    uint64_t registered_at;
    uint64_t last_launched_at;
    int      launch_count;
} ApplicationEntry;

/* Application Resource Profile */
typedef struct {
    int64_t  db_id;
    char     profile_id[APP_PROFILE_ID_MAX_LEN];
    char     name[APP_NAME_MAX_LEN];
    char     description[APP_DESC_MAX_LEN];
    double   cpu_cores;            /* e.g. 0.5, 1.5 cores (0 = unlimited) */
    int64_t  cpu_quota_us;         /* Calculated or direct quota */
    int64_t  cpu_period_us;        /* Default 100000 us */
    int      cpu_weight;           /* 1-10000, default 100 */
    uint64_t memory_max_bytes;     /* Max memory ceiling in bytes (0 = unlimited) */
    int64_t  pids_max;             /* Maximum active tasks/threads (0 or -1 = unlimited) */
    uint64_t io_read_bps;          /* Max read bytes/sec (0 = unlimited) */
    uint64_t io_write_bps;         /* Max write bytes/sec (0 = unlimited) */
    uint64_t created_at;
    uint64_t updated_at;
} ApplicationProfile;

/* Application Running Instance */
typedef struct {
    int64_t  db_id;
    char     instance_id[APP_INSTANCE_ID_MAX_LEN];
    char     app_id[APP_ID_MAX_LEN];
    char     vm_id[64];
    pid_t    guest_pid;
    uint64_t process_start_time;
    char     cgroup_path[128];
    AppState state;
    char     profile_id[APP_PROFILE_ID_MAX_LEN];
    uint64_t started_at;
    uint64_t stopped_at;
    int      exit_code;
    char     failure_reason[256];
} ApplicationInstance;

/* Application Resource Sample */
typedef struct {
    int64_t  db_id;
    char     instance_id[APP_INSTANCE_ID_MAX_LEN];
    char     app_id[APP_ID_MAX_LEN];
    char     vm_id[64];
    double   cpu_usage_pct;
    uint64_t cpu_time_usec;
    uint64_t throttled_usec;
    uint64_t memory_current_bytes;
    uint64_t memory_peak_bytes;
    int64_t  pids_current;
    int64_t  pids_max;
    uint64_t io_read_bytes;
    uint64_t io_write_bytes;
    uint64_t sample_timestamp;
    char     freshness[16];        /* "FRESH", "STALE", "UNAVAILABLE" */
    char     verification_state[24];/* "VERIFIED", "UNVERIFIED" */
} ApplicationResourceSample;

/* ------------------------------------------------------------------ Lifecycle & Initialization */

/* Initialize application manager, database schemas, and default profiles */
AppResult app_manager_init(const char *db_path);

/* Shutdown application manager and release cached handles */
void app_manager_shutdown(void);

/* Ensure database tables and indexes exist */
int app_manager_ensure_schema(sqlite3 *db);

/* String utility for AppResult and AppState */
const char *app_result_str(AppResult res);
const char *app_state_name(AppState state);
AppState app_state_from_name(const char *name);

/* ------------------------------------------------------------------ Application Registry */

/* Register a new application for a virtual machine */
AppResult app_manager_register_app(const char *vm_id,
                                   const char *app_id,
                                   const char *name,
                                   const char *executable_path,
                                   const char *description,
                                   const char *category,
                                   const char *version,
                                   const char *default_profile_id,
                                   char *err_buf, size_t err_sz);

/* Update an existing application's metadata */
AppResult app_manager_update_app(const char *vm_id,
                                 const char *app_id,
                                 const char *name,
                                 const char *description,
                                 const char *category,
                                 const char *version,
                                 const char *default_profile_id,
                                 char *err_buf, size_t err_sz);

/* Remove application registration from database (does NOT delete executable from guest) */
AppResult app_manager_unregister_app(const char *vm_id,
                                     const char *app_id,
                                     char *err_buf, size_t err_sz);

/* Retrieve details of a registered application */
AppResult app_manager_get_app(const char *vm_id,
                              const char *app_id,
                              ApplicationEntry *out_app,
                              char *err_buf, size_t err_sz);

/* List registered applications for a VM (optional category filter) */
AppResult app_manager_list_apps(const char *vm_id,
                                const char *category_filter,
                                ApplicationEntry *out_apps,
                                int max_apps,
                                int *out_count);

/* Enable or disable an application */
AppResult app_manager_set_enabled(const char *vm_id,
                                  const char *app_id,
                                  int enabled,
                                  char *err_buf, size_t err_sz);

/* ------------------------------------------------------------------ Application Discovery */

/* Discover executables in guest filesystem (/bin, /usr/bin, /usr/local/bin, /opt) via RPC */
AppResult app_manager_discover_apps(const char *vm_id,
                                    AgentAppDiscoveryList *out_discovery,
                                    char *err_buf, size_t err_sz);

/* ------------------------------------------------------------------ Application Profiles */

/* Create a new reusable resource profile */
AppResult app_manager_create_profile(const char *profile_id,
                                     const char *name,
                                     const char *description,
                                     double cpu_cores,
                                     int cpu_weight,
                                     uint64_t memory_max_bytes,
                                     int64_t pids_max,
                                     uint64_t io_read_bps,
                                     uint64_t io_write_bps,
                                     char *err_buf, size_t err_sz);

/* Update an existing resource profile */
AppResult app_manager_update_profile(const char *profile_id,
                                     const char *name,
                                     const char *description,
                                     double cpu_cores,
                                     int cpu_weight,
                                     uint64_t memory_max_bytes,
                                     int64_t pids_max,
                                     uint64_t io_read_bps,
                                     uint64_t io_write_bps,
                                     char *err_buf, size_t err_sz);

/* Delete a resource profile */
AppResult app_manager_delete_profile(const char *profile_id,
                                     char *err_buf, size_t err_sz);

/* Retrieve profile details */
AppResult app_manager_get_profile(const char *profile_id,
                                  ApplicationProfile *out_profile,
                                  char *err_buf, size_t err_sz);

/* List all available resource profiles */
AppResult app_manager_list_profiles(ApplicationProfile *out_profiles,
                                    int max_profiles,
                                    int *out_count);

/* Assign a resource profile to an application */
AppResult app_manager_assign_profile(const char *vm_id,
                                     const char *app_id,
                                     const char *profile_id,
                                     char *err_buf, size_t err_sz);

/* ------------------------------------------------------------------ Launcher & Lifecycle */

/* Launch an application inside the target guest VM.
 * Enforces resource profile cgroup BEFORE releasing the child to execute.
 * Returns the generated instance_id and assigns guest PID. */
AppResult app_manager_launch_app(const char *vm_id,
                                 const char *app_id,
                                 const char *args_str,
                                 const char *profile_id_override,
                                 char *out_instance_id, size_t instance_id_sz,
                                 char *err_buf, size_t err_sz);

/* Stop a running application instance (graceful SIGTERM, followed by SIGKILL if timeout).
 * Uses process start time to guarantee PID reuse protection. */
AppResult app_manager_stop_app(const char *vm_id,
                               const char *instance_id,
                               int timeout_ms,
                               int force,
                               char *err_buf, size_t err_sz);

/* Query live state and instance details */
AppResult app_manager_get_instance(const char *instance_id,
                                   ApplicationInstance *out_instance,
                                   char *err_buf, size_t err_sz);

/* List instances for a VM (optional state filter) */
AppResult app_manager_list_instances(const char *vm_id,
                                     AppState state_filter,
                                     ApplicationInstance *out_instances,
                                     int max_instances,
                                     int *out_count);

/* Reconcile stale instance records (e.g. after VM restart or crash) */
AppResult app_manager_reconcile_instances(const char *vm_id,
                                          int *out_reconciled_count);

/* ------------------------------------------------------------------ Resource Monitoring */

/* Collect live resource metrics from the guest agent for an instance,
 * persist a time-series record in application_resource_samples, and return sample. */
AppResult app_manager_collect_metrics(const char *vm_id,
                                      const char *instance_id,
                                      ApplicationResourceSample *out_sample,
                                      char *err_buf, size_t err_sz);

/* Retrieve persisted time-series samples for an instance (for Flutter charts) */
AppResult app_manager_get_samples(const char *instance_id,
                                  ApplicationResourceSample *out_samples,
                                  int max_samples,
                                  int *out_count);

/* Log an application event in application_events table */
AppResult app_manager_log_event(const char *instance_id,
                                const char *app_id,
                                const char *vm_id,
                                const char *event_type,
                                const char *details);

#ifdef __cplusplus
}
#endif

#endif /* VOS_APP_MANAGER_H */

