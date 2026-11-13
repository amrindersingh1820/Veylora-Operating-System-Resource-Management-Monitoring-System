# Linux cgroups v2 Resource Enforcement Guide

This document describes the design, setup, architecture, and verification of Linux Control Groups v2 (`cgroups v2`) used by the **Virtual OS Simulator** inside the Alpine Linux ARM64 guest on Apple Silicon (QEMU with HVF acceleration).

---

## 1. Architectural Overview

Linux cgroups v2 provides a unified process hierarchy where all active resource controllers (`cpu`, `memory`, `pids`, `io`) operate under a single directory tree rooted at `/sys/fs/cgroup`.

```
/sys/fs/cgroup/                                 (Unified Root)
  ├── cgroup.controllers                        (cpuset cpu io memory hugetlb pids)
  ├── cgroup.subtree_control                    (+cpu +memory +pids +io)
  └── virtualos/                                (Virtual OS Base)
        ├── cgroup.subtree_control              (+cpu +memory +pids +io)
        └── applications/                       (Application Cgroup Namespace)
              ├── cgroup.subtree_control        (+cpu +memory +pids +io)
              ├── <group_id_1>/                 (Dedicated App Cgroup)
              │     ├── cpu.max                 (50000 100000 -> 50% CPU)
              │     ├── cpu.weight              (150)
              │     ├── cpu.stat                (usage_usec, user_usec, system_usec)
              │     ├── memory.max              (268435456 -> 256 MB)
              │     ├── memory.current          (Live RAM usage in bytes)
              │     ├── memory.events           (oom_kill counter)
              │     ├── pids.max                (50 -> Max 50 tasks)
              │     ├── pids.current            (Live task counter)
              │     ├── io.max                  (254:0 rbps=... wbps=...)
              │     └── cgroup.procs            (Attached PIDs)
              └── <group_id_2>/
```

### Key cgroups v2 Invariants

1. **No-Internal-Process Constraint**: A parent cgroup that has enabled controllers via `cgroup.subtree_control` cannot host leaf processes directly; processes must reside only in leaf nodes (e.g., `/sys/fs/cgroup/virtualos/applications/<group_id>/cgroup.procs`).
2. **Subtree Controller Propagation**: Child cgroups can only activate controllers that have been explicitly enabled in every ancestor's `cgroup.subtree_control` file.
3. **Unified Atomic Accounting**: A process belongs to exactly one cgroup in the hierarchy, eliminating controller conflicts and discrepancies present in cgroups v1.

---

## 2. Kernel & Distribution Compatibility

Empirical probing on the project's guest image confirms:
- **Guest OS**: Alpine Linux v3.20 (ARM64 / aarch64)
- **Linux Kernel**: `6.6.31-0-lts`
- **Supported Controllers**: `cpuset`, `cpu`, `io`, `memory`, `hugetlb`, `pids`
- **GPU Acceleration**: Strictly unsupported under QEMU HVF on Apple Silicon; reports `gpu_supported: false` and `gpu_status: "UNSUPPORTED_BY_HOST_HYPERVISOR"` (no simulated fake acceleration).

---

## 3. Automated Initialization & Setup Protocol

In minimal environments or Live CD boots (such as Alpine Linux Live ISO), `/sys/fs/cgroup` is not mounted by default with cgroup2. The `vos_guest_agent` executes the following automatic initialization sequence upon startup:

```c
/* 1. Mount unified cgroup2 filesystem if not already active */
if (stat("/sys/fs/cgroup/cgroup.controllers", &st) != 0) {
    mkdir("/sys/fs/cgroup", 0755);
    mount("none", "/sys/fs/cgroup", "cgroup2", 0, NULL);
}

/* 2. Enable controllers in root subtree_control */
write_file("/sys/fs/cgroup/cgroup.subtree_control", "+cpu +memory +pids +io\n");

/* 3. Create virtualos base and enable controllers */
mkdir("/sys/fs/cgroup/virtualos", 0755);
write_file("/sys/fs/cgroup/virtualos/cgroup.subtree_control", "+cpu +memory +pids +io\n");

/* 4. Create applications base and enable controllers */
mkdir("/sys/fs/cgroup/virtualos/applications", 0755);
write_file("/sys/fs/cgroup/virtualos/applications/cgroup.subtree_control", "+cpu +memory +pids +io\n");
```

---

## 4. Resource Controllers & Parameter Mapping

### A. CPU Resource Management

1. **CPU Quota & Period (`cpu.max`)**:
   - Format: `"<quota_us> <period_us>\n"`.
   - Default period: `100000` us (100 ms).
   - Core conversion formula:
     $$\text{quota\_us} = \lfloor \text{cpu\_cores} \times \text{period\_us} \rfloor$$
     * Example: 1.5 cores with 100,000 us period $\rightarrow$ `150000 100000`.
     * Example: 0.5 cores (50% CPU cap) with 100,000 us period $\rightarrow$ `50000 100000`.
   - Unconstrained / unlimited quota: `"max 100000\n"`.

2. **CPU Weight (`cpu.weight`)**:
   - Proportional share when CPU contention exists.
   - Allowed range: `1` to `10000` (Default: `100`).

3. **CPU Statistics (`cpu.stat`)**:
   - Parsed fields: `usage_usec` (total CPU time consumed), `user_usec`, `system_usec`.

### B. Memory Resource Management & Accounting

1. **Memory Ceiling (`memory.max`)**:
   - Format: `"<max_bytes>\n"` or `"max\n"` for unconstrained.
   - Sets the hard memory ceiling. When usage reaches `memory.max`, the kernel executes page reclamation. If reclamation cannot free enough memory, the in-cgroup OOM killer is triggered (killing member processes, e.g. with exit status 137).
   - **Critical Safety Guardrail**: Prior to applying a lowered memory limit, `cgroup_manager` reads `memory.current`. If $\text{requested\_max} < \text{memory.current}$, the request is rejected with `LIMIT_BELOW_CURRENT_USAGE` rather than killing guest applications with an immediate OOM abort.

2. **Precise Linux cgroups v2 Memory Accounting Fields**:
   - `memory.current`: The total amount of physical memory (in bytes) currently charged to the cgroup and its descendant cgroups. It accounts for **anonymous memory, file cache (page cache), and kernel memory structures (e.g., page tables, slab buffers)**. It does **NOT** include swap space.
   - `memory.swap.current`: Tracks swap space usage separately.
   - `memory.peak`: The historical high-water mark (in bytes) of memory usage reached since the cgroup was instantiated. Supported on Linux 6.6+.
   - `memory.events`: Monotonically increasing event counters tracking `low`, `high`, `max` (number of times `memory.max` was hit), `oom` (number of times the cgroup entered an out-of-memory condition), and `oom_kill` (number of tasks terminated by the memcg OOM killer).
   - `memory.stat`: Granular breakdown of memory components, distinguishing `anon`, `file`, `kernel`, `slab`, `sock`, and `file_mapped`.

### C. Process & Task Count Management (`pids.max`, `pids.current`)

- Protects the guest OS against fork bombs and uncontrolled resource exhaustion.
- Format: `"<max_pids>\n"` or `"max\n"`.
- `pids.current`: Returns the live count of **active tasks** currently running within the cgroup. Crucially, in the Linux kernel, **every POSIX thread created via `clone()` or `pthread_create()` is counted as an independent task**, not just top-level processes.
- Syscall Behavior: When a process inside the cgroup executes `fork()` or `clone()` while `pids.current >= pids.max`, the kernel immediately fails the call and returns `EAGAIN` (`Resource temporarily unavailable`), and increments the `max` counter in `pids.events`.

### D. Block I/O Management (`io.max`, `io.stat`)

- Format: `"<major>:<minor> rbps=<bps> wbps=<bps>\n"`.
- Block device detection resolves `/dev/vda` (`st_rdev` major 253, minor 0 on Alpine Linux ARM64).
- Readback verification ensures the major:minor limit was committed by the blkio controller.
- `io.stat`: Reports cumulative I/O bytes (`rbytes`, `wbytes`) and I/O operations (`rios`, `wios`) charged to the cgroup.

---

## 5. Process Lifecycle & Attachment Safety

1. **System Init Protection**:
   Attaching PID $\le 1$ (`/sbin/init` or system processes) is strictly rejected with `PERMISSION_DENIED`.
2. **Process Existence & PID Reuse Protection**:
   - `cgroup_read_process_starttime()` parses `/proc/<pid>/stat`, reliably extracting token 19 after the closing parenthesis (`starttime`, field 22) to handle process command names containing spaces and nested parentheses.
   - When an `expected_starttime` is supplied, `cgroup_attach_process_with_identity()` verifies that the target process has not terminated and been replaced by a recycled PID.
   - The process identity is re-checked immediately before writing into `cgroup.procs`. If the PID disappeared or start time changed, the operation fails with `AGENT_STATUS_PROCESS_IDENTITY_CHANGED`.
3. **Hardened Cgroup Identifier Validation**:
   `cgroup_is_safe_name()` enforces strict validation rejecting empty strings, `.`, `..`, leading `.` or `-`, paths with `/` or `\`, shell metacharacters (`$`, `;`, `&`, `|`, `>`, `<`, `*`, `?`), lengths $>64$, and reserved cgroup names (`applications`, `virtualos`, `cgroup`, `root`, `cgroup.*`, `cpu.*`, `memory.*`, `pids.*`, `io.*`).
4. **Attachment Verification**:
   After writing to `cgroup.procs`, the file is read back to confirm that the PID was successfully migrated by the kernel scheduler.
5. **Populated Group Removal Rejection**:
   To strictly preserve process isolation and adhere to cgroups v2 hierarchy rules, **processes are never silently dumped into the root cgroup**. If `cgroup.procs` contains active processes, `cgroup_remove_group()` rejects the deletion with `AGENT_STATUS_GROUP_BUSY` (matching the kernel's native `EBUSY` error). Only empty groups can be unlinked and deleted.

---

## 6. CLI Verification Examples

```bash
# 1. Query guest cgroup v2 capabilities
virtualos resource capabilities alpine-test

# 2. Create resource group for desktop application
virtualos resource create alpine-test app_photoshop "Adobe Photoshop Wine"

# 3. Enforce 1.5 vCPUs (150% CPU quota) and 512 MB RAM
virtualos resource set-cpu alpine-test app_photoshop 1.5
virtualos resource set-memory alpine-test app_photoshop 512

# 4. Limit to 30 concurrent processes
virtualos resource set-pids alpine-test app_photoshop 30

# 5. Attach application PID 1024
virtualos resource attach alpine-test app_photoshop 1024

# 6. Read verified live status
virtualos resource status alpine-test app_photoshop
```

