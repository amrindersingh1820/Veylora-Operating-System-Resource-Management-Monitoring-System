# Virtual OS Simulator – VM Operations & Management Guide

This document describes the operational workflows for configuring, launching, monitoring, and managing real ARM64 virtual machines on Apple Silicon macOS using the **Virtual OS Simulator (Phase 2)**.

---

## 1. Overview & Architectural Principles

Virtual OS Simulator provides safe, isolated execution of ARM64 guest operating systems on Apple Silicon Macs using QEMU with hardware acceleration (`-accel hvf` via Apple's Hypervisor.framework).

Key operating principles:
* **Separation of Sim and Hypervisor**: The academic CPU scheduling and memory allocation algorithms in the simulator operate on a deterministic analytical model. Real virtual machines run their own guest OS kernel schedulers and virtual memory subsystems.
* **Process State vs. Guest OS Readiness**: A running QEMU process confirms that the hypervisor started, allocated memory, and loaded UEFI firmware. It does **not** prove that the guest OS has completed booting, initialized services, or reached a login prompt. These states are reported separately.
* **Safe Process Management**: All QEMU processes are invoked directly using `posix_spawn()` with programmatic argument arrays (`argv`). No user strings are passed through a shell (`/bin/sh` or `system()`), eliminating command injection vulnerabilities.
* **Data Safety**: The simulator will never overwrite existing virtual disks or delete user disk images automatically. Deleting a VM configuration removes its database entry and audit trail, but leaves all disk and ISO files intact on the host.

---

## 2. Guest OS Image Management & Types

Operating ARM64 virtual machines on QEMU's `-M virt` platform requires understanding three distinct image roles:

| Image Role | Typical Format | QEMU Attachment | Purpose |
| :--- | :--- | :--- | :--- |
| **Installation Media** | `.iso` | `-cdrom <path>` | Read-only installer image containing the bootloader, kernel, and installation wizard. |
| **Prepared Virtual Disk** | `.qcow2`, `.raw`, `.img` | `-drive file=<path>,format=qcow2,if=virtio` | Empty or pre-allocated virtual hard disk where the guest OS will be installed or has been installed. |
| **Bootable Guest Installation** | `.qcow2`, `.raw` | `-drive file=<path>,format=qcow2,if=virtio` | Fully installed guest operating system with its own EFI system partition and bootloader. |

> [!IMPORTANT]
> **ARM64 `-M virt` Machine Architecture Requirement:**
> Unlike generic x86 PC machines (`q35` or `i440fx`) which include legacy SeaBIOS in the QEMU binary, the ARM64 `-M virt` machine platform has no legacy BIOS. It requires EDK2 UEFI firmware (`edk2-aarch64-code.fd`) to initialize the virtual hardware and boot EFI bootloaders on ISOs and virtual disks. Virtual OS Simulator automatically detects and loads this firmware from Homebrew (`/opt/homebrew/share/qemu/edk2-aarch64-code.fd`).

### Preparing Compatible ARM64 Images

To obtain and prepare a lightweight, compatible ARM64 Linux guest:

```bash
# 1. Create a workspace directory for guest images
mkdir -p vm_images && cd vm_images

# 2. Download the official Alpine Linux standard ARM64 ISO (~212 MB)
curl -LO https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/aarch64/alpine-standard-3.20.0-aarch64.iso
mv alpine-standard-3.20.0-aarch64.iso alpine-arm64.iso

# 3. Create a 4 GB sparse QCOW2 virtual disk (occupies ~196 KB initially)
qemu-img create -f qcow2 alpine.qcow2 4G
```

---

## 3. Managing VMs via the Interactive Terminal CLI

The Virtual OS Simulator CLI provides full virtual machine lifecycle management via menu option `[8]`:

```bash
# Launch the CLI interface
./virtual_os_cli
# Or run with the main executable
./virtual_os --cli
```

### CLI Menu Options

From the main menu, enter `8` to open the **Virtual Machine Manager**:

```
 === VIRTUAL MACHINE MANAGER (ARM64 / QEMU) === 
 [1] List All Virtual Machines
 [2] View VM Details & Event Log
 [3] Create New VM Configuration
 [4] Start Virtual Machine
 [5] Stop Running VM
 [6] Refresh / Check VM Status
 [7] Delete VM Configuration
 [0] Return to Main Menu
```

### Operation 1: Create a VM Configuration (`[3]`)
1. Enter an identifier (e.g. `alpine_dev`). Allowed characters: alphanumeric, dashes, and underscores.
2. Enter a friendly display name (e.g. `Alpine Linux 3.20 ARM64`).
3. Enter the OS image path (e.g. `vm_images/alpine-arm64.iso` or `vm_images/alpine.qcow2`). The simulator validates that the file exists and has a supported extension (`.iso`, `.qcow2`, `.raw`, `.img`).
4. Enter an optional secondary disk image path (e.g. `vm_images/alpine.qcow2`), or press Enter to skip.
5. Specify virtual CPUs (1 to host core count, default 2).
6. Specify RAM in MiB (128 to 90% of host RAM, default 1024 or 2048).
7. Choose a display backend:
   * `[0] none`: Headless execution. Serial console output is redirected to `<work_dir>/<vm_id>_console.log`.
   * `[1] cocoa`: Opens a native macOS Cocoa GUI window.
   * `[2] sdl`: Opens an SDL2 window.
   * `[3] vnc`: Starts a local VNC server bound to `127.0.0.1:5900` (connect with macOS Screen Sharing or TigerVNC).
8. Choose network mode:
   * `[0] none`: Fully isolated from host and external networks.
   * `[1] user`: Safe user-mode NAT (`-nic user,model=virtio-net-pci`). Requires no root/elevated privileges and does not bridge host interfaces.

### Operation 2: Start a Virtual Machine (`[4]`)
1. Enter the configured VM ID.
2. The simulator verifies that QEMU is present and EDK2 firmware is accessible.
3. The simulator checks host resource limits and validates configuration parameters.
4. The QEMU child process is spawned via `posix_spawn()`.
5. An immediate 150ms startup probe executes `waitpid(pid, &wstatus, WNOHANG)`. If QEMU encounters invalid syntax, corrupted disk headers, or startup faults, the error and console log snippet are captured immediately, and the VM state is transitioned to `failed`.
6. Upon a successful probe, the VM state is marked `running`, the live PID is recorded, and an event is persisted to SQLite.

### Operation 3: Inspect VM Details & Audit Log (`[2]`)
Shows all configured hardware parameters, live PID, process state, guest readiness indicator, and the recent audit trail from the `vm_events` SQLite table.

### Operation 4: Gracefully Stop a Virtual Machine (`[5]`)
1. Sends `SIGTERM` to the QEMU child process to permit the guest OS kernel and QEMU to flush disk caches and shut down cleanly.
2. Polls `waitpid()` with a 5000ms timeout.
3. If the process does not terminate within the timeout window, safely escalates to `SIGKILL` to prevent orphan hypervisor processes.
4. Reaps the child process and updates the database state to `stopped`.

### Operation 5: Delete VM Configuration (`[7]`)
Removes the VM configuration and lifecycle event history from SQLite.
> [!NOTE]
> Deletion is rejected if the VM is currently `running` or `starting`. Stop the VM first before deleting its configuration. Virtual disk and ISO files on the host filesystem are preserved.

---

## 4. C11 API Integration

Developers embedding or extending the Virtual OS Simulator backend can use the virtualization API directly from C11:

```c
#include "virtualization/vm_manager.h"
#include "virtualization/vm_config.h"

int main(void)
{
    /* 1. Initialize the VM manager */
    VmManager *mgr = vm_manager_init(NULL); /* NULL searches system PATH and Homebrew */

    /* 2. Configure the database for persistence */
    vm_manager_set_db_path("virtual_os.db");
    vm_manager_reconcile_db(mgr); /* Fix any stale states after crash/reboot */

    /* 3. Define the VM configuration */
    VmConfig cfg;
    vm_config_init(&cfg);
    snprintf(cfg.id, sizeof cfg.id, "alpine_01");
    snprintf(cfg.name, sizeof cfg.name, "Alpine Linux ARM64");
    snprintf(cfg.os_image_path, sizeof cfg.os_image_path, "vm_images/alpine-arm64.iso");
    snprintf(cfg.disk_image_path, sizeof cfg.disk_image_path, "vm_images/alpine.qcow2");
    snprintf(cfg.work_dir, sizeof cfg.work_dir, ".");
    cfg.arch       = VM_ARCH_AARCH64;
    cfg.cpu_count  = 2;
    cfg.memory_mb  = 1024;
    cfg.display    = VM_DISPLAY_NONE;
    cfg.net        = VM_NET_USER;

    /* 4. Validate configuration against host limits */
    char err[512] = {0};
    char qemu_path[512] = {0};
    vm_manager_detect_qemu(mgr, cfg.arch, qemu_path, sizeof qemu_path, err, sizeof err);
    if (vm_config_validate(&cfg, qemu_path, err, sizeof err) != VM_OK) {
        fprintf(stderr, "Configuration error: %s\n", err);
        return 1;
    }

    /* 5. Start the virtual machine */
    if (vm_start(mgr, &cfg, err, sizeof err) != VM_MGR_OK) {
        fprintf(stderr, "Failed to start VM: %s\n", err);
        return 1;
    }

    VmHandle h;
    vm_get_handle(mgr, cfg.id, &h);
    printf("VM running with PID %d (Log: %s)\n", (int)h.pid, h.console_log);

    /* 6. Periodically reap any unexpectedly exited children */
    vm_reap_children(mgr);

    /* 7. Gracefully stop the VM */
    vm_stop(mgr, cfg.id, 5000, err, sizeof err);

    /* 8. Clean up manager */
    vm_manager_shutdown(mgr);
    return 0;
}
```

---

## 5. Lifecycle State Machine

The Virtual OS Simulator tracks seven distinct lifecycle states:

```
                  ┌──────────────┐
                  │  configured  │ (Initial state in database)
                  └──────┬───────┘
                         │
                         ▼
                  ┌──────────────┐
                  │ ready_to_start
                  └──────┬───────┘
                         │ vm_start() called
                         ▼
                  ┌──────────────┐
                  │   starting   │ (posix_spawn called, 150ms startup probe active)
                  └──┬────────┬──┘
      Probe failed   │        │ Probe passed
                     ▼        ▼
       ┌──────────────┐      ┌──────────────┐
       │    failed    │      │   running    │ (PID active, guest booting)
       └──────▲───────┘      └──────┬───────┘
              │                     │ vm_stop() called
    Unexpected│                     ▼
         crash│              ┌──────────────┐
              └──────────────┤   stopping   │ (SIGTERM issued, awaiting exit)
                             └──────┬───────┘
                                    │ Clean exit or timeout -> SIGKILL
                                    ▼
                             ┌──────────────┐
                             │   stopped    │
                             └──────────────┘
```

### Distinguishing Process State and Guest OS Readiness

* **`state == VM_STATE_RUNNING`**: The QEMU hypervisor process is running under macOS Hypervisor.framework (`pid > 0`), virtual CPU threads are active, and firmware is executing.
* **`h->guest_ready == 0`**: The guest OS is booting. EDK2 UEFI initialization, kernel decompression, hardware probing, and init scripts are in progress.
* **`h->guest_ready == 1`**: Reserved for future guest agent / QEMU Guest Agent (QGA) socket handshakes confirming the OS reached runlevel/target.

---

## 6. Host Resource Impact & Limitations

### Memory Allocation
Allocating memory to a virtual machine (e.g. 2048 MiB) locks that physical memory from macOS.
* On macOS Apple Silicon, unified memory is shared dynamically between CPU and GPU.
* The simulator validates that requested memory does not exceed **90% of total host RAM** (checked via `sysctl hw.memsize`).
* **Recommendation**: For development on an 8 GB or 16 GB MacBook Air, allocate **1024 MiB to 2048 MiB** per VM.

### CPU Allocation
* Default vCPU count is set to **2**, with a maximum cap equal to the number of host CPU cores (detected via `sysctl hw.ncpu`).
* Allocating more vCPUs than physical performance cores introduces virtualization context-switching overhead.

### GPU & Acceleration Limitations
* Virtual OS Simulator provides accelerated CPU and memory virtualization via Apple's **Hypervisor.framework (`-accel hvf`)**.
* **Direct GPU passthrough is NOT supported**: macOS Apple Silicon does not support PCIe GPU passthrough to virtual machines in QEMU.
* Emulated display adapters (`ramfb`, `virtio-gpu-pci`) provide framebuffer rendering. 3D Metal acceleration inside the guest is not supported by QEMU's aarch64 target on macOS.

---

## 7. Crash Recovery & Database Reconciliation

If the simulator crashes, macOS restarts, or a power failure occurs while virtual machines are marked `running` in SQLite:

1. On the subsequent launch of `virtual_os` or `virtual_os_cli`, `vm_manager_reconcile_db()` is automatically invoked.
2. The function queries all records where `status IN ('starting', 'running', 'stopping')`.
3. For each record, it verifies whether the process is genuinely active using `kill(pid, 0)`.
4. Stale records are automatically updated to `stopped`, and an explanatory audit event is logged:
   `"Reconciled stale state 'running' -> 'stopped' on startup"`
5. This ensures the database never presents stale running states to frontends or users.

---

## 8. Troubleshooting

| Symptom | Cause | Solution |
| :--- | :--- | :--- |
| `QEMU executable not found` | QEMU is not installed or not in PATH | Run `brew install qemu`. Verify with `which qemu-system-aarch64`. |
| `EDK2 firmware not found` | Homebrew share directory missing | Reinstall QEMU via Homebrew (`brew reinstall qemu`). Verify `/opt/homebrew/share/qemu/edk2-aarch64-code.fd`. |
| `Unsupported image format (.xyz)` | File extension is not `.iso`, `.qcow2`, `.raw`, `.img`, or `.bin` | Convert disk image to QCOW2 via `qemu-img convert -O qcow2 source.xyz disk.qcow2`. |
| `QEMU failed to start (exit code 1)` | Invalid disk image header or missing permissions | Inspect `<work_dir>/<vm_id>_console.log` for exact QEMU error diagnostics. |
| `Cannot delete VM: state is running` | Safety check preventing deletion of active VM | Stop the VM using menu option `[5]` before deleting configuration. |
| VM starts but guest hangs at EFI Shell | ISO image does not contain an ARM64 EFI bootloader | Ensure the ISO is an **ARM64/AArch64** release (e.g. `alpine-standard-*-aarch64.iso`), not an x86_64 ISO. |
