# Virtual OS Simulator – Monitoring Architecture

## 1. Overview and Design Principles

The Virtual OS Simulator monitoring subsystem provides real-time, empirically sourced telemetry for host hardware, running virtual machines, and individual guest applications.

### Core Architectural Principles
1. **Source Attribution & Ground Truth**:
   Every metric has an unambiguous, traceable origin. The system distinguishes between:
   - **Host-wide metrics**: Mach microkernel load and memory statistics from macOS.
   - **Hypervisor observations**: QEMU child process CPU and RSS memory measured via `proc_pidinfo`.
   - **In-guest kernel metrics**: True guest OS metrics gathered directly from Linux `/proc` filesystems by the guest agent (`vos_guest_agent`).
   These metrics are never conflated or substituted for one another.
2. **Zero Fabrication**:
   If an in-guest metric cannot be retrieved (e.g. because the guest agent is not running), it is explicitly marked `unavailable` or `has_guest_telemetry = 0`. The system never fabricates or simulates guest values.
3. **Decoupled Asynchronous Sampling**:
   Resource collection runs in independent POSIX worker threads with configurable sampling intervals (default 1000ms), isolated from the main UI/CLI thread.
4. **Resilient Communication**:
   Virtio-serial UNIX domain sockets (`/dev/virtio-ports/org.virtualos.agent.0` inside guest, `/tmp/vos_agent_<vm_id>.sock` on host) provide an isolated point-to-point communication channel requiring zero guest network stack or TCP bridging.

```
+-----------------------------------------------------------------------------------------+
|                                    MACOS HOST (ARM64)                                   |
|                                                                                         |
|  +--------------------+    +---------------------------------------------------------+  |
|  |   Host Monitor     |    |                    VM Monitor Engine                    |  |
|  | mach_host_self()   |    |                                                         |  |
|  | HOST_CPU_LOAD_INFO |    |  +-------------------+        +----------------------+  |  |
|  | HOST_VM_INFO64     |    |  | Background Worker |        | SQLite Persistence   |  |  |
|  | sysctl(hw.memsize) |    |  | pthread / 1000ms  |        | - resource_samples   |  |  |
|  +--------------------+    |  +---------+---------+        | - process_samples    |  |  |
|                            +------------|------------------| - resource_limits    |--+  |
|                                         |                  +----------------------+     |
|                                         | proc_pidinfo()                                |
|                                         v                                               |
|                             +-----------------------+                                   |
|                             |  QEMU Child Process   |                                   |
|                             |  (PID: 40369)         |                                   |
|                             +-----------+-----------+                                   |
|                                         |                                               |
|               UNIX Domain Socket        |                                               |
|            /tmp/vos_agent_<id>.sock <---+                                               |
+-----------------------------------------|-----------------------------------------------+
                                          | Virtio-Serial Bus
+-----------------------------------------v-----------------------------------------------+
|                               LINUX ARM64 GUEST OS                                      |
|                                                                                         |
|   /dev/virtio-ports/org.virtualos.agent.0                                               |
|              ^                                                                          |
|              | JSON Telemetry Stream                                                    |
|   +----------+----------------------------------------------------------------------+   |
|   |                      Virtual OS Guest Agent (vos_guest_agent)                    |   |
|   |  - CPU (/proc/stat)               - Network (/proc/net/dev)                     |   |
|   |  - Memory (/proc/meminfo)         - Processes (/proc/[pid]/stat, status)         |   |
|   |  - Disk I/O (/proc/diskstats)     - Cgroups v2 Enforcement                      |   |
|   +---------------------------------------------------------------------------------+   |
|                                                                                         |
|   GUEST APPLICATIONS:                                                                   |
|   [Photoshop / GIMP] [IntelliJ / CLion] [Chromium] [System Services]                    |
|   (Creative Category) (DevTools Cat.)  (Browser)   (System Cat.)                        |
+-----------------------------------------------------------------------------------------+
```

---

## 2. Host Resource Monitor Implementation

Implemented in `src/monitoring/host_monitor.c` and `src/monitoring/host_monitor.h`.

### Host CPU Utilization (Mach Kernel Statistics)
Uses `host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, ...)`:
- Queries cumulative host tick counters: `CPU_STATE_USER`, `CPU_STATE_SYSTEM`, `CPU_STATE_IDLE`, and `CPU_STATE_NICE`.
- Computes delta CPU load between consecutive samples:
  $$\Delta \text{total} = \Delta \text{user} + \Delta \text{system} + \Delta \text{idle} + \Delta \text{nice}$$
  $$\text{User \%} = \frac{\Delta \text{user}}{\Delta \text{total}} \times 100, \quad \text{System \%} = \frac{\Delta \text{system}}{\Delta \text{total}} \times 100, \quad \text{Total \%} = 100 - \frac{\Delta \text{idle}}{\Delta \text{total}} \times 100$$
- Logical core count and CPU architecture are queried via `sysctlbyname("hw.ncpu")` and `sysctlbyname("hw.machine")`.

### Host Physical Memory Distribution (Mach 64-bit VM Info)
Uses `host_statistics64(mach_host_self(), HOST_VM_INFO64, ...)` with `host_page_size(mach_host_self(), &page_size)`:
- **Total Physical RAM**: `sysctlbyname("hw.memsize")`.
- **Wired Memory**: `vm_stat.wire_count * page_size` (kernel non-pageable memory).
- **Active Memory**: `vm_stat.active_count * page_size`.
- **Inactive Memory**: `vm_stat.inactive_count * page_size`.
- **Free Memory**: `vm_stat.free_count * page_size`.
- **Compressed Memory**: `vm_stat.compressor_page_count * page_size`.
- **Used Memory**: Computed as `(wire + active + compressor) * page_size`.
- **Available Memory**: Free memory plus inactive and purgeable memory.

### Target QEMU Process Inspection
Uses `proc_pidinfo(pid, PROC_PIDTASKINFO, ...)` and `proc_pidinfo(pid, PROC_PIDTBSDINFO, ...)`:
- `pti_resident_size`: Physical RAM resident in host memory (RSS) consumed by the QEMU process.
- `pti_virtual_address_size`: Virtual address space allocated.
- `pti_total_user` and `pti_total_system`: Cumulative CPU execution time in nanoseconds. Successive samples calculate true hypervisor CPU consumption:
  $$\text{QEMU CPU \%} = \frac{\Delta \text{user\_ns} + \Delta \text{system\_ns}}{\Delta \text{wall\_clock\_ns}} \times 100$$
- `pbi_status`: Process execution state (running, sleeping, stopped, zombie).

---

## 3. Host-to-Guest Communication & Wire Protocol

Implemented in `src/monitoring/agent_protocol.c` and `src/monitoring/agent_protocol.h`.

### Communication Channel
QEMU is configured with a dedicated virtio-serial communication device:
```bash
-chardev socket,id=vos_agent,path=/tmp/vos_agent_<vm_id>.sock,server=on,wait=off \
-device virtio-serial-pci \
-device virtserialport,chardev=vos_agent,name=org.virtualos.agent.0
```
- In the Linux guest, the kernel automatically instantiates `/dev/virtio-ports/org.virtualos.agent.0`.
- The guest agent opens this port as a standard file descriptor and emits telemetry packets.
- The host `vm_monitor` connects to `/tmp/vos_agent_<vm_id>.sock` via UNIX domain sockets using non-blocking I/O (`poll(POLLIN)`).
- Fallback TCP support (port 9888) is provided for networked environments.

### Telemetry Packet Format (JSON Wire Protocol)
Each transmission is a compact JSON document conforming to schema version 1:
```json
{
  "version": 1,
  "vm_id": "alpine_arm64",
  "timestamp_ms": 1730456123000,
  "sequence": 42,
  "cpu": {
    "total_pct": 34.2,
    "user_pct": 22.1,
    "sys_pct": 11.5,
    "idle_pct": 65.8,
    "iowait_pct": 0.6,
    "cores": 2,
    "core_pct": [38.0, 30.4]
  },
  "memory": {
    "total_b": 1073741824,
    "used_b": 419430400,
    "free_b": 268435456,
    "avail_b": 654311424,
    "buffers_b": 33554432,
    "cached_b": 352321536,
    "swap_total_b": 0,
    "swap_used_b": 0
  },
  "disk": {
    "read_b": 15728640,
    "write_b": 4194304,
    "read_ops": 312,
    "write_ops": 88
  },
  "network": {
    "iface": "eth0",
    "rx_b": 1048576,
    "tx_b": 524288,
    "rx_pkts": 1420,
    "tx_pkts": 980
  },
  "processes": [
    {
      "pid": 512,
      "name": "photoshop.bin",
      "state": "R",
      "cpu_pct": 24.5,
      "rss_b": 314572800,
      "virt_b": 629145600
    },
    {
      "pid": 601,
      "name": "clion",
      "state": "S",
      "cpu_pct": 8.2,
      "rss_b": 209715200,
      "virt_b": 524288000
    }
  ]
}
```

---

## 4. Application & Process Classification

Implemented in `src/monitoring/process_monitor.c` and `src/monitoring/process_monitor.h`.

In-guest processes are categorized using signature matching to support application-level resource profiling:
| Category | Identified Binaries & Signatures | Example Applications |
| :--- | :--- | :--- |
| **`APP_GROUP_CREATIVE`** | `photoshop`, `lightroom`, `blender`, `gimp`, `inkscape`, `krita`, `davinci` | Adobe Photoshop, Lightroom, Blender 3D |
| **`APP_GROUP_DEVTOOLS`** | `idea`, `pycharm`, `clion`, `webstorm`, `code`, `gcc`, `clang`, `rustc`, `node` | JetBrains IDEs, VS Code, Compilers |
| **`APP_GROUP_BROWSER`** | `firefox`, `chrome`, `chromium`, `brave`, `safari`, `edge`, `webkit` | Firefox, Chromium |
| **`APP_GROUP_SYSTEM`** | `init`, `systemd`, `kworker`, `udevd`, `sshd`, `dbus`, `syslog`, `journald` | OS init, Linux kernel threads, SSH |
| **`APP_GROUP_OTHER`** | Unrecognized custom user processes | User scripts, computational jobs |

---

## 5. SQLite Persistence Schema

All samples are persisted into SQLite (`virtual_os.db` or configured path) with non-destructive migrations:

### `resource_samples`
Stores combined hypervisor and guest metrics:
```sql
CREATE TABLE IF NOT EXISTS resource_samples (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    vm_id           TEXT NOT NULL,
    timestamp_ms    INTEGER NOT NULL,
    metric_source   TEXT NOT NULL,      -- 'host', 'guest', 'combined'
    vm_state        TEXT NOT NULL,
    qemu_pid        INTEGER NOT NULL,
    qemu_cpu_pct    REAL NOT NULL,
    qemu_rss_bytes  INTEGER NOT NULL,
    guest_cpu_pct   REAL NOT NULL,      -- -1.0 if agent unavailable
    guest_mem_used  INTEGER NOT NULL,
    guest_mem_total INTEGER NOT NULL,
    disk_read_b     INTEGER NOT NULL,
    disk_write_b    INTEGER NOT NULL,
    net_rx_b        INTEGER NOT NULL,
    net_tx_b        INTEGER NOT NULL,
    created_at      TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))
);
CREATE INDEX IF NOT EXISTS idx_res_samples_vm_time ON resource_samples(vm_id, timestamp_ms);
```

### `process_samples`
Stores granular in-guest process telemetry:
```sql
CREATE TABLE IF NOT EXISTS process_samples (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    vm_id           TEXT NOT NULL,
    timestamp_ms    INTEGER NOT NULL,
    pid             INTEGER NOT NULL,
    process_name    TEXT NOT NULL,
    category        TEXT NOT NULL,      -- 'creative', 'devtools', 'browser', 'system', 'other'
    cpu_percent     REAL NOT NULL,
    rss_bytes       INTEGER NOT NULL,
    virtual_bytes   INTEGER NOT NULL,
    created_at      TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))
);
CREATE INDEX IF NOT EXISTS idx_proc_samples_vm_time ON process_samples(vm_id, timestamp_ms);
```

### `resource_limits`
Stores application-level resource constraints and enforcement logs:
```sql
CREATE TABLE IF NOT EXISTS resource_limits (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    vm_id               TEXT NOT NULL,
    process_identifier  TEXT NOT NULL,
    cpu_limit           REAL NOT NULL,  -- Quota % or -1.0
    memory_limit        INTEGER NOT NULL, -- Max RAM bytes
    enforcement_status  TEXT NOT NULL,  -- 'enforced', 'unsupported', 'permission_denied', 'protected_process'
    detail              TEXT NOT NULL DEFAULT '',
    updated_at          TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))
);
```

