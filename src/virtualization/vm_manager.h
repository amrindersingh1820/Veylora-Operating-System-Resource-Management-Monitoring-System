/* vm_manager.h – QEMU process lifecycle manager for Virtual OS Simulator.
 *
 * Responsibilities:
 *   1. Detect the QEMU executable and report its version.
 *   2. Validate a VmConfig before launching.
 *   3. Build the QEMU argument list from a VmConfig.
 *   4. Start a QEMU process (via posix_spawn on POSIX or CreateProcess on Win32).
 *   5. Track process ID and lifecycle state.
 *   6. Stop the VM gracefully (SIGTERM → timeout → SIGKILL).
 *   7. Handle unexpected child termination.
 *   8. Record lifecycle events in the existing SQLite database.
 *
 * Security contract:
 *   - Arguments are passed as arrays; no shell invocation.
 *   - Paths are validated before use.
 *   - The QEMU monitor socket interface is NOT exposed externally.
 *   - No elevated privileges are requested.
 *
 * Thread safety: none – call from a single thread.
 */
#ifndef VOS_VM_MANAGER_H
#define VOS_VM_MANAGER_H

#include "vm_config.h"
#include <stddef.h>
#include <sys/types.h>   /* pid_t on POSIX */

/* ---------------------------------------------------------------- Error codes */
typedef enum {
    VM_MGR_OK               =  0,
    VM_MGR_ERR_QEMU_MISSING = -1,   /* qemu-system-* not found in PATH or given path */
    VM_MGR_ERR_CONFIG       = -2,   /* VmConfig validation failed */
    VM_MGR_ERR_ALREADY_RUN  = -3,   /* VM is already running */
    VM_MGR_ERR_NOT_RUNNING  = -4,   /* VM is not running (cannot stop) */
    VM_MGR_ERR_SPAWN        = -5,   /* posix_spawn / CreateProcess failed */
    VM_MGR_ERR_DB           = -6,   /* Database operation failed */
    VM_MGR_ERR_TIMEOUT      = -7,   /* Graceful stop timed out; forced SIGKILL */
    VM_MGR_ERR_INTERNAL     = -8,   /* Unexpected internal error */
} VmMgrError;

/* ---------------------------------------------------------------- Manager state */

/* Maximum VMs tracked simultaneously in this session (not a DB limit). */
#define VM_MGR_MAX_VMS  16

/* Internal runtime record for one live (or recently exited) VM. */
typedef struct {
    VmConfig cfg;
    pid_t    pid;          /* 0 = not running */
    int      exit_code;    /* last exit code; meaningful only when stopped/failed */
    VmState  state;        /* current runtime state */
    int      db_vm_id;     /* row id in virtual_machines table (0 = not persisted) */
    int      guest_ready;  /* 1 if guest OS boot/readiness verified, 0 if only process alive */
    char     console_log[VM_PATH_MAX_LEN]; /* Path to serial output log */
    char     last_error[512]; /* Diagnostic message if failed */
} VmHandle;

/* Manager context. Exposed here so tests can perform white-box inspection;
 * production code should treat it as opaque and use the API only. */
struct VmManager {
    char        qemu_search_path[2048];
    int         custom_search_path;       /* 1 if caller specified custom path */
    VmHandle    vms[VM_MGR_MAX_VMS];
    int         nvm;
};
typedef struct VmManager VmManager;

/* ---------------------------------------------------------------- Lifecycle API */

/* Initialise the manager.
 *
 * @param qemu_search_path  Colon-separated list of directories to search for
 *                          QEMU (like PATH).  Pass NULL to search the system PATH.
 *
 * Returns a heap-allocated VmManager on success, NULL on allocation failure.
 * Call vm_manager_shutdown() to release.
 */
VmManager *vm_manager_init(const char *qemu_search_path);

/* Release all resources and clean up.  Any running VMs are NOT stopped
 * automatically – call vm_stop() first if needed. */
void vm_manager_shutdown(VmManager *mgr);

/* ---------------------------------------------------------------- QEMU detection */

/* Search for an appropriate qemu-system-* binary.
 *
 * On success, fills qemu_path_out (up to path_len bytes) with the absolute path
 * and returns VM_MGR_OK.  On failure returns VM_MGR_ERR_QEMU_MISSING and writes
 * an explanation to err_buf.
 */
int vm_manager_detect_qemu(VmManager *mgr,
                           VmArch arch,
                           char *qemu_path_out, size_t path_len,
                           char *err_buf, size_t err_buf_sz);

/* Report the version string of a given QEMU binary.
 *
 * Runs "qemu_path --version" and writes the first line into version_out.
 * Returns 0 on success, -1 if the binary cannot be executed.
 */
int vm_manager_qemu_version(const char *qemu_path,
                            char *version_out, size_t version_len);

/* ---------------------------------------------------------------- VM control */

/* Validate and start a VM.
 *
 * @param mgr      Manager context.
 * @param cfg      Fully populated configuration.  State field is ignored on
 *                 input and set on return.
 * @param err_buf  Human-readable error message on failure.
 *
 * On success:
 *   - cfg->state is set to VM_STATE_RUNNING.
 *   - The VM's PID and db_vm_id are stored internally (retrieve via vm_get_handle).
 *   - A "started" event is recorded in SQLite.
 *   - Returns VM_MGR_OK.
 *
 * On failure returns a negative VmMgrError; cfg->state is set to VM_STATE_FAILED.
 */
int vm_start(VmManager *mgr, VmConfig *cfg,
             char *err_buf, size_t err_buf_sz);

/* Stop a running VM gracefully.
 *
 * Sends SIGTERM (or equivalent) to the QEMU process and waits up to
 * timeout_ms milliseconds.  If the process has not exited by then, sends
 * SIGKILL.  Records the stop event in SQLite.
 *
 * @param timeout_ms  Maximum wait after SIGTERM before forced kill (ms).
 *                    Pass 0 to use the default (5000 ms).
 *
 * Returns VM_MGR_OK if the process exited cleanly within the timeout,
 * VM_MGR_ERR_TIMEOUT if SIGKILL was required,
 * VM_MGR_ERR_NOT_RUNNING if no such VM is running.
 */
int vm_stop(VmManager *mgr, const char *vm_id,
            int timeout_ms,
            char *err_buf, size_t err_buf_sz);

/* Poll for unexpected child termination (non-blocking).
 *
 * Checks whether any tracked QEMU process has exited since the last call.
 * Updates state to VM_STATE_FAILED and logs an event for each such process.
 * Call periodically from a main loop.
 *
 * Returns the number of processes that were found to have exited.
 */
int vm_reap_children(VmManager *mgr);

/* ---------------------------------------------------------------- Status queries */

/* Return the current runtime state of a VM by id.
 *
 * Returns VM_STATE_COUNT if no VM with that id is tracked in this session.
 */
VmState vm_get_state(VmManager *mgr, const char *vm_id);

/* Fill *out with a copy of the VmHandle for vm_id.
 * Returns 0 on success, -1 if not found. */
int vm_get_handle(VmManager *mgr, const char *vm_id, VmHandle *out);

/* ---------------------------------------------------------------- DB helpers (internal use) */

/* Set the database file path used by the VM manager's own SQLite connection.
 * Must be called after db_open() and before vm_db_ensure_schema().
 * Typically called once during application startup. */
void vm_manager_set_db_path(const char *path);

/* Ensure the virtual_machines and vm_events tables exist.
 * Must be called after db_open().  Idempotent (uses CREATE TABLE IF NOT EXISTS).
 * Returns 0 on success, -1 on database error. */
int vm_db_ensure_schema(void);

/* Insert or update a VM record.  Returns db row id on success, -1 on error. */
int vm_db_save(const VmHandle *h);

/* Append an event row for a VM.  event_type is a short string ("started",
 * "stopped", "failed", etc.).  Returns 0 on success, -1 on error. */
int vm_db_log_event(int db_vm_id, const char *event_type, const char *message);

/* ---------------------------------------------------------------- Phase 2: Execution & Persistence */

/* Search for system UEFI/EDK2 firmware for ARM64 (e.g. edk2-aarch64-code.fd).
 * Returns 0 if found and copies path into fw_out; -1 if not found. */
int vm_manager_find_firmware(VmArch arch, char *fw_out, size_t fw_sz);

/* Reconcile database state on startup.
 * Checks any VM recorded as 'starting', 'running', or 'stopping'. If the process
 * is no longer alive, updates status in SQLite to 'stopped' or 'failed' and logs
 * an audit event.
 * Returns the number of stale records reconciled, or -1 on DB error. */
int vm_manager_reconcile_db(VmManager *mgr);

/* Load all configured VMs from the database.
 * Returns the number of loaded VMs (up to max_vms), or -1 on error. */
int vm_db_list(VmConfig *out_configs, VmState *out_states, int *out_pids, int max_vms);

/* Load a specific VM configuration and status by its string vm_id.
 * Returns 0 on success, -1 if not found. */
int vm_db_get(const char *vm_id, VmConfig *out_cfg, VmState *out_state);

/* Delete a VM configuration and its event history from SQLite.
 * Cannot delete a running VM. Disk images on the host are NEVER deleted.
 * Returns 0 on success, -1 on error. */
int vm_db_delete(const char *vm_id, char *err_buf, size_t err_buf_sz);

/* Format recent events for vm_id into out_log buffer.
 * Returns number of bytes formatted on success, -1 on error. */
int vm_db_get_events(const char *vm_id, char *out_log, size_t out_log_sz);

#endif /* VOS_VM_MANAGER_H */
