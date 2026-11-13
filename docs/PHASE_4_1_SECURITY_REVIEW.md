# Phase 4.1 — Security and Isolation Review

**Project**: Virtual OS Simulator  
**Platform**: macOS Apple Silicon M5 (`arm64`), QEMU HVF Hypervisor, Alpine Linux ARM64  
**Date**: November 3, 2026  

---

## 1. Security Architecture & Threat Model

The Virtual OS Simulator isolates desktop applications (such as future workloads like Adobe Photoshop, Lightroom, JetBrains IDEs, or compilers) inside sandboxed guest virtual machines. The resource management subsystem prevents guest applications from consuming excessive host or VM resources, protects host responsiveness, and prevents inter-application denial-of-service inside the guest.

```
+------------------------------------------------------------------------+
| Host OS (macOS Apple Silicon)                                          |
|  - Process Isolation: QEMU hypervisor runs as non-root user process    |
|  - Hardware Virtualization Isolation: HVF Stage 2 memory translation   |
|    (EL2) isolates guest physical memory from host physical space       |
|    (Note: QEMU host process has no syscall sandbox / seccomp on macOS) |
|  - Host Scheduling Priority: setpriority(PRIO_PROCESS, qemu_pid, nice) |
+------------------------------------------------------------------------+
                                    |
                    Isolated Virtio-Serial Socket
                    (No network exposure, UNIX domain)
                                    v
+------------------------------------------------------------------------+
| Guest Operating System (Linux ARM64 / Alpine Linux)                    |
|  - System Init Protection: PID 1 immune from cgroup migration          |
|  - Kernel Cgroups v2 Hierarchy: Independent application cgroups        |
|  - OOM Guardrails: Prevents accidental application termination         |
|  - Fork Bomb Protection: pids.max caps task & thread exhaustion        |
|  - Zero Arbitrary Execution: Strictly typed RPC wire format            |
+------------------------------------------------------------------------+
```

---

## 2. In-Depth Security Assessment by Dimension

### A. System Init & Kernel Task Protection
- **Vulnerability Analyzed**: An operator or compromised service attaching `/sbin/init` (PID 1) or vital system daemons into an application cgroup with restrictive CPU or memory bounds, inducing guest kernel panic or unresponsiveness.
- **Defensive Mechanism**: `cgroup_attach_process()` in `guest_agent/src/cgroup_manager.c`:
  ```c
  if (pid <= 1) {
      snprintf(err, err_sz, "Refusing to attach system init / PID %d", (int)pid);
      return AGENT_STATUS_PERMISSION_DENIED;
  }
  ```
- **Audit Verification**: Passed. Attempting to attach PID 1 or negative PIDs returns `AGENT_STATUS_PERMISSION_DENIED` with an audit event logged.

### B. Path Traversal & Group Identifier Sanitization
- **Vulnerability Analyzed**: Malicious group IDs such as `../../etc` or `../../../../proc` supplied via the RPC or CLI to escape `/sys/fs/cgroup/virtualos/applications/`.
- **Defensive Mechanism**: `is_safe_name()` in `guest_agent/src/cgroup_manager.c`:
  ```c
  static int is_safe_name(const char *name) {
      if (!name || !name[0]) return 0;
      for (size_t i = 0; name[i]; i++) {
          char c = name[i];
          if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.') return 0;
          if (c == '.' && name[i+1] == '.') return 0; /* No directory traversal */
      }
      return 1;
  }
  ```
- **Audit Verification**: Passed. Input containing `/`, `..`, or shell metacharacters is rejected with `AGENT_STATUS_INVALID_CONFIG` before any filesystem call is attempted.

### C. Inadvertent OOM Killer Triggering (Safe Memory Lowering)
- **Vulnerability Analyzed**: A dynamic resource adjustment lowering `memory.max` below an application's active working set (`memory.current`), triggering immediate termination by the kernel OOM killer.
- **Defensive Mechanism**: Pre-write usage validation in `cgroup_set_memory_limit()`:
  ```c
  char cur_str[64];
  if (read_cgroup_file(cur_file, cur_str, sizeof(cur_str)) == 0) {
      unsigned long long current_usage = strtoull(cur_str, NULL, 10);
      if (max_bytes > 0 && max_bytes < current_usage) {
          snprintf(err, err_sz,
                   "Requested limit %llu bytes is below current usage %llu bytes (rejected to prevent OOM kill)",
                   (unsigned long long)max_bytes, current_usage);
          return AGENT_STATUS_LIMIT_BELOW_USAGE;
      }
  }
  ```
- **Audit Verification**: Passed. Rejects limit reductions below active RSS, returning `RESOURCE_ERR_LIMIT_BELOW_USAGE`.

### D. Process Isolation & Cgroup Deletion Safety
- **Vulnerability Analyzed**: Silently dumping member processes into the root cgroup `/sys/fs/cgroup/cgroup.procs` during group deletion, escaping resource governance.
- **Remediated Mechanism**: `cgroup_remove_group()` checks `cgroup.procs` for running tasks. If tasks remain, it returns `AGENT_STATUS_GROUP_BUSY` (`RESOURCE_ERR_GROUP_BUSY`). The caller must stop or migrate the process before the group can be unlinked.
- **Audit Verification**: Passed. Confirmed by unit tests and live Linux kernel probing.

### E. Remote Execution & Injection Prevention
- **Vulnerability Analyzed**: Use of shell wrappers (`system()`, `popen()`, `sh -c`) for resource management operations, allowing command injection.
- **Defensive Mechanism**:
  - The host communicates with the guest exclusively over Virtio-Serial character device (`org.virtualos.agent.0`) framed with structured JSON.
  - The guest agent parses discrete integer and string fields and performs direct POSIX system calls (`open`, `write`, `read`, `mkdir`, `rmdir`).
  - **Zero shell invocations** are used anywhere in the resource management subsystem.

### F. Host Protection & Hypervisor Boundaries
- **Hypervisor Boundary Realities**: QEMU runs with `-smp 2 -m 1024`.
  - *vCPU Allocation*: Exposes 2 virtual cores to the guest Linux scheduler. The host QEMU process executes vCPU threads scheduled by the macOS Mach/BSD kernel; total host CPU usage can exceed 100% per core as it aggregates vCPU execution and emulator overhead.
  - *Guest RAM vs. Host RSS*: `-m 1024` configures the guest physical address space. Because macOS uses demand-paging and QEMU has internal emulator heaps, device buffers, DMA pages, and page tables, host Resident Set Size (RSS) differs from configured guest RAM.
  - *Host Scheduling Priority*: POSIX `setpriority(PRIO_PROCESS, qemu_pid, nice)` adjusts dynamic priority in the macOS Mach scheduler to yield CPU cycles to desktop tasks, but is **not** a hard host CPU quota.
  - *Absence of Host Syscall Sandbox*: On macOS, QEMU does not enable seccomp (Linux-only) or macOS Seatbelt profiles (`sandbox_init`). HVF provides ARM64 Stage 2 translation isolation for the VM, but the QEMU host process runs unconfined in userspace.

### G. GPU Acceleration Transparency
- Hardware GPU virtualization on Apple Silicon HVF is not supported by QEMU. The system explicitly returns `gpu_supported: false` and `UNSUPPORTED_BY_HOST_HYPERVISOR`. No simulated numbers or fake OpenGL contexts are presented.

### H. Process Start-Time Verification (PID Reuse Protection)
- Implemented in `guest_agent/src/cgroup_manager.c`:
  - `cgroup_read_process_starttime()` parses field 22 (`starttime`, token 19 after the closing parenthesis) of `/proc/<pid>/stat`, isolating `comm` safely.
  - Validates `expected_starttime` before attachment and re-verifies immediately prior to writing to `cgroup.procs`.
  - If a process terminates and its PID is recycled by the guest kernel before attachment, the request fails safely with `AGENT_STATUS_PROCESS_IDENTITY_CHANGED` (`RESOURCE_ERR_PROCESS_IDENTITY_CHANGED`).

---

## 3. Security Recommendations for Future Phases

1. **Non-Root Guest Agent Execution**:
   In Phase 5, investigate running `vos_guest_agent` under a dedicated `virtualos` system user in the guest, granting access via Linux capabilities (`CAP_SYS_ADMIN` or cgroupfs file permissions `chown -R virtualos:virtualos /sys/fs/cgroup/virtualos/`).

