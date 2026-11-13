# Resource Management Architecture & Enforcement Strategy

This document details the multi-tiered resource management and enforcement architecture implemented in Phase 4 of the **Virtual OS Simulator**.

---

## 1. Multi-Tier Resource Architecture

The system enforces resources across three distinct, coordinated layers:

```
+-------------------------------------------------------------------------+
| Layer 1: Host System (macOS Apple Silicon M5 ARM64)                     |
| - Mach host metrics & Process RSS monitoring                            |
| - QEMU Process Priority Management: setpriority() nice [-20, 19]        |
| - Hypervisor boundary verification (-smp vCPUs, -m RAM MiB)             |
+-------------------------------------------------------------------------+
                                    |
                       virtio-serial RPC channel
                                    v
+-------------------------------------------------------------------------+
| Layer 2: In-Guest OS Kernel (Linux ARM64 / Alpine Linux 6.6)            |
| - Unified cgroups v2 hierarchy: /sys/fs/cgroup                          |
| - Namespace: /sys/fs/cgroup/virtualos/applications/<group_id>/          |
| - CPU Enforcement: cpu.max (quota/period), cpu.weight (proportional)   |
| - Memory Enforcement: memory.max ceiling, memory.current accounting     |
| - Task Limit Enforcement: pids.max protection against fork bombs        |
| - Disk I/O Throttling: io.max (rbps / wbps)                             |
+-------------------------------------------------------------------------+
                                    |
                           Process Attachment
                                    v
+-------------------------------------------------------------------------+
| Layer 3: Application Processes (Running inside Guest OS)                 |
| - Adobe Photoshop, Lightroom (Wine / ARM64), CLion, Node, Python        |
| - Constrained within designated cgroup leaf nodes                       |
| - Real-time metrics readback via cgroup.stat, memory.current, etc.      |
+-------------------------------------------------------------------------+
```

---

## 2. Layer 1: Hypervisor-Level Hardware Boundaries

### Hardware Enclosure via QEMU Parameters
Every virtual machine is launched with hypervisor configuration parameters:
- **vCPU Allocation (`-smp <cpu_count>`)**: Specifies the number of virtual CPUs exposed to the guest kernel. The guest OS scheduler perceives and utilizes exactly this number of logical cores.
- **Guest RAM Allocation (`-m <memory_mb>M`)**: Sets the maximum physical address space configured for the guest operating system.

### Critical Technical Distinctions (Host vs. Guest Boundaries)
To ensure technical accuracy, the system explicitly documents the following architectural boundaries:
1. **vCPU Count vs. Host CPU Usage**: vCPU count defines the virtual CPUs exposed to the guest kernel. The host QEMU process executes vCPU threads scheduled by the macOS Mach/BSD scheduler. Host CPU consumption is the aggregate of all vCPU threads plus emulator overhead; on a multi-core host, QEMU CPU usage can legally exceed 100% per core (e.g. >200% for a 2-vCPU VM during intense compilation).
2. **Guest RAM vs. Host Resident Set Size (RSS)**: Guest RAM is the configured guest physical memory. QEMU host RSS differs substantially from guest RAM: memory is demand-paged on macOS, meaning guest RAM is faulted in only as pages are touched, while QEMU internal heaps, device emulation buffers, DMA controllers, and page tables introduce host memory overhead beyond the configured guest size.
3. **Host Process Priority vs. Hard CPU Quota**: `resource_limits_apply_host_priority(qemu_pid, nice)` uses POSIX `setpriority(PRIO_PROCESS, qemu_pid, nice)`. This adjusts relative dynamic priority in the macOS Mach/BSD scheduler to yield cycles to interactive desktop applications during heavy VM workloads. It is **not** a hard host CPU bandwidth quota or CFS bandwidth limit.
4. **HVF Isolation vs. Host Syscall Sandboxing**: Apple Hypervisor.framework (`HVF`) provides ARM64 EL2 Stage 2 page-table translation isolation between the guest VM and the host physical hardware. However, HVF acceleration does **not** automatically sandbox the QEMU host userspace process itself. QEMU on macOS runs as a standard user process without Linux seccomp or macOS Seatbelt (`sandbox_init`) filters.
5. **Dynamic Hotplug Constraint (Apple Silicon HVF)**: Under macOS Hypervisor.framework (`HVF`), dynamic CPU and RAM hotplugging are not supported by the platform hypervisor APIs. Changes committed via `resource_manager_set_vm_hardware()` in SQLite are marked `requires_restart = 1` and take effect upon VM reboot.

---

## 3. Layer 2: In-Guest Application Limits via cgroups v2

Applications running within the Linux guest are isolated into fine-grained resource groups under:
`/sys/fs/cgroup/virtualos/applications/<group_id>/`

### A. CPU Resource Enforcement
1. **Bandwidth Throttling (`cpu.max`)**:
   - Accepts fractional core targets (e.g. `1.5` cores, `0.5` cores).
   - Computes microsecond quota: $\text{quota} = \text{cores} \times 100,000\,\mu\text{s}$.
   - When the quota is reached within the period, the Linux Completely Fair Scheduler (CFS) suspends execution of tasks in the cgroup until the next period window.
2. **Fair Share Scheduling (`cpu.weight`)**:
   - Proportional weight between 1 and 10,000. When multiple applications compete for CPU, cycles are distributed proportionally to weights.

### B. Memory Resource Enforcement & Accounting
1. **Hard Upper Bound (`memory.max`)**:
   - Sets the absolute physical memory ceiling in bytes.
   - When memory reaches `memory.max`, page reclamation is executed. If usage cannot be reduced, the cgroup OOM killer terminates member tasks (exit code 137).
2. **Precise Accounting Definitions**:
   - `memory.current`: Total physical memory charged to the cgroup in bytes (including anonymous memory, page cache, and kernel structures like page tables). Swap memory is tracked separately via `memory.swap.current`.
   - `memory.peak`: Highest recorded memory usage since creation.
   - `memory.events`: Event metrics counting `low`, `high`, `max` (limit hits), `oom`, and `oom_kill` actions.
3. **OOM-Kill Prevention Guardrail**:
   - In production environments, blindly setting `memory.max` lower than current usage causes the Linux kernel to immediately invoke the Out-Of-Memory (OOM) killer on member processes.
   - *Implementation Safeguard*: `cgroup_set_memory_limit()` inspects `memory.current` prior to writing `memory.max`. If the proposed limit is below active usage, the request is rejected with `LIMIT_BELOW_CURRENT_USAGE`, protecting running applications from sudden termination.

### C. Process & Task Count Enforcement (`pids.max`, `pids.current`)
- Restricts the maximum number of simultaneous tasks (both processes and POSIX threads) in the cgroup.
- When an application attempts a `fork()` or `clone()` that would exceed `pids.max`, the syscall fails with `EAGAIN` (`Resource temporarily unavailable`), safely capping runaway task creation and incrementing `pids.events` (`max`).

### D. Process Attachment & PID Reuse Protection
To prevent attaching the wrong process if a PID terminates and is quickly recycled by the Linux kernel:
1. **Procfs Stat Token Extraction**: `cgroup_read_process_starttime()` parses `/proc/<pid>/stat`, finding the first `(` and last `)` to safely bypass command names that contain spaces and nested parentheses. It extracts field 22 (`starttime`, token 19 after `)`).
2. **Identity Verification**: When `resource_manager_attach_process_with_identity()` is called with `expected_start_time`:
   - It verifies that the live process's start time matches `expected_start_time`.
   - It re-verifies immediately before writing the PID into `cgroup.procs`.
   - If the PID terminated or was recycled, it returns `AGENT_STATUS_PROCESS_IDENTITY_CHANGED` (`RESOURCE_ERR_PROCESS_IDENTITY_CHANGED`).
3. **Safety Restrictions**: System init (`PID <= 1`) is strictly protected and cannot be attached. Unsafe group identifiers containing traversal sequences or metacharacters are rejected before any syscall.

### E. Disk I/O Throttling (`io.max`, `io.stat`)
- Restricts bytes/sec (`rbps`, `wbps`) on the virtual block device `/dev/vda` (`253:0`).
- Validated with live kernel readback verification.
- Empirical benchmark in Alpine Linux guest demonstrates throttling direct I/O (`iflag=direct`) from an unthrottled baseline of $1.1\text{ GB/s}$ ($0.0089\text{ s}$ for 10 MB) down to $2.0\text{ MB/s}$ ($4.9829\text{ s}$ for 10 MB) when configured with `rbps=2097152`, verified by before-and-after deltas in `io.stat`.

---

## 4. Hardware Acceleration & GPU Policy

### Strict Non-Simulation Policy
The Virtual OS Simulator enforces absolute transparency:
- QEMU with HVF acceleration on macOS Apple Silicon does not support hardware GPU passthrough or `virtio-gpu-gl` accelerated host contexts.
- Therefore, the system explicitly reports:
  ```json
  "capabilities": {
    "gpu_supported": false,
    "gpu_status": "UNSUPPORTED_BY_HOST_HYPERVISOR"
  }
  ```
- No mock or simulated GPU metrics are generated. When GPU capabilities are queried via the CLI or C API, the status is explicitly documented as `UNSUPPORTED_BY_HOST_HYPERVISOR`.

---

## 5. Persistence, State Reconciliation, and Auditing

All resource configurations, events, and verifications are stored in `virtual_os.db` with WAL mode enabled:

1. **`resource_groups`**:
   Tracks registered groups (`group_id`, `group_name`, `cgroup_path`, `status`).
2. **`resource_limits`**:
   Maintains the active policy (`cpu_quota_us`, `cpu_period_us`, `cpu_weight`, `memory_max_bytes`, `pids_max`, `verified`).
3. **`enforcement_events`**:
   Append-only audit trail recording every enforcement attempt, requested values, applied values, verification flags, and error details.
