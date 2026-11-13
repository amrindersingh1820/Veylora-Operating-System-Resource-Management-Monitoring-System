# Virtual OS Simulator — Phase 4.2 Test Results & Empirical Verification

**Project**: Virtual OS Simulator  
**Host Environment**: macOS Apple Silicon M5 (`arm64`)  
**Hypervisor**: QEMU 11.1.2 with Apple Hypervisor.framework (`HVF`) acceleration  
**Guest Environment**: Alpine Linux v3.20 ARM64 (`aarch64`, Linux kernel `6.6.31-0-lts`)  
**Date**: November 3, 2026  
**Status**: All Tests Passed (800 / 800 checks, 0 failures)  

---

## 1. Live Guest Disk I/O Throttling Benchmark

The benchmark was executed inside the live Alpine Linux ARM64 guest running on QEMU HVF under kernel `6.6.31-0-lts`. To ensure deterministic measurement of the block I/O controller and prevent page-cache masking, direct I/O (`iflag=direct`) was enforced on `/dev/vda` (`major:minor 253:0`).

### Empirical Benchmark Data Table

| Metric / Parameter | Benchmark 1: Unthrottled Baseline | Benchmark 2: Throttled Direct Read | Delta / Comparison |
|---|---|---|---|
| **Target Device** | `/dev/vda` (`253:0`) | `/dev/vda` (`253:0`) | Same physical guest drive |
| **I/O Access Mode** | Direct I/O (`iflag=direct`) | Direct I/O (`iflag=direct`) | Bypasses Linux page cache |
| **`io.max` Configuration** | `253:0 rbps=max wbps=max` | `253:0 rbps=2097152 wbps=2097152` | $2,097,152\text{ B/s}$ ($2.0\text{ MB/s}$) |
| **Workload Size** | $10\text{ MB}$ ($10,485,760\text{ bytes}$) | $10\text{ MB}$ ($10,485,760\text{ bytes}$) | Identical data transferred |
| **Block Size / Count** | `bs=1M count=10` | `bs=1M count=10` | Identical request breakdown |
| **Elapsed Workload Time** | **`0.008869 seconds`** ($8.87\text{ ms}$) | **`4.982891 seconds`** ($4982.89\text{ ms}$) | **$561.8\times$ elapsed duration** |
| **Measured Throughput** | **`1.1 GB/s`** ($1,100\text{ MB/s}$) | **`2.0 MB/s`** ($2.007\text{ MB/s}$) | Enforced within $0.35\%$ error |
| **Kernel Uptime Start** | `2.19 s` | `2.24 s` | Monotonic `/proc/uptime` |
| **Kernel Uptime End** | `2.23 s` | `7.25 s` | $\Delta\text{uptime} = 5.01\text{ s}$ |
| **`io.stat` Before** | `rbytes=0 rios=0` | `rbytes=10485760 rios=10` | Initial accumulator state |
| **`io.stat` After** | `rbytes=10485760 rios=10` | `rbytes=20971520 rios=20` | Cumulative accounting |
| **`io.stat` Delta (`rbytes`)** | **`+10,485,760 bytes`** ($10.0\text{ MB}$) | **`+10,485,760 bytes`** ($10.0\text{ MB}$) | Exact byte accounting match |
| **`io.stat` Delta (`rios`)** | **`+10 operations`** | **`+10 operations`** | 1 operation per 1 MB block |

### Analysis & Measurement Limitations
1. **Direct I/O vs. Buffered I/O**:
   - When processes read or write via standard buffered I/O, the Linux page cache absorbs writes and satisfies reads from memory. The `io.max` controller throttles actual block device transfers, not virtual memory page dirtying. Direct I/O (`O_DIRECT` / `iflag=direct`) accurately forces synchronous block device requests.
2. **Precision of Throttling**:
   - Configured rate: $2,097,152\text{ bytes/sec}$ ($2.000\text{ MB/s}$).
   - Measured throughput: $10,485,760\text{ bytes} / 4.982891\text{ seconds} = 2,104,352\text{ bytes/sec}$ ($2.007\text{ MB/s}$).
   - Error margin is under $0.35\%$, reflecting precise Linux CFS blkio token bucket pacing.
3. **Hardware Storage Bounds**:
   - The unthrottled baseline of $1.1\text{ GB/s}$ reflects Apple Silicon M5 PCIe NVMe storage backing the QEMU qcow2 drive image. The cgroup throttle safely confines the workload to the defined rate without host starvation.

---

## 2. In-Guest PID Identity & Start-Time Verification

### Live Guest Procfs Inspection
A live background process was spawned and inspected inside Alpine Linux:
- **Command**: `sleep 60 &`
- **Assigned PID**: `2361`
- **Raw `/proc/2361/stat`**:
  ```text
  2361 (sleep) S 2343 2361 2342 52288 2362 4194304 63 0 0 0 0 0 0 0 20 0 1 0 726 1744896 192 ...
  ```
- **Extracted Comm**: `sleep` (isolated between first `(` and last `)`)
- **Extracted Start Time**: `726` (token 19 after closing parenthesis, field 22)
- **Attachment Verification**: Writing `2361` to `cgroup.procs` confirmed via readback:
  ```text
  Attached cgroup.procs: 2361
  ```
- **Termination Verification**: `kill -9 2361` was executed. The test confirmed `[ ! -d /proc/2361 ]`, ensuring instant cleanup from `/proc`.

### Unit Test Verification (`test_phase_4_2_safety_and_pid_identity`)
A dedicated suite tested edge cases and simulated procfs states:

| Test Case | Input Condition | Expected Result | Actual Result | Status |
|---|---|---|---|---|
| Valid start time match | `expected_starttime = 7654321` | Attachment succeeds (`AGENT_STATUS_SUCCESS`) | `AGENT_STATUS_SUCCESS` | PASS |
| Nested parens in comm | `(worker (bg) thread)` in `/proc/1234/stat` | Token 19 parsed correctly (`7654321`) | `7654321ULL` | PASS |
| Recycled PID (Identity Mismatch) | `expected_starttime = 1111111` | Rejection: `AGENT_STATUS_PROCESS_IDENTITY_CHANGED` | `PROCESS_IDENTITY_CHANGED` | PASS |
| Unspecified start time (0) | `expected_starttime = 0` | Allowed for backward compatibility | `AGENT_STATUS_SUCCESS` | PASS |
| Non-existent PID | `pid = 999999` | Rejection: `AGENT_STATUS_PROCESS_NOT_FOUND` | `PROCESS_NOT_FOUND` | PASS |
| PID 1 Protection | `pid = 1` | Rejection: `AGENT_STATUS_PERMISSION_DENIED` | `PERMISSION_DENIED` | PASS |
| PID 0 / Negative PID | `pid <= 0` | Rejection: `AGENT_STATUS_PERMISSION_DENIED` | `PERMISSION_DENIED` | PASS |
| Unsafe Group Identifier | `group_id = "../escape"` | Rejection: `AGENT_STATUS_INVALID_CONFIG` | `INVALID_CONFIG` | PASS |

---

## 3. Cgroup Identifier Safety Rejection Matrix

`cgroup_is_safe_name()` was evaluated against 33 malicious or invalid inputs:

| Input Category | Tested Strings | Verdict |
|---|---|---|
| Valid Names | `"app1"`, `"worker_2"`, `"test-group"`, `"grp.1"` | **ACCEPTED (1)** |
| Null / Empty | `NULL`, `""` | **REJECTED (0)** |
| Directory Traversal | `"."`, `"..`, `"../foo"`, `"foo/bar"`, `"/foo"` | **REJECTED (0)** |
| Invalid Leading Characters | `"-foo"`, `".foo"` | **REJECTED (0)** |
| Path Separators | `"bad\\path"` | **REJECTED (0)** |
| Shell Metacharacters | `"bad$var"`, `"bad;cmd"`, `"bad|pipe"`, `"bad&bg"` | **REJECTED (0)** |
| I/O Redirection & Wildcards | `"bad>out"`, `"bad<in"`, `"bad*star"`, `"bad?q"` | **REJECTED (0)** |
| Whitespace & Control Characters | `"bad space"`, `"bad\nnewline"` | **REJECTED (0)** |
| Reserved System Directories | `"applications"`, `"virtualos"`, `"cgroup"`, `"root"` | **REJECTED (0)** |
| Reserved Controller Files | `"cgroup.procs"`, `"cgroup.subtree_control"`, `"cgroup.controllers"` | **REJECTED (0)** |
| Reserved Resource Limits | `"cpu.max"`, `"cpu.weight"`, `"memory.max"`, `"pids.max"`, `"io.max"` | **REJECTED (0)** |
| Length Exceeded ($>64\text{ chars}$) | 65-character alphanumeric string | **REJECTED (0)** |

---

## 4. Full Regression Test Matrix

All 4 test suites compile cleanly and pass with 0 failures:

```text
================================================================
 Virtual OS Simulator - Complete Test Suite Summary
================================================================
 1. tests/test_core.c                  : 256 checks, 0 failed [PASS]
 2. tests/test_vm.c                    : 114 checks, 0 failed [PASS]
 3. tests/test_monitoring.c            : 240 checks, 0 failed [PASS]
 4. tests/test_resource_enforcement.c  : 190 checks, 0 failed [PASS]
----------------------------------------------------------------
 Total Test Checks                      : 800 checks, 0 failed [100% PASS]
================================================================
```

### Build & Binary Verification
- `virtual_os_cli`: Builds cleanly with `make cli`.
- `vos_guest_agent`: Builds cleanly with `make -C guest_agent`.
- Zero compiler warnings with `-std=c11 -Wall -Wextra`.
- Pure C11 backend; no unneeded external dependencies.

