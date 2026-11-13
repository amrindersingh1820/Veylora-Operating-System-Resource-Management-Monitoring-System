# Application Management User & Operator Guide

**Project**: Virtual OS Simulator  
**CLI Command**: `virtualos app <subcommand> [options]`  
**Host Architecture**: macOS Apple Silicon M5 (`arm64`)  
**Guest Environment**: Alpine Linux ARM64 (Linux Kernel `6.6.31-0-lts`)  
**Status**: ACTIVE  

---

## 1. Overview

The Application Management subsystem in Virtual OS Simulator allows administrators, developers, and automated scripts to manage applications running inside ARM64 virtual machines. It integrates the SQLite metadata store on macOS with real Linux kernel cgroups v2 resource boundaries inside the guest.

Key Capabilities:
* **In-Guest Application Discovery**: Scan guest executable directories without executing code or traversing arbitrary filesystems.
* **Per-VM Registry**: Register applications with metadata, custom categories, and assigned resource profiles.
* **Guaranteed Cgroup Containment**: Applications are placed into cgroups v2 before execution starts.
* **PID Reuse Safe Termination**: Stop running applications with zero risk of killing recycled process IDs.
* **Real-Time Telemetry & Time-Series Sampling**: Collect fine-grained CPU throttling, RAM usage, and process counts.
* **Reconciliation**: Detect and correct stale instance records after guest reboots or crashes.

---

## 2. CLI Command Reference

The non-interactive CLI is accessible via `virtualos app <subcommand>`.

### 2.1 Discovery & Registry

#### `discover`
Scans standard guest executable paths (`/bin`, `/usr/bin`, `/usr/local/bin`, `/opt`) and lists detected runnable binaries.
```bash
./virtual_os_cli app discover <vm_id>
```
*Example Output*:
```
=== Discovered Applications in VM 'alpine_01' (18 total) ===
#    Application Name     Package          Executable Path
----------------------------------------------------------------------
[ 1] busybox              busybox          /bin/busybox
[ 2] sh                   busybox          /bin/sh
[ 3] ls                   busybox          /bin/ls
[ 4] top                  busybox          /usr/bin/top
[ 5] htop                 htop             /usr/bin/htop
[ 6] python3              python3          /usr/bin/python3
```

#### `register`
Registers an application in SQLite for the specified VM.
```bash
./virtual_os_cli app register <vm_id> <app_id> <name> <executable_path> [category] [profile_id]
```
*Parameters*:
- `vm_id`: ID of the target virtual machine.
- `app_id`: Unique identifier within this VM (e.g., `calc_cli`).
- `name`: Human-readable display name (e.g., `"Scientific Calculator"`).
- `executable_path`: In-guest absolute path (e.g., `/usr/bin/bc`).
- `category` (optional): Classification (default: `"System Utility"`).
- `profile_id` (optional): Initial resource profile (default: `"default"`).

#### `list`
Lists all registered applications for a VM, with optional category filtering.
```bash
./virtual_os_cli app list <vm_id> [category]
```

#### `info`
Displays detailed metadata, statistics, and configuration for a registered application.
```bash
./virtual_os_cli app info <vm_id> <app_id>
```

#### `unregister`
Removes an application from the registry.
```bash
./virtual_os_cli app unregister <vm_id> <app_id>
```
> [!NOTE]
> Unregistering an application deletes the metadata and historical logs from SQLite, but **strictly preserves** the underlying executable file on the guest filesystem.

#### `enable` / `disable`
Toggles an application's execution state. Disabled applications cannot be launched.
```bash
./virtual_os_cli app enable <vm_id> <app_id>
./virtual_os_cli app disable <vm_id> <app_id>
```

---

### 2.2 Resource Profiles

#### `profile-list`
Lists all available resource profiles in the system.
```bash
./virtual_os_cli app profile-list
```
*Example Output*:
```
=== Available Resource Profiles (3 total) ===
PROFILE ID       NAME               CORES    WEIGHT   MAX MEM(MB)  MAX PIDS   DISK I/O
--------------------------------------------------------------------------------------------
default          Default Profile    0.00     100      0            -1         0 MB/s
performance      Performance Profile 1.50     200      512          50         0 MB/s
restricted       Restricted Sandbox 0.50     100      128          20         5 MB/s
```

#### `profile-create`
Creates a new reusable resource profile in SQLite.
```bash
./virtual_os_cli app profile-create <profile_id> <name> <cores> <mem_mb> <pids_max> [io_mb_s]
```

#### `profile-assign`
Assigns a resource profile to a registered application.
```bash
./virtual_os_cli app profile-assign <vm_id> <app_id> <profile_id>
```

---

### 2.3 Execution & Lifecycle

#### `launch`
Launches the specified application inside the guest VM under its enforced cgroup.
```bash
./virtual_os_cli app launch <vm_id> <app_id> [arguments...]
```
*Example Output*:
```
[SUCCESS] Application 'busybox_worker' launched successfully in VM 'alpine_01'
  Instance ID  : inst_1700001234_abc89
  Guest PID    : 2342
  Start Time   : 1045230 (procfs verified)
  Cgroup Path  : /sys/fs/cgroup/virtualos/applications/app_busybox_worker (pre-execution attached)
  Profile      : restricted
  Status       : RUNNING
```

#### `stop`
Gracefully stops a running application instance using `SIGTERM`, falling back to `SIGKILL` if it fails to exit within the timeout.
```bash
./virtual_os_cli app stop <vm_id> <instance_id> [--force]
```
> [!IMPORTANT]
> The stop operation verifies the PID's kernel start time before sending signals. If the PID has been recycled by another process, termination is aborted with `APP_ERR_PID_IDENTITY_CHANGED`.

#### `status`
Displays runtime status, process identity, cgroup path, and exit codes for an instance.
```bash
./virtual_os_cli app status <vm_id> <instance_id>
```

#### `metrics`
Queries live cgroup v2 metrics from the guest agent and returns real-time resource utilization.
```bash
./virtual_os_cli app metrics <vm_id> <instance_id>
```
*Example Output*:
```
=== Live Application Resource Metrics ===
  Instance ID     : inst_1700001234_abc89
  Application ID  : busybox_worker
  VM ID           : alpine_01
  CPU Usage       :  49.8 %
  CPU Time Total  : 1019598 us
  CPU Throttled   : 952635 us
  Memory Current  :  16.20 MB
  Memory Peak     :  24.50 MB
  Active PIDs     : 1 / 10
  Disk I/O Read   :   0.00 KB
  Disk I/O Write  :   0.00 KB
  Telemetry State : FRESH (VERIFIED)
```

#### `ps`
Lists all active or recent application instances for a VM.
```bash
./virtual_os_cli app ps <vm_id>
```

#### `reconcile`
Audits instance records against actual in-guest processes and reconciles terminated or crashed tasks.
```bash
./virtual_os_cli app reconcile <vm_id>
```

---

## 3. Interactive Terminal UI Walkthrough

Users can also manage applications interactively through the terminal menu:
1. Launch `./virtual_os_cli` without arguments.
2. Select `[8] Virtual Machine Manager (ARM64 / QEMU)`.
3. Select `[10] Application Management & In-Guest Launch`.
4. Enter the target VM ID.
5. Use menu options `[1]` through `[10]` to discover, register, configure profiles, launch, inspect, and stop applications with real-time feedback.

---

## 4. Phase 5.2 Security & Hardening Architecture

In Phase 5.2, the application management subsystem underwent critical security corrections and validation:

1. **Direct `execve()` Dispatch**:
   - Zero shell invocation (`system()`, `popen()`, `sh -c`, `bash -c`, `eval`) in production application launching.
   - Applications are launched with explicit `executable_path`, discrete `argv` array, and a sanitized, controlled `default_envp` (`PATH`, `HOME`, `TERM`, `USER`, `LOGNAME`).
2. **In-Memory Argument Parsing (`app_parse_args`)**:
   - In-memory tokenizer handles single and double quotes, preserving whitespace within quoted arguments (e.g. `--greeting="Hello World"`).
   - Shell metacharacters (`;`, `$`, `|`, `&`, `` ` ``, `<`, `>`) remain literal argument data; shell injection attacks are completely mitigated.
3. **Dynamic Block Device Resolution (`cgroup_resolve_block_device`)**:
   - Primary guest drive major:minor is dynamically queried from sysfs (`/sys/block/<dev>/dev`) and validated against `stat()` (`S_ISBLK`).
   - Zero hardcoded `253:0` or `254:0` values. Writing to non-block devices or unresolvable targets is rejected before `io.max` is modified.
4. **Pre-Execution Handshake & Abort Safety**:
   - Dual-pipe synchronization protocol blocks child before execution, allowing the parent to read start time and confirm cgroup attachment in `cgroup.procs`.
   - Negative test proves the target process is immediately killed with `SIGKILL` without ever reaching `execve()` if cgroup attachment fails.


