# Flutter & Frontend Application Management API Contract

**Project**: Virtual OS Simulator  
**Header**: `src/application/app_manager.h`  
**Host Architecture**: macOS Apple Silicon M5 (`arm64`)  
**Shared Library**: `build/libvirtual_os.dylib` (or statically linked CLI/backend)  
**Status**: APPROVED & READY FOR FLUTTER INTEGRATION  

---

## 1. Architectural Scope and Integration Options

Virtual OS Simulator is architected with a pure C11 backend and SQLite persistence layer. To support the planned Flutter/Dart desktop and mobile interface, the backend provides two clean integration paths:

1. **High-Performance Dart FFI (`dart:ffi`)**: Directly bind to the compiled C symbols using native pointers and structs for zero-overhead, sub-millisecond telemetry updates and instant launch control.
2. **Local Daemon / IPC Bridge**: Wrap the C API in a lightweight HTTP/JSON or Unix Domain Socket service that the Flutter app consumes via standard `http` packages.

This document defines the exact C API signatures, struct definitions, JSON schemas, and ready-to-use Dart FFI bindings.

---

## 2. Data Models and JSON Schemas

### 2.1 Application Entry (`ApplicationEntry`)
```json
{
  "db_id": 1,
  "app_id": "busybox_worker",
  "vm_id": "alpine_01",
  "name": "Busybox Background Worker",
  "executable_path": "/bin/busybox",
  "description": "Background compute job",
  "category": "System Utility",
  "version": "1.36.1",
  "enabled": true,
  "default_profile_id": "restricted",
  "registered_at": 1700000000,
  "last_launched_at": 1700000500,
  "launch_count": 3
}
```

### 2.2 Application Resource Profile (`ApplicationProfile`)
```json
{
  "db_id": 3,
  "profile_id": "restricted",
  "name": "Restricted Sandbox",
  "description": "0.5 CPU, 128MB RAM, 20 PIDs",
  "cpu_cores": 0.50,
  "cpu_quota_us": 50000,
  "cpu_period_us": 100000,
  "cpu_weight": 100,
  "memory_max_bytes": 134217728,
  "pids_max": 20,
  "io_read_bps": 5242880,
  "io_write_bps": 5242880,
  "created_at": 1700000000,
  "updated_at": 1700000000
}
```

### 2.3 Application Instance (`ApplicationInstance`)
```json
{
  "db_id": 12,
  "instance_id": "inst_1700000500_abc12",
  "app_id": "busybox_worker",
  "vm_id": "alpine_01",
  "guest_pid": 2342,
  "process_start_time": 1045230,
  "cgroup_path": "virtualos/applications/app_busybox_worker",
  "state": "RUNNING",
  "profile_id": "restricted",
  "started_at": 1700000500,
  "stopped_at": 0,
  "exit_code": -1,
  "failure_reason": ""
}
```

### 2.4 Time-Series Resource Sample (`ApplicationResourceSample`)
```json
{
  "instance_id": "inst_1700000500_abc12",
  "app_id": "busybox_worker",
  "vm_id": "alpine_01",
  "cpu_usage_pct": 49.8,
  "cpu_time_usec": 1019598,
  "throttled_usec": 952635,
  "memory_current_bytes": 16982016,
  "memory_peak_bytes": 25690112,
  "pids_current": 1,
  "pids_max": 20,
  "io_read_bytes": 0,
  "io_write_bytes": 0,
  "sample_timestamp": 1700000502,
  "freshness": "FRESH",
  "verification_state": "VERIFIED"
}
```

---

## 3. C Backend API Reference

### 3.1 Subsystem Lifecycle
```c
AppResult app_manager_init(const char *db_path);
void      app_manager_shutdown(void);
const char *app_result_str(AppResult res);
const char *app_state_name(AppState state);
AppState    app_state_from_name(const char *name);
```

### 3.2 Registry CRUD
```c
AppResult app_manager_register_app(const char *vm_id, const char *app_id, const char *name,
                                   const char *executable_path, const char *description,
                                   const char *category, const char *version,
                                   const char *default_profile_id, char *err_buf, size_t err_sz);

AppResult app_manager_get_app(const char *vm_id, const char *app_id, ApplicationEntry *out_app,
                              char *err_buf, size_t err_sz);

AppResult app_manager_list_apps(const char *vm_id, const char *category_filter,
                                ApplicationEntry *out_apps, int max_apps, int *out_count);

AppResult app_manager_update_app(const char *vm_id, const char *app_id, const char *name,
                                 const char *description, const char *category,
                                 const char *version, const char *default_profile_id,
                                 char *err_buf, size_t err_sz);

AppResult app_manager_unregister_app(const char *vm_id, const char *app_id, char *err_buf, size_t err_sz);

AppResult app_manager_set_enabled(const char *vm_id, const char *app_id, int enabled,
                                  char *err_buf, size_t err_sz);
```

### 3.3 Application Discovery
```c
AppResult app_manager_discover_apps(const char *vm_id, AgentAppDiscoveryList *out_discovery,
                                    char *err_buf, size_t err_sz);
```

### 3.4 Resource Profiles
```c
AppResult app_manager_create_profile(const char *profile_id, const char *name, const char *description,
                                     double cpu_cores, int cpu_weight, uint64_t memory_max_bytes,
                                     int64_t pids_max, uint64_t io_read_bps, uint64_t io_write_bps,
                                     char *err_buf, size_t err_sz);

AppResult app_manager_get_profile(const char *profile_id, ApplicationProfile *out_profile,
                                  char *err_buf, size_t err_sz);

AppResult app_manager_list_profiles(ApplicationProfile *out_profiles, int max_profiles, int *out_count);

AppResult app_manager_update_profile(const char *profile_id, const char *name, const char *description,
                                     double cpu_cores, int cpu_weight, uint64_t memory_max_bytes,
                                     int64_t pids_max, uint64_t io_read_bps, uint64_t io_write_bps,
                                     char *err_buf, size_t err_sz);

AppResult app_manager_delete_profile(const char *profile_id, char *err_buf, size_t err_sz);

AppResult app_manager_assign_profile(const char *vm_id, const char *app_id, const char *profile_id,
                                     char *err_buf, size_t err_sz);
```

### 3.5 Launcher & Process Lifecycle
```c
AppResult app_manager_launch_app(const char *vm_id, const char *app_id, const char *args_str,
                                 const char *profile_id_override, char *out_instance_id,
                                 size_t instance_id_sz, char *err_buf, size_t err_sz);

AppResult app_manager_stop_app(const char *vm_id, const char *instance_id, int timeout_ms,
                               int force, char *err_buf, size_t err_sz);

AppResult app_manager_get_instance(const char *instance_id, ApplicationInstance *out_instance,
                                   char *err_buf, size_t err_sz);

AppResult app_manager_list_instances(const char *vm_id, AppState state_filter,
                                     ApplicationInstance *out_instances, int max_instances,
                                     int *out_count);

AppResult app_manager_reconcile_instances(const char *vm_id, int *out_reconciled_count);
```

### 3.6 Telemetry & Time-Series Sampling
```c
AppResult app_manager_collect_metrics(const char *vm_id, const char *instance_id,
                                      ApplicationResourceSample *out_sample,
                                      char *err_buf, size_t err_sz);

AppResult app_manager_get_samples(const char *instance_id, ApplicationResourceSample *out_samples,
                                  int max_samples, int *out_count);

AppResult app_manager_log_event(const char *instance_id, const char *app_id, const char *vm_id,
                                const char *event_type, const char *details);
```

---

## 4. Dart FFI Integration Example

```dart
import 'dart:ffi' as ffi;
import 'package:ffi/ffi.dart';

// 1. Define FFI Signatures
typedef AppManagerInitC = ffi.Int32 Function(ffi.Pointer<Utf8> dbPath);
typedef AppManagerInitDart = int Function(ffi.Pointer<Utf8> dbPath);

typedef AppManagerLaunchAppC = ffi.Int32 Function(
    ffi.Pointer<Utf8> vmId,
    ffi.Pointer<Utf8> appId,
    ffi.Pointer<Utf8> argsStr,
    ffi.Pointer<Utf8> profileOverride,
    ffi.Pointer<Utf8> outInstanceId,
    ffi.Size instanceIdSz,
    ffi.Pointer<Utf8> errBuf,
    ffi.Size errSz);

typedef AppManagerLaunchAppDart = int Function(
    ffi.Pointer<Utf8> vmId,
    ffi.Pointer<Utf8> appId,
    ffi.Pointer<Utf8> argsStr,
    ffi.Pointer<Utf8> profileOverride,
    ffi.Pointer<Utf8> outInstanceId,
    int instanceIdSz,
    ffi.Pointer<Utf8> errBuf,
    int errSz);

// 2. Client Wrapper Class
class VirtualOSClient {
  late ffi.DynamicLibrary _lib;
  late AppManagerInitDart _init;
  late AppManagerLaunchAppDart _launchApp;

  VirtualOSClient() {
    _lib = ffi.DynamicLibrary.open('libvirtual_os.dylib');
    _init = _lib.lookupFunction<AppManagerInitC, AppManagerInitDart>('app_manager_init');
    _launchApp = _lib.lookupFunction<AppManagerLaunchAppC, AppManagerLaunchAppDart>('app_manager_launch_app');
  }

  void initialize(String dbPath) {
    using((arena) {
      final pPath = dbPath.toNativeUtf8(allocator: arena);
      final rc = _init(pPath);
      if (rc != 0) throw Exception("Failed to initialize app manager (code: $rc)");
    });
  }

  String launch(String vmId, String appId, {String? args, String? profile}) {
    return using((arena) {
      final pVm = vmId.toNativeUtf8(allocator: arena);
      final pApp = appId.toNativeUtf8(allocator: arena);
      final pArgs = args != null ? args.toNativeUtf8(allocator: arena) : ffi.nullptr;
      final pProf = profile != null ? profile.toNativeUtf8(allocator: arena) : ffi.nullptr;
      final pInstId = arena<ffi.Uint8>(96).cast<Utf8>();
      final pErr = arena<ffi.Uint8>(256).cast<Utf8>();

      final rc = _launchApp(pVm, pApp, pArgs, pProf, pInstId, 96, pErr, 256);
      if (rc != 0) {
        throw Exception("Launch failed: ${pErr.toDartString()} (code: $rc)");
      }
      return pInstId.toDartString();
    });
  }
}
```

