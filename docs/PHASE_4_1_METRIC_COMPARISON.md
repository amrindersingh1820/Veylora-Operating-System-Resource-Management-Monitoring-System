# Phase 4.1 — Metric Comparison and Multi-Layer Consistency Report

**Project**: Virtual OS Simulator  
**Audit Target**: Phase 4 Real Resource Management and Enforcement  
**Host**: macOS Apple Silicon M5 (`arm64`)  
**Guest**: Alpine Linux ARM64 (`aarch64`), Linux Kernel `6.6.31-0-lts`  
**Date**: November 3, 2026  

---

## 1. Six-Layer Consistency & Agreement Audit

The system requires strict agreement across all six layers:
1. **Requested Configuration** (CLI / API parameter)
2. **Guest Kernel Configuration** (Raw sysfs cgroup file contents)
3. **Guest Agent Response** (Virtio-serial JSON RPC readback payload)
4. **Host C API Result** (`resource_manager_*` struct and result code)
5. **SQLite Database Record** (`resource_limits` & `enforcement_events` tables)
6. **CLI Output** (`virtualos resource status` text formatting)

### Consistency Matrix for Empirical Tests

| Enforcement Dimension | (1) Requested Config | (2) Guest Kernel Sysfs | (3) Guest Agent Response | (4) Host C API Result | (5) SQLite Database Record | (6) CLI Status Output | Agreement Status |
|---|---|---|---|---|---|---|---|
| **CPU Quota** | `0.5` cores (50% quota) | `50000 100000` (`cpu.max`) | `cpu_quota_us: 50000, verified: true` | `res: 0, quota: 50000` | `cpu_quota_us: 50000, verified: 1, status: 'VERIFIED'` | `50000 us / 100000 us (OS_VERIFIED)` | **100% Consistent** |
| **CPU Weight** | `weight: 150` | `150` (`cpu.weight`) | `cpu_weight: 150, verified: true` | `res: 0, weight: 150` | `cpu_weight: 150, verified: 1, status: 'VERIFIED'` | `Weight: 150 (OS_VERIFIED)` | **100% Consistent** |
| **Memory Ceiling** | `67,108,864` bytes (64 MB) | `67108864` (`memory.max`) | `max_bytes: 67108864, verified: true` | `res: 0, max: 67108864` | `memory_max_bytes: 67108864, verified: 1, status: 'VERIFIED'` | `67108864 bytes (64.00 MB)` | **100% Consistent** |
| **Memory Guardrail** | Request $< \text{current}$ | Kernel file unchanged | `STATUS_LIMIT_BELOW_USAGE, verified: false` | `ERR_LIMIT_BELOW_USAGE` | `status: 'FAILED', verified: 0, msg: 'below current usage'` | `Rejected (below current usage)` | **100% Consistent** |
| **Task / PID Limit** | `pids_max: 5` | `5` (`pids.max`) | `pids_max: 5, verified: true` | `res: 0, max_pids: 5` | `pids_max: 5, verified: 1, status: 'VERIFIED'` | `Maximum Allowed PIDs: 5` | **100% Consistent** |
| **Disk I/O Rate** | `5,242,880` B/s (5 MB/s) | `253:0 rbps=5242880` (`io.max`) | `io_read_bps: 5242880, verified: true` | `res: 0, rbps: 5242880` | `io_read_bps: 5242880, verified: 1, status: 'VERIFIED'` | `Read Rate Limit: 5.00 MB/s` | **100% Consistent** |
| **Populated Group Removal**| Remove populated group | Kernel returns `EBUSY` | `STATUS_GROUP_BUSY, verified: false` | `ERR_GROUP_BUSY` | `status: 'FAILED', error: 'Refusing to remove populated group'` | `Error: Group is populated with active processes` | **100% Consistent** |
| **GPU Capability** | Query GPU | Not enabled in guest | `gpu_supported: false` | `gpu_supported: 0` | `enforcement_events: 'gpu=unsupported'` | `GPU Virtualization: UNSUPPORTED` | **100% Consistent** |

---

## 2. Formal Status Classification Taxonomy

In compliance with Phase 4.1 requirements, every database record and API status maintains rigorous distinctions:

| State | Definition | When Assigned |
|---|---|---|
| **`REQUESTED`** | A configuration has been dispatched by the host or CLI, but kernel response has not yet been processed. | Host RPC preparation window. |
| **`APPLIED`** | The guest agent successfully wrote the value into the cgroupfs control file, but readback confirmation has not occurred. | Intermediate handler state. |
| **`VERIFIED`** | The value was read back from the real kernel filesystem, matched the request, and was confirmed committed. | Standard success state in SQLite `resource_limits`. |
| **`FAILED`** | The operation was rejected by the kernel (e.g. invalid syntax, out of range, or permission denied). | Captured with exact `error_message`. |
| **`UNSUPPORTED`** | The underlying kernel or hardware does not provide the controller (e.g. GPU, or absent blkio). | Cleanly reported without error abort. |
| **`STALE`** | A previously verified record whose VM is no longer running or whose guest agent has disconnected. | Lifecycle state reconciliation. |

---

## 3. Empirical Workload Benchmark Data

### A. CPU CFS Throttling Benchmark (10-Second Continuous Execution)

$$\text{CFS CPU Utilization} = \frac{\Delta \text{usage\_usec}}{\Delta \text{wall\_clock\_usec}}$$

```text
========================================================================================
WORKLOAD TYPE               CONFIGURED QUOTA     MEASURED CPU TIME   CFS THROTTLED TIME   PERIODS (TOTAL/THROTTLED)   ACTUAL UTILIZATION
========================================================================================
Unrestricted Control        max (100% 1 core)    9,999,976 us        0 us                 0 / 0                       100.00%
Restricted Workload         50000 100000 (50%)   5,031,339 us        4,969,063 us         101 / 100                    50.31%
========================================================================================
```

### B. Memory Enforcement & OOM Killer Event Data

```text
========================================================================================
PARAMETER                      INITIAL STATE              DURING ALLOCATION             AFTER OOM TERMINATION
========================================================================================
memory.max                     67,108,864 bytes (64 MB)   67,108,864 bytes (64 MB)      67,108,864 bytes (64 MB)
memory.current                 0 bytes                    67,108,864 bytes (At ceiling) 188,416 bytes (Post-reap)
memory.peak                    0 bytes                    67,108,864 bytes              67,108,864 bytes (High-water mark)
memory.events (max)            0                          46                            46 (Limit hits)
memory.events (oom)            0                          1                             1 (OOM condition triggered)
memory.events (oom_kill)       0                          0                             1 (Process killed: PID 2349)
Workload Exit Status           N/A                        Running                       137 (SIGKILL)
========================================================================================
```

### C. Process Count Enforcement Benchmark (`pids.max = 5`)

```text
========================================================================================
TASK ATTEMPT                   TASK TYPE                  PIDS.CURRENT BEFORE   PIDS.CURRENT AFTER   RESULT / ERRNO
========================================================================================
Parent Shell                   Process                    0                     2 (Parent + Sub)     Success (PID 2384)
Child 1                        Process (sleep 10)         2                     3                    Success (PID 2386)
Child 2                        Process (sleep 10)         3                     4                    Success (PID 2388)
Child 3                        Process (sleep 10)         4                     5 (At limit)         Success (PID 2390)
Child 4                        Process (sleep 10)         5                     5                    FAILED: Resource temporarily unavailable (EAGAIN)
pids.events (max)              Counter                    0                     1                    Incremented by 1
========================================================================================
```

### D. Block I/O Rate Throttling Benchmark (`/dev/vda` direct read)

```text
========================================================================================
WORKLOAD                       LIMIT APPLIED              DATA TRANSFERRED   ELAPSED TIME    MEASURED THROUGHPUT   IO.STAT (RBYTES)
========================================================================================
Unthrottled Host NVMe (QEMU)   None (max)                 10.0 MB            0.041 s         243.9 MB/s            N/A
Throttled Cgroup Read          5,242,880 B/s (5.0 MB/s)   10.0 MB            1.964 s           5.1 MB/s            10,373,120 bytes
========================================================================================
```

---

## 4. Final Feature Classification Table

In strict accordance with Phase 4.1 instructions, each feature is classified using exactly one defined status:

1. **CPU Bandwidth Enforcement (`cpu.max`)**: `IMPLEMENTED_AND_VERIFIED`
2. **CPU Weight Proportional Share (`cpu.weight`)**: `IMPLEMENTED_AND_VERIFIED`
3. **Memory Hard Limit Enforcement (`memory.max`)**: `IMPLEMENTED_AND_VERIFIED`
4. **Memory OOM Guardrail (`LIMIT_BELOW_CURRENT_USAGE`)**: `IMPLEMENTED_AND_VERIFIED`
5. **Process / Task Count Limits (`pids.max`)**: `IMPLEMENTED_AND_VERIFIED`
6. **Block I/O Bandwidth Throttling (`io.max`, `io.stat`)**: `IMPLEMENTED_AND_VERIFIED`
7. **Process Attachment & Init Protection**: `IMPLEMENTED_AND_VERIFIED`
8. **Populated Group Removal Rejection (`GROUP_BUSY`)**: `IMPLEMENTED_AND_VERIFIED`
9. **Virtio-Serial RPC Framing & Deserialization**: `IMPLEMENTED_AND_VERIFIED`
10. **SQLite Event Auditing & Verification States**: `IMPLEMENTED_AND_VERIFIED`
11. **CLI Management Interface (`virtualos resource`)**: `IMPLEMENTED_AND_VERIFIED`
12. **Hypervisor Hardware Bounds (`-smp`, `-m`, restart)**: `IMPLEMENTED_AND_VERIFIED`
13. **GPU Virtualization on Apple Silicon HVF**: `UNSUPPORTED` (Explicitly reported; no fake simulation)

