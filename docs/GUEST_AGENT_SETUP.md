# Virtual OS Simulator – Guest Monitoring Agent Setup

## 1. Overview

The **Virtual OS Guest Monitoring Agent** (`vos_guest_agent`) is a lightweight, zero-dependency C11 daemon designed to run inside Linux ARM64 virtual machines. It continuously reads kernel telemetry from the Linux `/proc` filesystem and transmits structured JSON telemetry back to the Virtual OS Simulator host backend.

### Key Characteristics
- **Pure Portable C11**: Zero external dependencies (no libsystemd, no third-party JSON libraries, no Python).
- **Lightweight Footprint**: Memory footprint < 1.2 MB resident RAM; CPU consumption < 0.1% at 1000ms sampling.
- **Direct Kernel Telemetry**: Gathers true metrics directly from `/proc/stat`, `/proc/meminfo`, `/proc/diskstats`, `/proc/net/dev`, and `/proc/[pid]/`.
- **Virtio-Serial Transport**: Default output directly to `/dev/virtio-ports/org.virtualos.agent.0`.

---

## 2. Directory Structure

The agent codebase is located in `guest_agent/`:
```
guest_agent/
├── include/
│   ├── agent.h            # Main agent lifecycle and loop
│   ├── metrics.h          # Data structures for CPU, memory, disk, network, process
│   └── agent_protocol.h   # JSON serializer for telemetry payloads
├── src/
│   ├── cpu_monitor.c      # Parses /proc/stat
│   ├── memory_monitor.c   # Parses /proc/meminfo
│   ├── disk_monitor.c     # Parses /proc/diskstats
│   ├── network_monitor.c  # Parses /proc/net/dev
│   ├── process_monitor.c  # Scans /proc/[pid]/stat & status
│   ├── agent_protocol.c   # Compact JSON wire serializer
│   └── main.c             # Daemon entrypoint, argument parsing, socket/port I/O
├── Makefile               # Cross-platform / Linux ARM64 build configuration
└── README.md              # Quick start and command reference
```

---

## 3. Metric Collection Details

| Subsystem | Source Interface | Metrics Extracted |
| :--- | :--- | :--- |
| **CPU** | `/proc/stat` | User ticks, System ticks, Idle ticks, IO-wait ticks, IRQ/SoftIRQ ticks, total %, per-core % breakdown |
| **Memory** | `/proc/meminfo` | `MemTotal`, `MemFree`, `MemAvailable`, `Buffers`, `Cached`, `SwapTotal`, `SwapFree` |
| **Disk I/O** | `/proc/diskstats` | Sectors read/written, Read/Write ops count, I/O in progress for primary disks (`vda`, `sda`, `nvme0n1`) |
| **Network** | `/proc/net/dev` | Received bytes, Transmitted bytes, Packet count, Error count for active interface (`eth0`, `enp0s1`) |
| **Processes** | `/proc/[pid]/stat`<br>`/proc/[pid]/status` | PID, PPID, executable name, state (`R`, `S`, `D`, `Z`), user/system CPU ticks, RSS memory, virtual memory |

---

## 4. Building the Guest Agent

### Native Build (Inside Linux Guest)
When logged into the guest OS (e.g. Alpine Linux, Debian, Fedora ARM64):
```bash
# Alpine Linux prerequisites
apk add --no-cache gcc musl-dev make

# Debian / Ubuntu prerequisites
sudo apt-get update && sudo apt-get install -y build-essential

# Build binary
cd guest_agent
make
```

### Host Cross-Compilation (macOS ARM64 for Linux ARM64)
Because the host is Apple Silicon (ARM64) and the guest is Linux ARM64, cross-compilation can be accomplished using a musl-cross or Docker toolchain:
```bash
# Build statically linked ELF binary for Linux ARM64 using Docker
docker run --rm -v "$(pwd)/guest_agent":/src -w /src \
  --platform linux/arm64 alpine:latest \
  sh -c "apk add --no-cache gcc musl-dev make && make clean && make CFLAGS='-std=c11 -O2 -Wall -static'"
```

---

## 5. Running the Agent

### Command-Line Arguments
```bash
Usage: vos_guest_agent [OPTIONS]

Options:
  --vm-id <ID>          Set the VM identifier in telemetry payloads (default: 'guest_vm')
  --interval <MS>       Sampling interval in milliseconds (default: 1000)
  --port <DEVICE_PATH>  Virtio-serial device path (default: '/dev/virtio-ports/org.virtualos.agent.0')
  --tcp <PORT>          Transmit telemetry via TCP socket instead of virtio-serial
  --stdout              Print telemetry JSON directly to stdout (useful for debugging)
  --oneshot             Emit a single sample and exit immediately
  --daemon              Fork and run as background daemon
  --help                Show this help message
```

### Examples
1. **One-shot debug test to stdout**:
   ```bash
   ./vos_guest_agent --oneshot --stdout
   ```
2. **Foreground run using virtio-serial**:
   ```bash
   ./vos_guest_agent --vm-id alpine_arm64
   ```
3. **Background daemon mode**:
   ```bash
   ./vos_guest_agent --vm-id alpine_arm64 --daemon
   ```

---

## 6. Service Configuration

### Systemd Service (`/etc/systemd/system/vos-agent.service`)
For Debian, Ubuntu, or Fedora guests:
```ini
[Unit]
Description=Virtual OS Guest Monitoring Agent
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/vos_guest_agent --vm-id alpine_arm64
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```
Enable and start the service:
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now vos-agent
```

### OpenRC Service (`/etc/init.d/vos-agent`)
For Alpine Linux guests:
```sh
#!/sbin/openrc-run

name="vos-agent"
description="Virtual OS Guest Monitoring Agent"
command="/usr/local/bin/vos_guest_agent"
command_args="--vm-id alpine_arm64 --daemon"
pidfile="/run/vos-agent.pid"

depend() {
    need localmount
    after bootmisc
}
```
Enable and start:
```bash
chmod +x /etc/init.d/vos-agent
rc-update add vos-agent default
service vos-agent start
```

