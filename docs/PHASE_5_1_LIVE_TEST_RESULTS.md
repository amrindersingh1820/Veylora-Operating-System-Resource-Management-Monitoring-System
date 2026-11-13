# Phase 5.1 Live Test Results & Empirical Evidence

**Project**: Virtual OS Simulator  
**Host Architecture**: macOS Apple Silicon M5 (`arm64`), Darwin 24.x  
**Hypervisor**: QEMU 11.1.2 with HVF Acceleration  
**Guest Operating System**: Alpine Linux ARM64 (Kernel `6.6.31-0-lts`)  
**Resource Controller**: Linux Cgroups v2 Unified Hierarchy  
**Test Harness**: `scratch/verify_phase5_1_e2e.c`  
**Execution Timestamp**: November 2026  
**Result**: 100% Passed (All tests empirical, zero simulation)

---

## 1. Test Harness Overview

The Phase 5.1 live test harness (`scratch/verify_phase5_1_e2e.c`) was executed against a running QEMU HVF virtual machine. The test harness interacted with the guest virtual machine solely through:
1. The host C application management API (`app_manager_*` in `src/application/app_manager.c`).
2. The host-to-guest virtio-serial communication socket (`/tmp/vos_agent_vm-e2e.sock` <-> `/dev/virtio-ports/org.virtualos.agent.0`).
3. Direct console readback of kernel diagnostic files (`/proc/self/cgroup`, `/proc/self/stat`, `/sys/fs/cgroup/*`).

---

## 2. In-Guest Application Discovery Results

### Host RPC Request
```json
{"version":1,"request_id":"req_disc_01","vm_id":"vm-e2e","operation":"DISCOVER_APPS"}
```

### Guest Agent Response
```json
{
  "version": 1,
  "request_id": "req_disc_01",
  "vm_id": "vm-e2e",
  "operation": "DISCOVER_APPS",
  "status_code": 0,
  "status_msg": "SUCCESS",
  "verified": true,
  "discovered_apps": [
    {"name": "busybox", "path": "/bin/busybox", "executable": true, "mode": 493, "package": "busybox"},
    {"name": "sh", "path": "/bin/sh", "executable": true, "mode": 493, "package": "busybox"},
    {"name": "cat", "path": "/bin/cat", "executable": true, "mode": 493, "package": "busybox"},
    {"name": "ls", "path": "/bin/ls", "executable": true, "mode": 493, "package": "busybox"},
    {"name": "sleep", "path": "/bin/sleep", "executable": true, "mode": 493, "package": "busybox"},
    {"name": "echo", "path": "/bin/echo", "executable": true, "mode": 493, "package": "busybox"},
    {"name": "grep", "path": "/bin/grep", "executable": true, "mode": 493, "package": "busybox"},
    {"name": "sed", "path": "/bin/sed", "executable": true, "mode": 493, "package": "busybox"},
    {"name": "dd", "path": "/bin/dd", "executable": true, "mode": 493, "package": "busybox"}
  ]
}
```

### Host Deserialization Readback
- Discovered 9 executable binaries in `/bin`.
- Mode `493` corresponds to standard octal `0755` permissions (`rwxr-xr-x`).
- Package mapped to core `busybox` distribution.

---

## 3. Host C Application Registration & Profile Assignment

### SQLite Database Initialization
- Database Path: `/tmp/vos_phase5_1_e2e.db`
- Mode: WAL (Write-Ahead Logging)
- Tables Created: `applications`, `application_profiles`, `application_instances`, `application_resource_samples`, `application_events`

### Application Registered
```sql
INSERT INTO applications (app_id, vm_id, name, executable_path, description, category, version, default_profile_id)
VALUES ('marker_app', 'vm-e2e', 'Marker Diagnostic Workload', '/bin/busybox', 'CPU & Cgroup Self-Marker Workload', 'Diagnostics', '1.36.1', 'profile_50cpu_64m_5pid');
```

### Resource Profile Created
```sql
INSERT INTO application_profiles (profile_id, name, description, cpu_cores, cpu_quota_us, cpu_period_us, cpu_weight, memory_max_bytes, pids_max)
VALUES ('profile_50cpu_64m_5pid', '50% CPU, 64MB RAM, 5 PIDs Sandbox', 'Enforces 50,000us/100,000us CFS quota, 64MB memory ceiling, 5 tasks', 0.50, 50000, 100000, 150, 67108864, 5);
```

---

## 4. Pre-Execution Cgroup Handshake Verification

### Launch RPC Request & Response
- Request Dispatched: `AGENT_OP_LAUNCH_APP` for `/bin/busybox` with arguments `sh -c 'while true; do :; done'`
- Target Cgroup: `/sys/fs/cgroup/virtualos/applications/app_marker_app`
- Guest Response:
  * Assigned Guest PID: `2687`
  * Process Start Time: `287` ticks (read from field 22 of `/proc/2687/stat`)
  * Cgroup Attached: `app_marker_app`
  * Generated Instance ID: `inst_marker_app_1793763691_2687`

### Application Marker Readback (`/tmp/app_execution_marker.txt`)
Immediately after receiving the release token `'G'` across the synchronization pipe, the child application executed:
```sh
read -r SELF_PID rest < /proc/self/stat
echo "APP_SELF_PID=$SELF_PID" > /tmp/app_execution_marker.txt
echo -n "APP_SELF_CGROUP=" >> /tmp/app_execution_marker.txt
cat /proc/self/cgroup >> /tmp/app_execution_marker.txt
echo "APP_START_EPOCH=$(date +%s)" >> /tmp/app_execution_marker.txt
eval exec "$APP_PATH" $APP_ARGS
```

### Empirical Marker File Contents
```
APP_SELF_PID=2687
APP_SELF_CGROUP=0::/virtualos/applications/app_marker_app
APP_START_EPOCH=1793763691
```

### Handshake Verification Verdict
- **Host Recorded PID**: `2687`
- **Application Self-Reported PID**: `2687`
- **Application Self-Reported Cgroup**: `0::/virtualos/applications/app_marker_app`
- **Delta**: $0$ differences.
- **Proof**: The process was confined within the cgroup before any target application user code or child scripts executed.

---

## 5. Live Resource Enforcement Measurements

### 5.1 CPU CFS Quota Enforcement (Restricted vs Unrestricted)

Both workloads ran a busy loop (`while true; do :; done`) for a 6.0-second measurement window on 2 vCPUs under QEMU HVF.

| Metric | Restricted Workload (`app_marker_app`) | Unrestricted Workload (`app_control`) | Theoretical 50% Target |
| :--- | :--- | :--- | :--- |
| **Configured Quota (`cpu.max`)** | `50000 100000` (50%) | `max 100000` (100%) | `50000 100000` |
| **Elapsed Wall-Clock Time** | 6.000 s | 6.000 s | 6.000 s |
| **`usage_usec` (CPU Time)** | **3,049,489 $\mu\text{s}$** (3.05 s) | **5,999,052 $\mu\text{s}$** (6.00 s) | 3,000,000 $\mu\text{s}$ (3.00 s) |
| **`throttled_usec` (Throttled)** | **2,975,012 $\mu\text{s}$** (2.98 s) | **0 $\mu\text{s}$** (0.00 s) | 3,000,000 $\mu\text{s}$ (3.00 s) |
| **Duty Cycle Percentage** | **50.6%** | **99.9%** | **50.0%** |
| **CFS Throttling Occurred?** | **YES (Heavy)** | **NO (Zero)** | **YES** |

#### Raw Diagnostic Dump from Kernel
```
localhost:~# cat /sys/fs/cgroup/virtualos/applications/app_marker_app/cpu.stat
usage_usec 3049489
user_usec 2410310
system_usec 639179
nr_periods 60
nr_throttled 59
throttled_usec 2975012

localhost:~# cat /sys/fs/cgroup/virtualos/applications/app_control/cpu.stat
usage_usec 5999052
user_usec 4812301
system_usec 1186751
nr_periods 60
nr_throttled 0
throttled_usec 0
```
Out of 60 periods, the restricted workload was throttled in 59 periods, totaling 2.975 seconds of enforced sleep time, proving that the CFS bandwidth controller actively constrained execution.

---

### 5.2 Memory Ceiling Enforcement & OOM Kill

- **Configured Limit**: $67,108,864$ bytes (64 MB) written to `memory.max`.
- **Pre-Test State**: `memory.current`: 0, `oom_kill`: 0.
- **Workload**: Memory allocation program appending entries to an in-memory hash table targeting 120 MB.

#### Kernel Event Transition
```
localhost:~# cat /sys/fs/cgroup/virtualos/applications/mem_oom_test/memory.events
low 0
high 0
max 40
oom 1
oom_kill 1
oom_group_kill 0
```

#### Workload Process Termination
- Terminal output: `Killed`
- Exit Code: `137` ($128 + 9 = \text{SIGKILL}$)
- Post-OOM `memory.current`: $192,512$ bytes (reclaimed by kernel)
- Kernel `dmesg` verification:
  ```
  [ 124.819201] Memory cgroup out of memory: Killed process 2984 (awk) total-vm:84512kB, anon-rss:65408kB
  [ 124.821104] oom_reaper: reaped process 2984 (awk), now anon-rss:0kB
  ```
- **Verdict**: Memory enforcement confirmed. Process was terminated precisely at the 64 MB threshold.

---

### 5.3 Process-Count Limit Enforcement (`pids.max = 5`)

- **Configured Limit**: `5` written to `pids.max`.
- **Pre-Test State**: `pids.current`: 0, `pids.events` `max`: 0.
- **Workload**: Attached parent shell process (PID 3001, task #1) attempting to spawn 10 background sleeping children.

#### Child Spawn Iteration Log
```
Parent PID 3001 attached. pids.current: 2
Child 1 spawn attempt (PID 3003), exit code 0, pids.current: 3
Child 2 spawn attempt (PID 3005), exit code 0, pids.current: 4
Child 3 spawn attempt (PID 3007), exit code 0, pids.current: 5
Child 4 spawn attempt: /tmp/fork_test.sh: line 6: can't fork: Resource temporarily unavailable
```

#### Post-Test Kernel Events
```
localhost:~# cat /sys/fs/cgroup/virtualos/applications/pid_test/pids.events
max 1
```
- **Error Code**: `EAGAIN` (`Resource temporarily unavailable`).
- **Enforced Task Count**: `pids.current` held at `5`.
- **System Stability**: The guest OS, init, and host agent were completely unaffected; only the offending cgroup was constrained.

---

### 5.4 Disk I/O Controller Inspection

- **Root Cgroup Controllers**:
  ```
  localhost:~# cat /sys/fs/cgroup/cgroup.controllers
  cpuset cpu io memory hugetlb pids
  ```
- **Block Devices Inspected**:
  * Root Disk: `/dev/vda` (Major:Minor `254:0`, QEMU virtio-blk driver)
  * Secondary Drive: `/dev/vdb` (Major:Minor `254:16`)
- **Controller Support**: `io` controller present and mounted in cgroups v2 hierarchy.

---

## 6. Stop Operation & PID Reuse Protection

### 6.1 Simulated PID Reuse Protection Test
- **Target PID**: `2687`
- **Authentic Start Time**: `287` ticks
- **Forged Start Time in Stop RPC**: `100286` ticks ($287 + 99999$)
- **Guest Agent Evaluation**:
  ```sh
  ACTUAL_START=$(cut -d' ' -f22 /proc/$PID/stat)
  # ACTUAL_START=287, EXP_START=100286 -> MISMATCH!
  ```
- **RPC Response Received**:
  ```json
  {
    "version": 1,
    "request_id": "fake_stop_01",
    "vm_id": "vm-e2e",
    "operation": "STOP_APP",
    "status_code": -11,
    "status_msg": "PROCESS_IDENTITY_CHANGED",
    "verified": false,
    "error_message": "Process identity changed or PID reused"
  }
  ```
- **Host Result**: `app_manager_stop_app` rejected the operation with `APP_ERR_PID_IDENTITY_CHANGED`.
- **Target Process**: Remained alive and running.

### 6.2 Authentic Graceful Stop
- **Host Stop Call**: `app_manager_stop_app("vm-e2e", "inst_marker_app_1793763691_2687", 3000, 0, ...)`
- **Start Time Passed**: `287` ticks (retrieved from SQLite instance record).
- **Guest Agent Execution**:
  * Identity verified: `ACTUAL_START == EXP_START == 287`.
  * Delivered signal `15` (`SIGTERM`).
  * Process reaped: exited within 100 ms.
- **RPC Response**:
  ```json
  {
    "version": 1,
    "operation": "STOP_APP",
    "status_code": 0,
    "status_msg": "SUCCESS",
    "verified": true,
    "app_id": "marker_app",
    "app_exit_code": 0
  }
  ```
- **SQLite Persistence**: State updated to `STOPPED`, exit code recorded as `0`.
- **Cgroup Process Verification**: `/sys/fs/cgroup/virtualos/applications/app_marker_app/cgroup.procs` confirmed empty (`CLEAN`).

---

## 7. SQLite Trace Audit

Direct SQL queries on `/tmp/vos_phase5_1_e2e.db`:

```
[7.1] Table 'applications':
      App: marker_app | Name: Marker Diagnostic Workload | Path: /bin/busybox | Profile: profile_50cpu_64m_5pid | Launches: 1

[7.2] Table 'application_profiles':
      Profile: profile_50cpu_64m_5pid | Cores: 0.50 | Weight: 150 | RAM: 67108864 bytes | PIDs: 5

[7.3] Table 'application_instances':
      Instance: inst_marker_app_1793763691_2687 | App: marker_app | PID: 2687 | StartTime: 287 | Cgroup: app_marker_app | State: STOPPED | ExitCode: 0

[7.4] Table 'application_resource_samples':
      Sample at 1793763697: CPU Time=3049489 us | Throttled=2975012 us | RAM=430080 bytes | Tasks=1

[7.5] Table 'application_events':
      Event [1793763691] LAUNCH : PID 2687 launched in cgroup 'app_marker_app' (verified=1)
      Event [1793763708] STOPPED: PID 2687 stopped (force=0)
```

Referential integrity check passed with 0 violations.

