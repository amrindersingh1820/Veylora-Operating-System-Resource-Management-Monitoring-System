# Virtual OS Simulator – Development Plan & Roadmap

This document outlines the multi-phase engineering roadmap for extending the Virtual OS Simulator from an academic OS simulation engine into an industrial-grade virtual machine management platform and desktop sandbox environment.

---

## Roadmap Overview

| Phase | Milestone Name | Status | Primary Focus |
|---|---|---|---|
| **Phase 1** | Codebase Audit, macOS Build, & QEMU Foundation | **Completed** | C11 architecture audit, macOS build verification, SQLite VM schema, QEMU detection, POSIX process management, test suite |
| **Phase 2** | Real ARM64 Virtual Machine Execution on macOS | **Completed** | Real ARM64 boot (`qemu-system-aarch64` + `-accel hvf`), UEFI EDK2 firmware, storage formats, graceful shutdown, interactive CLI |
| **Phase 3** | Real-Time Resource Monitoring & Resource Management | **Completed** | Host Mach statistics, C11 Linux guest agent (`vos_guest_agent`), virtio-serial JSON telemetry, application classification, cgroups v2 & nice limits |
| **Phase 4** | C Backend Headless IPC & Flutter FFI Integration | **Next Up** | C API / FFI header bindings, UNIX domain socket IPC daemon, asynchronous telemetry streaming |
| **Phase 5** | Flutter / Dart Desktop Frontend | Planned | Modern cross-platform macOS desktop GUI communicating over FFI/IPC with C backend |
| **Phase 6** | GUI Guest Acceleration & Desktop App Compatibility | Planned | VirtIO-GPU / Apple Virtualization framework, Spice/VNC displays, JetBrains/Adobe compatibility research |

---

## Detailed Milestone Breakdown

### Phase 1: Codebase Audit, macOS Build, & QEMU Foundation (COMPLETED)
* **Goal**: Establish a rock-solid, verified C11 foundation on Apple Silicon (ARM64) macOS with safe hypervisor process management.
* **Deliverables**:
  * Complete codebase audit distinguishing simulated algorithms from hypervisor management.
  * Universal cross-platform `Makefile` and `CMakeLists.txt` supporting macOS (Apple Silicon / Intel), Linux, and Windows.
  * VM configuration model (`vm_config.c`, `vm_config.h`) with strict input sanitization, CPU/memory bounds checking, and image validation.
  * Hypervisor discovery engine detecting QEMU in `PATH` and Apple Silicon Homebrew paths (`/opt/homebrew/bin`).
  * Process management engine (`vm_manager.c`, `vm_manager.h`) utilizing `posix_spawn()`, argument array construction (zero shell injection risk), two-stage SIGTERM -> SIGKILL shutdown, and `waitpid` child reaping.
  * SQLite persistence extension (`database.c`, `database.h`) introducing `virtual_machines` and `vm_events` tables with foreign keys and WAL mode.
  * Automated test suite executing 256 core checks with zero failures.

---

### Phase 2: Real ARM64 Virtual Machine Execution on macOS (COMPLETED)
* **Goal**: Enable booting and managing a real, minimal ARM64 guest operating system on Apple Silicon with hypervisor acceleration.
* **Deliverables**:
  * ARM64 QEMU runner: `-M virt -accel hvf -cpu host` with EDK2 UEFI firmware auto-resolution (`edk2-aarch64-code.fd`).
  * Discriminating boot storage: Bootable ISO (`-cdrom`) vs virtual disk (`-drive file=...,format=qcow2/raw,if=virtio`).
  * Multi-display backend: Cocoa native GUI window, VNC server (`127.0.0.1:5900`), and headless console redirection.
  * Strict lifecycle state tracking (`CONFIGURED`, `READY`, `STARTING`, `RUNNING`, `STOPPING`, `STOPPED`, `FAILED`).
  * Immediate 150ms startup probe & asynchronous child reaping (`vm_reap_children`).
  * Stale state database reconciliation across reboots (`vm_manager_reconcile_db`).
  * Integrated interactive CLI VM Manager menu.
  * 114 automated VM checks passing with zero failures.

---

### Phase 3: Real-Time Resource Monitoring & Resource Management (COMPLETED)
* **Goal**: Real resource monitoring and management collecting genuine, source-attributed telemetry from macOS host and running VMs.
* **Deliverables**:
  * **Host Resource Monitor (`src/monitoring/host_monitor.c`, `host_monitor.h`)**: Direct macOS Mach microkernel APIs (`HOST_CPU_LOAD_INFO`, `HOST_VM_INFO64`, `proc_pidinfo`), sysctl core and memory queries. Zero shell execution.
  * **Guest Monitoring Agent (`guest_agent/`)**: Standalone, lightweight C11 Linux ARM64 daemon (`vos_guest_agent`) extracting telemetry from `/proc/stat`, `/proc/meminfo`, `/proc/diskstats`, `/proc/net/dev`, and `/proc/[pid]/`.
  * **Virtio-Serial Telemetry Protocol (`src/monitoring/agent_protocol.c`, `agent_protocol.h`)**: Structured JSON wire format over `/dev/virtio-ports/org.virtualos.agent.0` <-> `/tmp/vos_agent_<vm_id>.sock`.
  * **Process Monitor & Application Classifier (`src/monitoring/process_monitor.c`, `process_monitor.h`)**: Categorizes in-guest applications (Creative, DevTools, Browser, System, Other) and provides CPU/memory ranking and filtering.
  * **Resource Management & Limits (`src/monitoring/resource_limits.c`, `resource_limits.h`)**:
    * VM hardware boundary verification (`-smp`, `-m`).
    * Linux cgroups v2 enforcement (`cpu.max`, `cpu.weight`, `memory.max`) with mandatory PID 1 protection.
    * macOS host QEMU process priority management (`setpriority` nice values).
  * **VM Monitor Telemetry Engine (`src/monitoring/vm_monitor.c`, `vm_monitor.h`)**: Background POSIX threads, non-blocking polling, SQLite persistence (`resource_samples`, `process_samples`, `resource_limits`).
  * **GPU Reality Investigation (`docs/GPU_SUPPORT.md`)**: Empirical assessment of Apple Silicon unified memory vs emulated framebuffers; marks GPU metrics unavailable rather than fabricating values.
  * **Documentation**: `docs/MONITORING_ARCHITECTURE.md`, `docs/RESOURCE_MANAGEMENT.md`, `docs/GUEST_AGENT_SETUP.md`, `docs/GPU_SUPPORT.md`.
  * **Test Suite**: `tests/test_monitoring.c` passing 132 checks (total test suite: 502 checks passing, 0 failures).

---

### Phase 5: Flutter / Dart Desktop Frontend
* **Goal**: Deliver a responsive, state-of-the-art desktop dashboard for macOS.
* **Key Tasks**:
  1. **Dart IPC Client**:
     * Dart FFI or socket client interacting directly with the Phase 3 IPC daemon.
  2. **Real-time Dashboard**:
     * Live CPU/Memory telemetry gauges, VM lifecycle controls (Start, Pause, Shutdown, Kill), interactive log terminal.
  3. **Algorithm Visualizer**:
     * Visual replay mode for the simulated OS scheduling and memory management components alongside live VM metrics.

---

### Phase 6: GUI Guest Acceleration & Desktop App Compatibility Research
* **Goal**: Investigate and prototype running real graphical desktop applications inside isolated guest VMs.
* **Key Tasks**:
  1. **Virtual Display & Graphics Drivers**:
     * Implement VirtIO-GPU (`-device virtio-gpu-pci`) with SPICE / VNC display forwarding to the host UI.
  2. **Desktop Application Compatibility Matrix**:
     * **JetBrains IDEs (IntelliJ, CLion, PyCharm)**:
       * *Platform*: ARM64 Linux guest.
       * *Feasibility*: High. JetBrains provides native aarch64 Linux tarballs (JDK/JBR included).
     * **Adobe Photoshop & Adobe Lightroom**:
       * *Platform*: Windows x86_64 / Windows on ARM.
       * *Feasibility*: Challenging. Adobe Creative Cloud requires Windows or macOS with active licensing services, GPU D3D12/Metal acceleration, and kernel-level anti-piracy modules. Running Adobe CC inside ARM64 Linux via Wine is unsupported; running inside Windows 11 ARM on QEMU requires significant emulation overhead and nested virtualization.
  3. **Hypervisor Driver Alternative (Apple Virtualization.framework)**:
     * Evaluate replacing QEMU with macOS native `VZVirtualMachine` for macOS and Linux guests, unlocking native Metal acceleration, Shared Directories (VirtioFS), and high performance.
