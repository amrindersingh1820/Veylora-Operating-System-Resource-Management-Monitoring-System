# Virtual OS Simulator – Resource Management API Reference

This document provides the complete API reference for the Virtual OS Simulator Resource Management and Enforcement subsystem (Phase 4). It covers the backend C API, wire protocol specifications, and integration contracts for the upcoming Flutter/Dart desktop frontend.

---

## 1. Result Codes (`ResourceResult`)

All C APIs return standard, strongly-typed result codes defined in `src/monitoring/resource_manager.h`:

| Enum Constant | Value | Description |
|---|---|---|
| `RESOURCE_SUCCESS` | `0` | Operation succeeded and was verified via operating system readback. |
| `RESOURCE_ERR_INVALID_CONFIG` | `-1` | Invalid parameter (e.g. invalid group ID, out-of-range weight, bad PID). |
| `RESOURCE_ERR_UNSUPPORTED` | `-2` | Requested controller or feature is not supported by guest kernel (e.g. GPU, I/O throttle). |
| `RESOURCE_ERR_PERMISSION_DENIED` | `-3` | Insufficient permissions (e.g. non-root guest agent, PID 1 init protection). |
| `RESOURCE_ERR_GROUP_NOT_FOUND` | `-4` | Target cgroup directory does not exist. |
| `RESOURCE_ERR_PROCESS_NOT_FOUND` | `-5` | Target PID is not currently active in guest process table (`ESRCH`). |
| `RESOURCE_ERR_COMMUNICATION_FAILED` | `-6` | Virtio-serial socket offline or request timed out without response. |
| `RESOURCE_ERR_VERIFICATION_FAILED` | `-7` | Kernel wrote value but readback verification check mismatched. |
| `RESOURCE_ERR_LIMIT_BELOW_USAGE` | `-8` | Requested memory limit is below active usage (rejected to prevent OOM kill). |
| `RESOURCE_ERR_FAILED` | `-9` | General operating system or database error. |

---

## 2. C API Reference (`resource_manager.h`)

### `resource_manager_init`
```c
int resource_manager_init(const char *db_path);
```
- **Description**: Initializes the host resource manager and ensures database schema (`resource_groups`, `resource_limits`, `enforcement_events`) exists.
- **Parameters**: `db_path` - Path to SQLite database (pass `NULL` for default `virtual_os.db`).
- **Returns**: `0` on success, `-1` on error.

### `resource_manager_shutdown`
```c
void resource_manager_shutdown(void);
```
- **Description**: Closes active database handles and cleans up mutexes.

### `resource_manager_detect_capabilities`
```c
ResourceResult resource_manager_detect_capabilities(const char *vm_id,
                                                    AgentCapabilities *out_caps,
                                                    char *err, size_t err_sz);
```
- **Description**: Probes the guest OS kernel via virtio-serial RPC to identify active cgroup v2 controllers and hardware acceleration.
- **Returns**: `RESOURCE_SUCCESS` if queried and verified. `out_caps->gpu_supported` is guaranteed to return `0` (unsupported).

### `resource_manager_create_group`
```c
ResourceResult resource_manager_create_group(const char *vm_id,
                                             const char *group_id,
                                             const char *group_name,
                                             char *err, size_t err_sz);
```
- **Description**: Creates `/sys/fs/cgroup/virtualos/applications/<group_id>/` inside guest OS and registers group in SQLite.
- **Safety**: Rejects group IDs with path traversal characters (`..`, `/`).

### `resource_manager_set_cpu_limit`
```c
ResourceResult resource_manager_set_cpu_limit(const char *vm_id,
                                              const char *group_id,
                                              double cpu_cores,
                                              int64_t period_us,
                                              char *err, size_t err_sz);
```
- **Description**: Enforces CPU quota on target cgroup.
- **Parameters**:
  - `cpu_cores`: Fractional vCPU limit (e.g., `1.5` for 150% CPU, `0.5` for 50% CPU; `<= 0` for unconstrained).
  - `period_us`: Quota period in microseconds (typically `100000`).
- **Verification**: Automatically reads back `cpu.max` from guest kernel before returning.

### `resource_manager_set_cpu_weight`
```c
ResourceResult resource_manager_set_cpu_weight(const char *vm_id,
                                               const char *group_id,
                                               int weight,
                                               char *err, size_t err_sz);
```
- **Description**: Configures proportional scheduling weight (`1` to `10000`, default `100`).

### `resource_manager_set_memory_limit`
```c
ResourceResult resource_manager_set_memory_limit(const char *vm_id,
                                                 const char *group_id,
                                                 uint64_t memory_bytes,
                                                 char *err, size_t err_sz);
```
- **Description**: Configures `memory.max` ceiling in bytes (`0` for unlimited).
- **Safety Guardrail**: Compares requested limit against `memory.current`. If $\text{requested} < \text{current}$, returns `RESOURCE_ERR_LIMIT_BELOW_USAGE` to prevent kernel OOM aborts.

### `resource_manager_set_process_limit`
```c
ResourceResult resource_manager_set_process_limit(const char *vm_id,
                                                  const char *group_id,
                                                  int64_t max_pids,
                                                  char *err, size_t err_sz);
```
- **Description**: Configures `pids.max` limit to protect against fork bombs.

### `resource_manager_set_io_limit`
```c
ResourceResult resource_manager_set_io_limit(const char *vm_id,
                                             const char *group_id,
                                             uint64_t read_bps,
                                             uint64_t write_bps,
                                             char *err, size_t err_sz);
```
- **Description**: Enforces read/write bytes per second throttling on primary guest block device. Returns `RESOURCE_ERR_UNSUPPORTED` if unavailable.

### `resource_manager_attach_process`
```c
ResourceResult resource_manager_attach_process(const char *vm_id,
                                               const char *group_id,
                                               pid_t pid,
                                               char *err, size_t err_sz);
```
- **Description**: Moves guest process into cgroup by writing PID to `cgroup.procs`.
- **Safety**: Rejects PID $\le 1$ with `RESOURCE_ERR_PERMISSION_DENIED`. Confirms PID membership via readback.

### `resource_manager_get_group_status`
```c
ResourceResult resource_manager_get_group_status(const char *vm_id,
                                                 const char *group_id,
                                                 AgentCgroupStatus *out_status,
                                                 char *err, size_t err_sz);
```
- **Description**: Collects verified applied limits and live runtime metrics (CPU time consumed, current memory RSS, active PIDs, OOM events).

### `resource_manager_remove_group`
```c
ResourceResult resource_manager_remove_group(const char *vm_id,
                                             const char *group_id,
                                             char *err, size_t err_sz);
```
- **Description**: Migrates attached processes to root cgroup and removes `/sys/fs/cgroup/virtualos/applications/<group_id>`.

### `resource_manager_set_vm_hardware`
```c
ResourceResult resource_manager_set_vm_hardware(const char *vm_id,
                                                int cpu_count,
                                                int memory_mb,
                                                int *out_requires_restart,
                                                char *err, size_t err_sz);
```
- **Description**: Updates VM hardware allocation (`-smp` vCPUs and `-m` RAM MiB).
- **Restart Contract**: Sets `*out_requires_restart = 1` because Apple Silicon QEMU HVF does not support dynamic CPU/RAM hotplugging.

---

## 3. Host-to-Guest RPC Wire Protocol

### Request Format (Host $\rightarrow$ Guest)
```json
{
  "version": 1,
  "request_id": "req-1730598000-1",
  "vm_id": "alpine-test",
  "operation": "SET_CPU_LIMIT",
  "group_id": "app_worker",
  "group_name": "Background Worker",
  "cpu_quota_us": 50000,
  "cpu_period_us": 100000,
  "cpu_cores": 0.50,
  "cpu_weight": 150,
  "memory_max_bytes": 268435456,
  "pids_max": 20,
  "io_read_bps": 0,
  "io_write_bps": 0,
  "pid": 1234
}
```

### Response Format (Guest $\rightarrow$ Host)
```json
{
  "version": 1,
  "request_id": "req-1730598000-1",
  "vm_id": "alpine-test",
  "operation": "SET_CPU_LIMIT",
  "status_code": 0,
  "status_msg": "SUCCESS",
  "verified": true,
  "error_message": "",
  "applied": {
    "group_id": "app_worker",
    "exists": true,
    "cpu_quota_us": 50000,
    "cpu_period_us": 100000,
    "cpu_weight": 150,
    "cpu_usage_usec": 2451928,
    "memory_max_bytes": 268435456,
    "memory_current_bytes": 14208000,
    "memory_oom_events": 0,
    "pids_max": 20,
    "pids_current": 1,
    "pids_events": 0,
    "io_read_bps": 0,
    "io_write_bps": 0,
    "attached_pids": [1234]
  },
  "capabilities": {
    "cgroup_v2_supported": true,
    "cgroup_v2_mounted": true,
    "cpu_controller": true,
    "memory_controller": true,
    "pids_controller": true,
    "io_controller": true,
    "gpu_supported": false,
    "kernel_release": "6.6.31-0-lts",
    "cgroup_root": "/sys/fs/cgroup"
  }
}
```

---

## 4. Flutter / Dart FFI Integration Contract

For the planned Flutter desktop frontend, backend functions can be bound using `dart:ffi`:

```dart
// Example Dart FFI signature mapping
typedef ResourceManagerSetCpuLimitC = Int32 Function(
    Pointer<Utf8> vmId,
    Pointer<Utf8> groupId,
    Double cpuCores,
    Int64 periodUs,
    Pointer<Utf8> errBuf,
    IntPtr errBufSz
);

typedef ResourceManagerSetCpuLimitDart = int Function(
    Pointer<Utf8> vmId,
    Pointer<Utf8> groupId,
    double cpuCores,
    int periodUs,
    Pointer<Utf8> errBuf,
    int errBufSz
);
```

### Integration Guidelines
1. **Never block the Flutter UI isolate**: Virtio-serial RPC calls should execute in a background Dart isolate using `compute()` or `Isolate.spawn()`.
2. **Audit & Event Streaming**: The Flutter UI can listen to SQLite `enforcement_events` or invoke `resource_manager_get_group_status` every second for live telemetry gauges.

