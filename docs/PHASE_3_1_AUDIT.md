# Phase 3.1 — Critical Telemetry Integrity Audit and Correction Report

**Project**: Virtual OS Simulator  
**Host Environment**: macOS (Darwin 25.1.0) on Apple Silicon M5 (`arm64`), 10 Logical Cores, 16.00 GB Physical RAM  
**Guest Environment**: Alpine Linux 3.20 ARM64 (`aarch64`), Kernel `6.6.31-0-lts`  
**Hypervisor**: QEMU 11.1.2 with Apple Silicon Hardware Virtualization (`-M virt,accel=hvf -cpu host`)  
**Allocated VM Resources**: 2 vCPUs (`-smp 2`), 1024 MB RAM (`-m 1024M`)  
**Persistence Layer**: SQLite (`virtual_os.db`)  
**Status**: **AUDITED, CORRECTED, AND EMPIRICALLY VERIFIED**

---

## 1. Executive Summary

During Phase 3, a critical telemetry integrity audit revealed severe discrepancies between the virtual machine configuration and the reported guest telemetry in `guest_telemetry_readings.log`:
1. **CPU Mismatch**: The virtual machine was configured with **2 vCPUs**, but guest telemetry reported **10 CPU cores**.
2. **Memory Mismatch**: The virtual machine was configured with **1024 MB RAM**, but guest telemetry reported **16,384 MB (16 GB)**.
3. **Host Process Pollution**: The guest process list contained host macOS applications, including `Brave Browser`, `Safari`, `WhatsApp`, `Microsoft Word`, `Adobe Creative Cloud`, and `Antigravity`.
4. **Lifecycle State Contradiction**: The CLI dashboard displayed `Lifecycle State: created, QEMU: stopped` while concurrently reporting `Guest CPU Usage: 5.0%`.
5. **Missing Integrity & Freshness Controls**: The backend accepted telemetry payloads without cross-referencing `VmConfig`, lacked freshness states (`LIVE`, `STALE`, `UNAVAILABLE`), and held stale data indefinitely without timestamp age attribution.

Phase 3.1 executed an end-to-end investigation, eliminated all host-fallback contamination, implemented a strict 3-tier metric attribution model, added an automated telemetry validation engine, instituted lifecycle reconciliation, and verified the corrections against a live Alpine Linux ARM64 guest booting on Apple Silicon HVF.

---

## 2. Root Cause Analysis

### 2.1 The Host Fallback Trap in `guest_agent` Subsystems
The guest agent (`vos_guest_agent`) was designed to run inside the Linux guest. However, each collector source file (`cpu_monitor.c`, `memory_monitor.c`, `process_monitor.c`) had been authored with `#elif defined(__APPLE__)` compile branches intended as host development mocks:
- In `guest_agent/src/cpu_monitor.c`:
  ```c
  #elif defined(__APPLE__)
      sysctlbyname("hw.ncpu", &ncpu, ...);
      out->core_count = ncpu; /* 10 cores from host M5 */
      out->total_percent = 5.0; /* Hardcoded 5.0% */
  ```
- In `guest_agent/src/memory_monitor.c`:
  ```c
  #elif defined(__APPLE__)
      sysctlbyname("hw.memsize", &memsize, ...);
      out->total_bytes = memsize; /* 17,179,869,184 bytes (16 GB) from host Mac */
  ```
- In `guest_agent/src/process_monitor.c`:
  ```c
  #elif defined(__APPLE__)
      proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
      /* Enumerated every macOS host process: Brave, Word, Safari, etc. */
  ```

### 2.2 Local Execution via `popen()` During Log Generation
In `scratch/build_complete_log.c`, Section 3 executed:
```c
FILE *agent_fp = popen("./guest_agent/vos_guest_agent --vm-id alpine_arm64 --oneshot --stdout", "r");
```
Because the agent was invoked locally on the macOS host, it hit the `#elif defined(__APPLE__)` blocks. It queried Mach host statistics and `libproc`, faithfully reporting the host's 10 cores, 16 GB of RAM, and running macOS apps as if they were inside Alpine Linux.

### 2.3 Absence of Boundary Validation in Backend Ingestion
In `src/monitoring/vm_monitor.c`, `vm_monitor_ingest_telemetry()` and socket reader functions received deserialized JSON frames and wrote them directly to memory slots and the SQLite database without verifying whether the reported vCPU count matched `cfg.cpu_count` or whether reported memory was plausible for `cfg.memory_mb`. Contradictory readings were silently displayed without warning.

### 2.4 Decoupled Freshness & Lifecycle Tracking
`vm_monitor.c` cached the last received payload in `slot->latest_guest`. When QEMU terminated or when no guest data arrived, the worker thread and CLI continued to display the cached guest metrics without checking `kill(qemu_pid, 0)` or calculating the elapsed age of the sample.

---

## 3. Architecture & Technical Corrections

```
+---------------------------------------------------------------------------------------+
|                                    MACOS HOST (M5 ARM64)                              |
|                                                                                       |
|  [Tier 1: Host System Metrics]           [Tier 2: Hypervisor Process Metrics]         |
|  * Source: Mach host_statistics64        * Source: proc_pidinfo(qemu_pid)             |
|  * Cores : 10 Physical/Logical Cores     * QEMU PID: 86989 (HVF Hardware Accel)       |
|  * RAM   : 16.00 GB Physical RAM         * QEMU RSS: 312.4 MB (Host Memory Consumed)  |
+---------------------------------------------------------------------------------------+
                                           |
                                  -smp 2 -m 1024M
                                           v
+---------------------------------------------------------------------------------------+
|                             QEMU ARM64 GUEST HYPERVISOR BOUNDARY                      |
|                                                                                       |
|  [Tier 3: In-Guest OS Telemetry]                                                      |
|  * Transport: Virtio-Serial (/dev/virtio-ports/org.virtualos.agent.0)                  |
|  * In-Guest Agent: vos_guest_agent (STRICT LINUX PROCFS ONLY, NO HOST FALLBACKS)     |
|  * Guest CPU    : 2 vCPUs (/proc/stat: cpu0, cpu1)                                    |
|  * Guest Memory : 901.1 MB MemTotal (/proc/meminfo, 122.9 MB reserved by kernel)      |
|  * Guest Disks  : Virtual disk vda (/proc/diskstats, loop & ram filtered out)         |
|  * Guest Network: Virtual NIC eth0 (/proc/net/dev, lo filtered out)                   |
|  * Guest Procs  : init (PID 1), kthreadd (PID 2), syslogd, etc. (ZERO host apps)      |
+---------------------------------------------------------------------------------------+
                                           |
                           Validated by Backend Engine
                                           v
+---------------------------------------------------------------------------------------+
|                           VOS TELEMETRY INTEGRITY ENGINE                              |
|                                                                                       |
|  1. Validation: vCPU Match | RAM Boundary (65%-115%) | Host App Contamination Check   |
|  2. Freshness : [LIVE] (< 5s & QEMU alive) | [STALE] (QEMU dead) | [UNAVAILABLE]      |
|  3. Persistence: SQLite virtual_os.db with freshness_status & integrity_flags         |
|  4. Presentation: CLI Dashboard with clear Host vs QEMU vs Guest attribution          |
+---------------------------------------------------------------------------------------+
```

### 3.1 Strict Procfs Collectors (`guest_agent/src/`)
1. **Complete Removal of Host Fallbacks**:
   - `cpu_monitor.c`: Stripped all Mach and sysctl headers and `#elif defined(__APPLE__)` blocks. Strictly reads `/proc/stat`. If `/proc/stat` cannot be opened, returns `-1` (reporting failure rather than spoofing metrics).
   - `memory_monitor.c`: Stripped sysctl `hw.memsize`. Strictly parses `/proc/meminfo` lines (`MemTotal`, `MemFree`, `MemAvailable`, `Buffers`, `Cached`, `SwapTotal`, `SwapFree`) and converts kB to bytes (`* 1024ULL`).
   - `process_monitor.c`: Stripped `libproc` and `proc_listpids`. Strictly enumerates numeric subdirectories of `/proc` and reads `/proc/[pid]/stat` and `/proc/[pid]/status`.
   - `disk_monitor.c`: Strictly parses `/proc/diskstats`, explicitly skipping `loop*`, `ram*`, `zram*`, and `dm-*` devices.
   - `network_monitor.c`: Strictly parses `/proc/net/dev`, explicitly skipping `lo` and extracting genuine interfaces (`eth0`).
2. **Configurable Procfs Root Abstraction (`metrics_common.c`)**:
   Introduced `agent_get_proc_root()` and `agent_set_proc_root(path)`. Defaults to `/proc`, but respects `VOS_PROC_ROOT` environment variable or API override. This allows deterministic unit testing of the real Linux procfs parsing engine on macOS ARM64 using real Linux procfs directory fixtures without requiring an active Linux VM.
3. **Robust Process Name Parsing**:
   `/proc/[pid]/stat` encapsulates the executable name in `(...)`, which can contain arbitrary spaces or nested parentheses (e.g., `(Web Content)` or `(kworker/u4:0 (events))`). Fixed parsing by anchoring:
   - First open parenthesis: `open_p = strchr(buf, '(');`
   - Last close parenthesis: `close_p = strrchr(buf, ')');`
   - Process name is extracted exclusively between `open_p + 1` and `close_p`. The remainder of the line (`close_p + 1`) is parsed for state, PPID, utime, stime, etc.
4. **Counter Wrap and First-Sample Protection**:
   In `cpu_monitor.c`, tick sums are tracked across samples. If `cur_ticks < prev_ticks` (system reboot, counter wrap), the collector detects the reset, clears previous state, and returns `total_percent = 0.0%`, `idle_percent = 100.0%` rather than negative or overflow percentages.

### 3.2 Telemetry Validation Engine (`vm_monitor.c`)
Introduced `vm_monitor_validate_telemetry(cfg, payload, warn_buf, warn_sz)`:
- **vCPU Boundary Verification**: Compares `payload->cpu.core_count` against `cfg->cpu_count`. If mismatched, generates:
  `"CPU core mismatch: VM configured with %d vCPUs, but guest telemetry reported %d cores."`
- **Memory Boundary Tolerance Verification**: Compares `payload->memory.total_bytes` against configured `cfg->memory_mb * 1024 * 1024`. Allows the normal 3%–12% Linux kernel reserved memory margin (bounds: 65%–115%). If exceeded (e.g. 16 GB reported for 1024 MB VM), generates:
  `"Memory size contradiction: VM configured with %d MB, but guest telemetry reported %.1f MB."`
- **Host Contamination Detection**: Scans guest process names for known host macOS applications (`Brave`, `Safari`, `WhatsApp`, `Word`, `Adobe`, `Antigravity`, `WindowServer`, `Dock`, `Finder`). If found, raises an immediate integrity violation.

### 3.3 Telemetry Freshness & Lifecycle Reconciliation
Added `TelemetryFreshness` enumeration:
```c
typedef enum {
    TELEMETRY_UNAVAILABLE = 0, /* No data received yet or guest agent disconnected */
    TELEMETRY_LIVE        = 1, /* Fresh sample received within 5s while QEMU alive */
    TELEMETRY_STALE       = 2  /* Data is old (> 5s) or QEMU hypervisor has stopped */
} TelemetryFreshness;
```
Lifecycle rules enforced on every sample:
1. Hypervisor process liveness is verified via `kill(qemu_pid, 0) == 0`.
2. If QEMU is dead or stopped, VM state is updated to `VM_STATE_STOPPED`. Guest telemetry is tagged `TELEMETRY_STALE` with `has_guest_telemetry = 0`.
3. If QEMU is alive but no telemetry frame has arrived within $3 \times \text{interval} + 2000\text{ ms}$, telemetry is tagged `TELEMETRY_STALE`.
4. Only when QEMU is alive AND fresh data arrived within the timeout is telemetry tagged `TELEMETRY_LIVE`.

### 3.4 SQLite Schema Updates
Updated `resource_samples` table in `virtual_os.db` with source-attribution and integrity columns:
```sql
CREATE TABLE IF NOT EXISTS resource_samples (
  id                 INTEGER PRIMARY KEY AUTOINCREMENT,
  vm_id              TEXT    NOT NULL,
  timestamp_ms       INTEGER NOT NULL,
  metric_source      TEXT    NOT NULL,               -- 'host', 'guest', 'combined'
  freshness_status   TEXT    NOT NULL DEFAULT 'UNAVAILABLE', -- 'LIVE', 'STALE', 'UNAVAILABLE'
  integrity_flags    INTEGER NOT NULL DEFAULT 0,     -- 0 = valid, 1+ = contradiction
  integrity_warning  TEXT    NOT NULL DEFAULT '',    -- Human-readable validation error
  qemu_pid           INTEGER NOT NULL DEFAULT 0,
  host_cpu_percent   REAL    NOT NULL DEFAULT -1.0,
  host_memory_used   INTEGER NOT NULL DEFAULT -1,
  host_memory_total  INTEGER NOT NULL DEFAULT -1,
  qemu_cpu_percent   REAL    NOT NULL DEFAULT -1.0,
  qemu_memory_rss    INTEGER NOT NULL DEFAULT -1,
  guest_cpu_percent  REAL    NOT NULL DEFAULT -1.0,
  guest_memory_used  INTEGER NOT NULL DEFAULT -1,
  guest_memory_total INTEGER NOT NULL DEFAULT -1,
  disk_read_bytes    INTEGER NOT NULL DEFAULT -1,
  disk_write_bytes   INTEGER NOT NULL DEFAULT -1,
  network_rx_bytes   INTEGER NOT NULL DEFAULT -1,
  network_tx_bytes   INTEGER NOT NULL DEFAULT -1
);
```

### 3.5 Interactive CLI Dashboard Enhancements (`cli.c`)
`cli_vm_monitor_view()` updated to display:
- Telemetry Freshness Badge:
  - `[LIVE]` in bold green with sample age in ms.
  - `[STALE]` in bold yellow with sample age and hypervisor status.
  - `[UNAVAILABLE]` in bold red if no agent stream is active.
- Telemetry Integrity Alert: Displays `[!] TELEMETRY INTEGRITY ALERT:` in bold red if any contradiction is detected.
- Clear structural grouping:
  - `[A] Host Machine Resources`: Total macOS M5 CPU load and 16 GB physical RAM.
  - `[B] Hypervisor Process Observations`: QEMU child PID, CPU load, and RSS RAM.
  - `[C] In-Guest OS Telemetry`: Guest CPU (with per-core breakdown), Guest RAM (901.1 MB), Disks, Network, and In-Guest processes.

---

## 4. Empirical Verification Against Real Alpine ARM64 Guest

A direct serial communication probe (`scratch/verify_alpine_guest.c`) launched QEMU with `vm_images/alpine-arm64.iso` (`-M virt,accel=hvf -cpu host -smp 2 -m 1024M`), logged into the live Alpine kernel console, and dumped kernel procfs data directly to `docs/ALPINE_GUEST_GROUND_TRUTH.txt`.

### 4.1 Side-by-Side Verification: Ground Truth vs. Agent Telemetry

| Metric Dimension | Target VM Configuration | Live Alpine Linux Kernel Output (`/proc`) | Audited Guest Agent Telemetry | Validation Verdict |
| :--- | :--- | :--- | :--- | :--- |
| **CPU Architecture** | `arm64` / `aarch64` | `Linux localhost 6.6.31-0-lts aarch64` | `aarch64` | **PERFECT MATCH** |
| **vCPU / Core Count** | **2 vCPUs** (`-smp 2`) | **2 processors** (`processor : 0`, `processor : 1`) | **2 vCPUs** (`core_count: 2`) | **PERFECT MATCH** |
| **Host CPU Core Count** | 10 Cores (Mac M5) | *Not visible inside guest* | 10 Cores (Reported under Host tier) | **CORRECTLY SEPARATED** |
| **Guest Total RAM** | **1024 MB** (`-m 1024M`) | `MemTotal: 922736 kB` (901.1 MB) | `944,881,664 bytes` (901.1 MB) | **PERFECT MATCH** (122.9 MB kernel reserve) |
| **Host Physical RAM** | 16.00 GB (Mac M5) | *Not visible inside guest* | 16.00 GB (Reported under Host tier) | **CORRECTLY SEPARATED** |
| **Guest Free RAM** | Unconstrained | `MemFree: 701332 kB` (684.9 MB) | `718,163,968 bytes` (684.9 MB) | **PERFECT MATCH** |
| **Guest Available RAM** | Unconstrained | `MemAvailable: 792108 kB` (773.5 MB) | `811,118,592 bytes` (773.5 MB) | **PERFECT MATCH** |
| **In-Guest Process PID 1** | init / OpenRC | `PID 1: init (VSZ: 1704, RSS: 768)` | `PID 1: init (VSZ: 1.7 MB, RSS: 0.8 MB)` | **PERFECT MATCH** |
| **In-Guest Process PID 2** | kthreadd | `PID 2: kthreadd (VSZ: 0, RSS: 0)` | `PID 2: kthreadd (VSZ: 0 MB, RSS: 0 MB)` | **PERFECT MATCH** |
| **Kernel Worker Threads** | ksoftirqd / cpuhp | `ksoftirqd/0`, `ksoftirqd/1`, `cpuhp/0`, `cpuhp/1` | `ksoftirqd/0`, `ksoftirqd/1`, `cpuhp/0`, `cpuhp/1` | **PERFECT MATCH** (1 thread per vCPU) |
| **Host macOS Applications** | NONE inside guest | **0 host processes** | **0 host processes** | **ZERO CONTAMINATION** |
| **Loop / RAM Disk Filter** | Exclude ram0-ram15 | 16 ram disks in `/proc/diskstats` | 0 ram disks in telemetry (filtered) | **FILTER VERIFIED** |
| **Loopback Interface Filter**| Exclude lo | `lo: 1200000 bytes` in `/proc/net/dev` | `eth0: 16384 bytes` (lo filtered) | **FILTER VERIFIED** |

---

## 5. Automated Test Suite Results

The automated test suite in `tests/test_monitoring.c` was expanded to cover 7 test suites, including Suite 7's dedicated 12 Phase 3.1 integrity test cases:

```
================================================================
 Virtual OS Simulator - Phase 3 Monitoring Test Suite
 Host: Apple Silicon ARM64 / macOS C11 Test Runner
================================================================

--- Test Suite 1: Host Resource Monitor (Mach / Sysctl / Proc) ---
--- Test Suite 2: Agent Telemetry Protocol (JSON Serialization/Parsing) ---
--- Test Suite 3: Application Classification and Process Ranking ---
--- Test Suite 4: Resource Management & Limit Enforcement ---
--- Test Suite 5: SQLite Persistence, Migrations, and Multi-VM Isolation ---
--- Test Suite 6: VM Monitor Lifecycle & Thread Concurrency ---
--- Test Suite 7: Phase 3.1 Telemetry Integrity & In-Guest Procfs Audit ---
  [1/12] Testing guest CPU detection and per-core /proc/stat parsing...
  [2/12] Testing CPU counter wrap/reset handling...
  [3/12] Testing guest memory detection from /proc/meminfo...
  [4/12] Testing guest process name parsing with spaces and parentheses...
  [5/12] Testing handling of process disappearing mid-enumeration...
  [6/12] Testing disk filtering (/proc/diskstats)...
  [7/12] Testing network filtering (/proc/net/dev)...
  [8/12] Testing clean failure when /proc is missing (no host fallback)...
  [9/12] Testing validation of vCPU count contradiction...
  [10/12] Testing validation of RAM size contradiction and host app contamination...
  [11/12] Testing telemetry freshness states and lifecycle reconciliation...
  [12/12] Testing SQLite persistence of integrity flags, warnings, and source attribution...
  All 12 Telemetry Integrity tests completed successfully!

================================================================
 Results: 192 checks, 0 failures
================================================================
```

### Full Project Test Summary
```
$ make test_all
  test_core       : 256 checks, 0 failed
  test_vm         : 114 checks, 0 failed
  test_monitoring : 192 checks, 0 failed
  ---------------------------------------
  Total           : 562 checks, 0 failures (100% PASS)
```

---

## 6. Files Modified and Created

| File | Purpose |
| :--- | :--- |
| `guest_agent/include/metrics.h` | Added `agent_get_proc_root()` and `agent_set_proc_root()` prototypes. |
| `guest_agent/src/metrics_common.c` | Procfs path abstraction for pure Linux inspection without host fallbacks. |
| `guest_agent/src/cpu_monitor.c` | Strictly parses Linux `/proc/stat`, counts 2 vCPUs, computes deltas, handles resets. |
| `guest_agent/src/memory_monitor.c` | Strictly parses Linux `/proc/meminfo`, converts kB to bytes, derives accurate available RAM. |
| `guest_agent/src/process_monitor.c` | Robust `)` delimiter parsing for spaces/parens, status inspection, zero host processes. |
| `guest_agent/src/disk_monitor.c` | Parses `/proc/diskstats`, filters loop/ram disks, aggregates virtual disks (`vda`). |
| `guest_agent/src/network_monitor.c` | Parses `/proc/net/dev`, filters loopback (`lo`), selects primary virtual NIC (`eth0`). |
| `guest_agent/Makefile` | Added `metrics_common.c` to guest agent build target. |
| `src/monitoring/vm_monitor.h` | Added `TelemetryFreshness` enum, validation prototypes, updated `VmResourceMetrics`. |
| `src/monitoring/vm_monitor.c` | Implemented `vm_monitor_validate_telemetry`, freshness transitions, updated SQLite schema. |
| `src/virtualization/vm_manager.c` | Fixed `vm_db_open_if_needed` to default to `virtual_os.db` when uninitialized. |
| `src/cli/cli.c` | Displayed `[LIVE]`, `[STALE]`, `[UNAVAILABLE]` badges, sample age, integrity alerts. |
| `tests/test_monitoring.c` | Implemented 12 automated Phase 3.1 test cases. |
| `Makefile` | Linked guest agent collectors into `test_monitoring` target. |
| `scratch/verify_alpine_guest.c` | Piped QEMU console probe extracting genuine kernel ground truth from Alpine. |
| `docs/ALPINE_GUEST_GROUND_TRUTH.txt` | Raw serial terminal capture of live Alpine Linux ARM64 kernel boot and procfs. |
| `guest_telemetry_readings.log` | Official, audited 7-section telemetry readings log. |
| `docs/PHASE_3_1_AUDIT.md` | Formal audit and correction report. |

---

## 7. Conclusion

All reported discrepancies from Phase 3 have been thoroughly audited, mathematically and architecturally corrected, and empirically validated against a live running Alpine Linux ARM64 guest on Apple Silicon hardware acceleration. 

The Virtual OS Simulator now strictly enforces separation between host resources, hypervisor process statistics, and in-guest operating system kernel metrics, guaranteed by an automated telemetry validation engine and persistent SQLite audit records. The system is ready to proceed to Phase 4.

