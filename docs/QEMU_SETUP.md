# Virtual OS Simulator – QEMU Setup & Virtualization Foundation

This document details the QEMU virtualization subsystem in **Virtual OS Simulator**, covering installation, capability verification, guest configuration, process lifecycle, limitations, and practical application compatibility on Apple Silicon macOS.

---

## 1. Installing QEMU

### macOS (Apple Silicon: M1 / M2 / M3 / M4 / M5)

Install QEMU using Homebrew:
```bash
brew install qemu
```
On Apple Silicon, Homebrew places binaries in `/opt/homebrew/bin/qemu-system-aarch64`.

### Linux (Debian / Ubuntu / Fedora)
```bash
# Ubuntu / Debian
sudo apt-get update && sudo apt-get install qemu-system-arm qemu-utils

# Fedora / RHEL
sudo dnf install qemu-system-aarch64 qemu-img
```

---

## 2. Checking QEMU Capabilities on Apple Silicon

After installation, verify that QEMU is present and supports native **HVF** (Hypervisor.framework) hardware acceleration:

```bash
# 1. Verify binary version
qemu-system-aarch64 --version

# 2. Check supported hardware accelerators
qemu-system-aarch64 -accel help
```

Expected output on Apple Silicon macOS:
```
Accelerators supported in QEMU:
hvf
tcg
```
* **`hvf`** (Hypervisor.framework): Native near-bare-metal virtualization for ARM64 guests on Apple Silicon.
* **`tcg`** (Tiny Code Generator): Software emulation fallback (orders of magnitude slower; only used when running non-native architectures like x86 on ARM64).

---

## 3. How the Virtual OS Simulator Manages QEMU

The C backend integrates QEMU through a strictly isolated process management layer (`src/virtualization/vm_manager.c`):

1. **Detection**: `vm_manager_detect_qemu()` searches the host `PATH` followed by platform fallback locations (`/opt/homebrew/bin/qemu-system-aarch64`, `/usr/local/bin/qemu-system-aarch64`).
2. **Safe Argument Construction**: CLI arguments are marshaled into an array without shell interpolation (`/bin/sh`), preventing command injection.
3. **Execution**: Processes are spawned using `posix_spawn()`, detached into their own process groups, with standard file descriptors securely redirected.
4. **Lifecycle States**:
   * `created` -> `disk_ready` -> `starting` -> `running` -> `stopping` -> `stopped` (or `failed`).
5. **Graceful Stop**: `vm_stop()` issues a `SIGTERM` to the process group. If the VM does not terminate within a specified timeout window (e.g., 3000ms), it safely escalates to `SIGKILL`.
6. **Persistence**: All VM configurations and lifecycle transitions are recorded in SQLite (`virtual_machines` and `vm_events` tables).

---

## 4. Preparing a Test ARM64 Guest Image

For Phase 1 and Phase 2 testing, use a lightweight ARM64 distribution such as **Alpine Linux**:

```bash
# 1. Create a project VM directory
mkdir -p vm_images && cd vm_images

# 2. Download Alpine Linux standard ARM64 ISO
curl -LO https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/aarch64/alpine-standard-3.20.0-aarch64.iso

# 3. Create a 4GB sparse QCOW2 virtual disk image
qemu-img create -f qcow2 alpine-disk.qcow2 4G
```

### Manual QEMU Test Run (Sanity Check)

To verify the image, EDK2 UEFI firmware, and HVF acceleration manually:
```bash
qemu-system-aarch64 \
    -M virt \
    -accel hvf \
    -cpu host \
    -smp 2 \
    -m 2048 \
    -bios /opt/homebrew/share/qemu/edk2-aarch64-code.fd \
    -cdrom alpine-standard-3.20.0-aarch64.iso \
    -drive file=alpine-disk.qcow2,if=virtio,format=qcow2 \
    -display none \
    -serial stdio
```

> **Why `-bios` is essential for ARM64 `-M virt`**: Unlike standard x86 PC machines (`q35` or `i440fx`) that bundle SeaBIOS inside the QEMU binary, ARM64 `-M virt` virtual hardware has no built-in BIOS. Booting ISOs or disk images requires EDK2 UEFI firmware (`edk2-aarch64-code.fd`), which Homebrew installs to `/opt/homebrew/share/qemu/`. Virtual OS Simulator discovers and supplies this automatically.

---

## 5. VM Configuration API Example (C11)

In client code or test harnesses, configure and launch a virtual machine programmatically:

```c
#include "virtualization/vm_config.h"
#include "virtualization/vm_manager.h"
#include <stdio.h>

int run_vm(void) {
    VmManager *mgr = vm_manager_init(NULL);
    if (!mgr) return -1;

    VmConfig cfg;
    vm_config_init(&cfg);

    snprintf(cfg.id, sizeof(cfg.id), "vm-alpine-01");
    snprintf(cfg.name, sizeof(cfg.name), "Alpine ARM64 Dev");
    cfg.arch = VM_ARCH_AARCH64;
    cfg.cpu_count = 2;
    cfg.memory_mb = 1024;
    snprintf(cfg.os_image_path, sizeof(cfg.os_image_path), "vm_images/alpine-arm64.iso");
    snprintf(cfg.disk_image_path, sizeof(cfg.disk_image_path), "vm_images/alpine.qcow2");
    snprintf(cfg.work_dir, sizeof(cfg.work_dir), ".");
    cfg.display = VM_DISPLAY_NONE;
    cfg.net     = VM_NET_USER;

    char err[512] = {0};
    char qemu_path[512] = {0};
    vm_manager_detect_qemu(mgr, cfg.arch, qemu_path, sizeof(qemu_path), err, sizeof(err));

    if (vm_config_validate(&cfg, qemu_path, err, sizeof(err)) != VM_OK) {
        fprintf(stderr, "Validation error: %s\n", err);
        vm_manager_shutdown(mgr);
        return -1;
    }

    if (vm_start(mgr, &cfg, err, sizeof(err)) != VM_MGR_OK) {
        fprintf(stderr, "Startup error: %s\n", err);
        vm_manager_shutdown(mgr);
        return -1;
    }

    VmHandle h;
    vm_get_handle(mgr, cfg.id, &h);
    printf("VM running! PID: %d, Log: %s\n", (int)h.pid, h.console_log);

    /* Gracefully stop after work */
    vm_stop(mgr, cfg.id, 5000, err, sizeof(err));
    vm_manager_shutdown(mgr);
    return 0;
}
```

---

## 6. Current Limitations & Realism Check

To maintain engineering integrity, the current implementation status and technical constraints are explicitly stated:

| Feature / Goal | Status | Technical Reality |
|---|---|---|
| **CPU / Memory Simulation** | Working | Algorithmic simulation engine for academic study. Does not govern guest OS kernel scheduling. |
| **QEMU Detection & Safe Spawning** | Working | Verified on macOS. Direct argument array execution without shell injection vulnerabilities. |
| **SQLite VM Config & Audit Persistence** | Working | Prepared statements, Foreign Key enforcement, WAL mode, transaction safety. |
| **Process Reaping & Graceful Shutdown** | Working | Two-stage SIGTERM -> 3s timeout -> SIGKILL escalation with non-blocking `waitpid`. |
| **ARM64 Linux Headless Boot** | Phase 2 | Requires user-provided disk image and EDK2 UEFI firmware for full unattended boot. |
| **QMP (QEMU Monitor Protocol) Socket** | Phase 2 | Currently relies on POSIX process signals; ACPI QMP shutdown will be added in Phase 2. |
| **JetBrains IDEs (IntelliJ, PyCharm)** | Phase 6 | Feasible in an ARM64 Linux guest with X11/Wayland display forwarding. Untested. |
| **Adobe Photoshop & Lightroom** | Untested / Heavy Constraints | Requires Windows or macOS. Windows 11 ARM on QEMU has significant graphics and licensing limitations. Native Apple Metal GPU pass-through is not supported in open-source QEMU on macOS. |
| **GPU Pass-Through on Apple Silicon** | Highly Restricted | Apple Silicon does not support PCIe GPU pass-through. VirtIO-GPU offers software rasterization in QEMU. Real Metal acceleration requires Apple's proprietary `Virtualization.framework`. |

---

## 7. Troubleshooting

### 1. `qemu-system-aarch64 not found in PATH`
* **Cause**: QEMU is not installed or Homebrew's bin folder is missing from your environment.
* **Resolution**: Run `brew install qemu` and verify `echo $PATH` includes `/opt/homebrew/bin`.

### 2. `Error: -accel hvf: No accelerator found`
* **Cause**: Running an x86_64 QEMU binary under Rosetta 2 emulation instead of the native ARM64 binary, or running on an unsupported architecture.
* **Resolution**: Verify binary architecture with `file $(which qemu-system-aarch64)`. It must output `Mach-O 64-bit executable arm64`.

### 3. Permission denied on guest disk image
* **Cause**: File permissions prevent read/write access to the virtual disk.
* **Resolution**: Check ownership with `ls -la /path/to/image.qcow2` and ensure the current user has `rw` permissions.
