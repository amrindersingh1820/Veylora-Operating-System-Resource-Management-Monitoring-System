# Virtual OS Simulator – Guest Monitoring Agent (Linux ARM64)

The **Virtual OS Simulator Guest Monitoring Agent** (`vos_guest_agent`) is a lightweight, non-intrusive daemon designed to run inside guest operating systems (such as Linux ARM64). It queries the Linux kernel `/proc` filesystem directly and streams structured telemetry to the host backend over a dedicated virtual channel.

---

## 1. Capabilities & Telemetry Collected

| Subsystem | Linux Kernel Source | Metrics Collected |
| :--- | :--- | :--- |
| **CPU** | `/proc/stat` | Overall CPU utilization (%), User %, System %, Idle %, I/O Wait %, Per-core CPU utilization (%), and raw CPU ticks. |
| **Memory** | `/proc/meminfo` | Total RAM, Free RAM, Available RAM, Used RAM, Buffers, Cached memory, and Swap usage. |
| **Disk I/O** | `/proc/diskstats` | Cumulative read bytes, write bytes, read operations, write operations, and I/O requests in flight. |
| **Network** | `/proc/net/dev` | Received bytes/packets/errors, transmitted bytes/packets/errors, and active network interface name. |
| **Processes** | `/proc/[pid]/stat`, `/proc/[pid]/status` | Process list: PID, Parent PID, executable name, state (Running, Sleeping, Disk Sleep, Zombie), CPU utilization (%), RSS memory, Virtual memory size, and start time. |

---

## 2. Security & Design Principles

1. **Strictly Read-Only Telemetry**: The agent only reads `/proc` kernel statistics and formats telemetry payloads. It does **not** expose a remote shell, arbitrary command execution, or execution endpoints.
2. **Privacy Guarantees**: Does **not** inspect user files, bash history, passwords, environment variables, browser cookies, or network payload packets.
3. **Bounded Overhead**: Memory usage is fixed (<2 MB RSS). Process enumeration sorts and truncates to the top processes (default 256) to ensure low token/bandwidth overhead.
4. **Resilience to Process Churn**: Gracefully handles processes that spawn and terminate during `/proc` directory traversal without crashing or logging spurious errors.

---

## 3. Communication Channels

The agent supports three communication backends:

### A. Virtio-Serial Channel (Recommended / Default)
* In QEMU on the macOS host, a virtio-serial port is attached:
  `-chardev socket,id=vos_agent,path=/tmp/vos_agent_<vm_id>.sock,server=on,wait=off -device virtio-serial-pci -device virtserialport,chardev=vos_agent,name=org.virtualos.agent.0`
* Inside the guest, the Linux kernel `virtio_console` driver presents this as:
  `/dev/virtio-ports/org.virtualos.agent.0`
* **Advantage**: High throughput, low latency, completely isolated from network routing, and functions even when VM networking is set to `VM_NET_NONE`.

### B. TCP Socket Channel (Testing / Remote Mode)
* The agent can listen on a local port (e.g., `--tcp 9888`).
* QEMU user networking can forward this port via hostfwd (`hostfwd=tcp:127.0.0.1:9888-:9888`).

### C. Standard Output (`--stdout`)
* Emits line-delimited JSON directly to stdout for testing, scripting, or pipe forwarding.

---

## 4. Building & Running in the Guest

### Build Prerequisites
* GCC or Clang
* GNU Make
* C11 standard library (glibc or musl libc)

### Build Instructions
```bash
# In Alpine Linux / Debian / Ubuntu / Fedora inside the VM:
make
```

### Running the Agent
```bash
# 1. Run over Virtio-Serial channel (default)
./vos_guest_agent --vm-id alpine_dev --channel /dev/virtio-ports/org.virtualos.agent.0 --interval 1000

# 2. Run in TCP server mode
./vos_guest_agent --vm-id alpine_dev --tcp 9888 --interval 1000

# 3. Test sample output on stdout
./vos_guest_agent --vm-id test_vm --stdout --oneshot
```

