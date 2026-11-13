# Virtual OS Simulator — Phase 7 UI Implementation Report

## 1. Architectural Architecture & Desktop Shell

Phase 7 completely redesigns and polishes the Flutter/Dart desktop application for macOS Apple Silicon. The application is structured around a centralized reactive state model (`VosService`) communicating with the underlying C11 virtualization engine (`libvos_backend.dylib`) via Dart FFI, wrapped in a high-fidelity macOS desktop interface.

### 1.1 Shell Hierarchy & State Flow

```
                      ┌────────────────────────────────────────┐
                      │             VirtualOsApp               │
                      │  (ListenableBuilder -> ThemeMode Bind) │
                      └──────────────────┬─────────────────────┘
                                         │
                      ┌──────────────────▼─────────────────────┐
                      │            VirtualOsShell              │
                      └───────┬────────────────────────┬───────┘
                              │                        │
            ┌─────────────────▼────────┐      ┌────────▼───────────────────────┐
            │    NavigationSidebar     │      │        Main View Column        │
            │ (Collapsible 8 Routes)   │      │ ┌────────────────────────────┐ │
            └──────────────────────────┘      │ │         AppTopBar          │ │
                                              │ │(VM Selector, Agent, Theme) │ │
                                              │ ├────────────────────────────┤ │
                                              │ │     Global Error Banner    │ │
                                              │ ├────────────────────────────┤ │
                                              │ │        IndexedStack        │ │
                                              │ │     (8 Screen Views)       │ │
                                              │ └────────────────────────────┘ │
                                              └────────────────────────────────┘
```

The `IndexedStack` maintains the state, scroll positions, and interactive form inputs of all 8 views without re-mounting widgets during sidebar transitions.

---

## 2. Comprehensive Screen Walkthrough

### 2.1 View 1: System Dashboard (`dashboard_screen.dart`)
- **System Topology Banner**: Displays Apple Silicon host identification, Mach kernel release, QEMU HVF hypervisor status, and Linux 6.6.31 guest kernel state.
- **KPI Metrics Grid**: Four `MetricCard` components reporting:
  1. *Host CPU Utilization* (Mach kernel samples, core count, idle percentage).
  2. *Host Memory Allocation* (Used RAM vs Available RAM, swap state).
  3. *Active Virtual Machines* (Running instances vs total provisioned).
  4. *In-Guest Cgroup Applications* (Active processes under cgroups v2 control).
- **Time-Series Charts**: Two synchronized `MetricChart` widgets rendering 60-second historical trends for CPU and Memory with cubic Bezier curves and area gradients.
- **Workloads & Events**: Live data table of running VMs with quick navigation and real-time audit log stream.

### 2.2 View 2: Virtual Machine Manager (`vm_screen.dart`)
- **Firmware & Hypervisor Banner**: Documents EDK2 UEFI code path (`/opt/homebrew/share/qemu/edk2-aarch64-code.fd`) and QEMU HVF acceleration.
- **Filter & Search Toolbar**: Filter by state (All, Running, Stopped, Failed) with instant text search and Grid/List view toggle.
- **VM Creation Wizard**: A 3-step modal wizard:
  - *Step 1: Identity & Profile*: Name, OS distribution (Alpine Linux ARM64, Ubuntu, Generic Linux), VM ID.
  - *Step 2: Hardware Resources*: vCPU allocation (1-8 cores), Guest RAM (512 MB - 8192 MB).
  - *Step 3: Storage & Firmware*: QCOW2 primary image path, ISO installation media, UEFI boot toggle.
- **Interactive VM Cards**: Displays hardware configuration, host QEMU process RSS, guest agent readiness, and contextual controls (`Start`, `Graceful Stop`, `Force Stop (SIGKILL)`, `Delete`) protected by `ConfirmationDialog`.

### 2.3 View 3: Application Lifecycle (`app_screen.dart`)
- **Cgroups v2 Isolation Banner**: Explains pre-execution synchronization pipe and direct `execve()` launch mechanism.
- **Three Dedicated Tabs**:
  1. *Registered Applications*: Catalog of pre-configured applications with category filters, profile assignments, launch counters, and launch triggers.
  2. *Active In-Guest Instances*: Real-time list of running processes showing assigned cgroup path (`/sys/fs/cgroup/vos_apps/...`), process start time, PID verification, and graceful termination controls.
  3. *In-Guest Binary Discovery*: RPC-driven scanner detecting available executables inside the guest (`/bin`, `/usr/bin`, `/sbin`), with one-click registration into SQLite.
- **Launch Modal**: Custom launch dialog allowing arguments specification and temporary resource profile overrides before launching.

### 2.4 View 4: Resource Profiles & Cgroups (`profile_screen.dart`)
- **Cgroups v2 Controller Notice**: Details verified kernel limits for `cpu.max`, `cpu.weight`, `memory.max`, `pids.max`, and `io.max`.
- **Preset Profile Catalog**: Default presets for High Performance, Balanced, Background Task, and Strict Sandboxing.
- **Interactive Profile Editor**: Live modal editor with precision sliders for:
  - CPU Cores (`0.25` to `8.0` cores with `100000` period).
  - CPU Weight (`1` to `10000` proportional weight).
  - Memory Max (`32 MB` to `8192 MB` OOM threshold).
  - Process Limits (`4` to `1024` max PIDs).
  - Disk I/O Limits (`1 MB/s` to `100 MB/s` read/write limits).
- **GPU Notice**: Explicit visual confirmation that GPU acceleration is unsupported on the guest platform, adhering to audit requirements.

### 2.5 View 5: Live Monitoring Observatory (`monitoring_screen.dart`)
- **Time Window Selector**: Toggle between 10s, 30s, and 60s observation windows.
- **Dual Live Comparison Charts**:
  - *CPU Telemetry*: Host CPU utilization vs QEMU process host CPU footprint.
  - *Memory Telemetry*: Host RAM pressure vs QEMU process RSS.
- **Four-Layer Architectural Breakdown**:
  1. *Host Native Layer*: Host CPU load, physical RAM, Mach kernel scheduling.
  2. *Hypervisor Layer*: QEMU HVF process PID, RSS footprint, virtual timer.
  3. *Guest Operating System Layer*: Guest kernel 6.6.31, `/proc/stat`, `/proc/meminfo`.
  4. *Application Cgroups Layer*: In-guest cgroups v2 controllers, throttled cycles, OOM events.

### 2.6 View 6: OS Algorithm Simulators (`simulator_screen.dart`)
- **Educational Distinction Banner**: Clearly designates that algorithm simulators are educational conceptual engines separate from the live Linux kernel scheduling.
- **Three Simulation Labs**:
  1. *CPU Scheduling*: Supports FCFS, SJF, SRTF, Priority, Round Robin (with configurable time quantum), and AUTO optimal selector. Features interactive process table and animated Gantt chart with waiting/turnaround metrics.
  2. *Memory Allocation*: Supports First Fit, Best Fit, Worst Fit, and AUTO selector. Visual partition memory map displaying assigned blocks, free holes, internal fragmentation, and allocation status.
  3. *Deadlock Avoidance*: Complete Banker's Algorithm simulator with Allocation, Max, and Available resource vectors. Computes safe state and displays safe execution sequence step-by-step.

### 2.7 View 7: Audit & Event Journal (`events_screen.dart`)
- **Summary Metrics**: Real-time counter of total events, VM state transitions, application launches, and limit violations.
- **Audit Table & Filter Toolbar**: Search events by string, filter by VM ID, or filter by event category (`VM_LIFECYCLE`, `APP_LAUNCH`, `ENFORCEMENT`, `SECURITY`).
- **Audit Detail Modal**: Inspects raw JSON event payload, timestamp, and actor metadata stored in SQLite.

### 2.8 View 8: Settings & System Diagnostics (`settings_screen.dart`)
- **Appearance Settings**: One-click toggle between Dark Theme, Light Theme, or System Default with live palette swatches.
- **C11 Backend Diagnostics**: Displays native library path, initialization state, API version (`phase6`), and dynamic symbol count.
- **Virtualization Infrastructure**: QEMU binary path (`/opt/homebrew/bin/qemu-system-aarch64`), hypervisor backend (`HVF`), and EDK2 firmware path.
- **Database & Storage**: SQLite file path, WAL journal status, foreign key enforcement verification.
- **Host System Diagnostics**: Apple Silicon M5 model identifier, physical cores, logical cores, and memory capacity.

---

## 3. Dynamic Library & Packaging Integration

### 3.1 Direct Framework Staging
To guarantee zero symbol lookup errors (`dlsym(RTLD_DEFAULT, vos_api_init): symbol not found`), the system utilizes a three-tier resolution strategy:
1. **Xcode Build Phase Embedding**: `Runner.xcodeproj` shell script embeds `libvos_backend.dylib` into `$TARGET_BUILD_DIR/$PRODUCT_NAME.app/Contents/Frameworks/` on every build.
2. **Entitlements Isolation Removal**: `com.apple.security.app-sandbox` is set to `<false/>` in `DebugProfile.entitlements` and `Release.entitlements` to allow direct access to C dylibs, QEMU processes, and Unix sockets.
3. **Multi-Path FFI Discovery**: `VosFfiBridge` checks the `.app` bundle `Frameworks` path first, falling back to executable-relative, absolute workspace, and system library paths.

