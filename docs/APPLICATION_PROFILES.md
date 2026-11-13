# Application Resource Profiles and Cgroups v2 Mapping Specification

**Project**: Virtual OS Simulator  
**Host Architecture**: macOS Apple Silicon M5 (`arm64`)  
**Guest Environment**: Alpine Linux ARM64 (Linux Kernel `6.6.31-0-lts`)  
**Status**: ACTIVE  

---

## 1. Profile Concept & Motivation

In multi-tenant or sandboxed virtualized environments, individual applications must not be allowed to exhaust system resources, starve other services, or crash the guest operating system via Out-Of-Memory (OOM) panics.

Virtual OS Simulator implements **Application Resource Profiles** to define reusable, declarative resource boundaries. Profiles bridge the gap between high-level user parameters (e.g. "0.5 CPU cores, 128 MB RAM") and low-level Linux kernel cgroups v2 control interfaces (`cpu.max`, `cpu.weight`, `memory.max`, `pids.max`, `io.max`).

---

## 2. Resource Parameter to Cgroup v2 Mapping

| Profile Attribute | C Type | Cgroup v2 File | Mathematical / Kernel Translation | Behavior & Bounds |
|---|---|---|---|---|
| `cpu_cores` | `double` | `cpu.max` | $\text{quota} = \lfloor \text{cores} \times 100,000 \rfloor$, $\text{period} = 100,000$ | CFS bandwidth quota. `0.5` cores $= 50,000\,\mu\text{s}$ per $100,000\,\mu\text{s}$. `0.0` or `-1` means `"max"` (unconstrained). |
| `cpu_weight` | `int` | `cpu.weight` | Proportional share (1 to 10,000) | Relative scheduling weight when CPU is contested. Default: `100`. |
| `memory_max_bytes` | `uint64_t` | `memory.max` | Bytes in decimal string format | Hard memory ceiling. Reaching limit triggers page reclamation and OOM kill if unreclaimable. `0` means `"max"`. |
| `pids_max` | `int64_t` | `pids.max` | Number of tasks (threads + processes) | Prevents fork bombs. Reaching limit causes `fork()` to return `EAGAIN`. `-1` means `"max"`. |
| `io_read_bps` | `uint64_t` | `io.max` | `<maj>:<min> rbps=<bytes>` | Throttles sequential and random read throughput on block device. `0` means unlimited. |
| `io_write_bps` | `uint64_t` | `io.max` | `<maj>:<min> wbps=<bytes>` | Throttles sequential and random write throughput on block device. `0` means unlimited. |

---

## 3. Standard Provisioned Profiles

Virtual OS Simulator automatically seeds three standard profiles upon system initialization:

### 3.1 `default`
Designed for unrestricted administrative utilities or legacy scripts.
* **CPU Allocation**: Unconstrained (`cpu.max = max`)
* **CPU Weight**: 100
* **Memory Limit**: Unconstrained (`memory.max = max`)
* **Process Limit**: Unconstrained (`pids.max = max`)
* **Disk I/O Limit**: Unconstrained
* **Intended Use**: Maintenance commands, interactive debugging shells.

### 3.2 `performance`
Designed for heavy computational jobs that require high thread concurrency and elevated scheduling priority.
* **CPU Allocation**: 1.50 Cores ($150,000\,\mu\text{s} / 100,000\,\mu\text{s}$)
* **CPU Weight**: 200 (Double standard scheduling priority)
* **Memory Limit**: 512 MB ($536,870,912\text{ bytes}$)
* **Process Limit**: 50 active tasks
* **Disk I/O Limit**: Unconstrained
* **Intended Use**: Compilations, data transformations, multi-threaded analytics.

### 3.3 `restricted`
Designed for untrusted code execution, background worker loops, or isolated multi-tenant services.
* **CPU Allocation**: 0.50 Cores ($50,000\,\mu\text{s} / 100,000\,\mu\text{s}$)
* **CPU Weight**: 100 (Standard priority)
* **Memory Limit**: 128 MB ($134,217,728\text{ bytes}$)
* **Process Limit**: 20 active tasks
* **Disk I/O Limit**: 5 MB/s ($5,242,880\text{ B/s}$)
* **Intended Use**: Background polling tasks, sandboxed scripts, untrusted binaries.

---

## 4. Custom Profile Lifecycle & Safety

### 4.1 Creating Custom Profiles
Administrators can define custom profiles dynamically via CLI or C API:
```bash
./virtual_os_cli app profile-create batch_job "Batch Processing Profile" 0.75 256 30 10
```
Parameters:
1. `profile_id`: `batch_job` (alphanumeric, dash, underscore only)
2. `name`: Display title
3. `cpu_cores`: `0.75` (75% of 1 core)
4. `mem_mb`: `256` (256 MB)
5. `pids_max`: `30` tasks
6. `io_mb_s`: `10` MB/s read/write throttle

### 4.2 In-Use Deletion Guard
To prevent breaking active applications or running instances, `app_manager_delete_profile()` executes a database integrity check:
```sql
SELECT COUNT(*) FROM applications WHERE default_profile_id = ?;
```
If any registered application is currently mapped to the profile, deletion is rejected with error code `APP_ERR_INVALID_PARAM` and the explanation:
```
Profile 'batch_job' is in use by 2 registered applications
```

### 4.3 Runtime Profile Overrides
When launching an application with `app_manager_launch_app()`, a caller may optionally supply a `profile_id_override`. If provided, the override profile takes precedence over the application's default profile for that specific execution instance, while preserving the application's permanent registry default.

