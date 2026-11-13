# VEYLORA UI Implementation Architecture

**Product Category:** Virtual Computing Environment  
**Product Name:** VEYLORA  
**Engine:** C11 Mach & Hypervisor Kernel Gateway (`libvos_backend.dylib`) + SQLite WAL + Flutter Desktop  

---

## 1. Overview of Changes

The transformation from Virtual OS Simulator into **VEYLORA** involved a ground-up UI/UX redesign of every screen and shared component while preserving 100% of the underlying C11 virtualization engine, QEMU ARM64 HVF hypervisor, Linux cgroups v2 integration, and SQLite persistence.

---

## 2. File-by-File Redesign Breakdown

### 2.1 Theme & Design Tokens (`frontend/lib/core/theme.dart`)
- **Implemented:**
  - `VosColors`: Added fine-grained Dark (`#090B10` background, `#131722` cards, `#1F2738` borders) and Light palettes.
  - `VeyloraSpacing`: Consistent 8-point typographic and layout grid (`xxs: 2` to `xxxl: 32`).
  - `VeyloraRadius`: Modern corner radii tokens (`xs: 4` to `full: 999`).
  - `VeyloraMotion`: Smooth cubic curves and desktop duration tokens (`140ms`, `240ms`, `360ms`).
  - `VeyloraTypography`: Added tabular figures (`tnum`) to eliminate layout jumpiness during real-time telemetry updates.

### 2.2 Branding & Constants (`frontend/lib/core/constants.dart`)
- **Updated:**
  - `appTitle`: `Veylora`
  - `appTagline`: `Beyond Virtual. Beyond Limits.`
  - `appCategory`: `Virtual Computing Environment`
  - `appVersion`: `v1.6.0 Pro`
  - Added asset paths for `assets/images/veylora_logo.jpg` and `assets/images/splash_bg.jpg`.

### 2.3 Navigation Sidebar (`frontend/lib/widgets/navigation_sidebar.dart`)
- **Restructured into 5 Requested Sections:**
  1. `OVERVIEW`: Dashboard (index `0`)
  2. `MANAGEMENT`: Virtual Machines (`1`), Applications (`2`), Resource Management (`3`)
  3. `OBSERVABILITY`: Monitoring (`4`), Event Logs (`6`)
  4. `TOOLS`: OS Algorithms (`5`)
  5. `PREFERENCES`: Settings (`7`)
- **Visuals:** Added live running VM & app count chips, brand logo emblem with subtle glow, live Mach engine indicator, collapsible drawer toggle, and host model hardware footer (`Mac17,3 · Apple Silicon · QEMU HVF`).

### 2.4 Top Bar Shell (`frontend/lib/widgets/app_top_bar.dart` & `frontend/lib/main.dart`)
- Dynamic breadcrumb path display (`Veylora > [SECTION] > [PAGE]`).
- Mach Core connectivity status pill (`LIVE` with green pulsing dot).
- Active VM quick switcher with status badge.
- Guest Agent Virtio-serial connectivity pill (`AGENT READY` / `STANDBY`).
- Quick actions: Replay Boot Console / Splash, Theme toggle (Dark / Light), and Refresh Telemetry.

### 2.5 Boot Splash Screen (`frontend/lib/presentation/splash/veylora_splash_screen.dart`)
- Adobe-style cinematic loading screen utilizing `splash_bg.jpg` with dark overlay.
- High-resolution metallic Veylora brand logo emblem.
- Dynamic boot console text generator simulating C11 kernel initialization, Mach host statistics attachment, QEMU HVF detection, Virtio chardev binding, and SQLite WAL database verification.
- Skip shortcut (`Esc` or `Space`) and smooth crossfade transition into the main workspace.

### 2.6 Dashboard Screen (`frontend/lib/presentation/dashboard/dashboard_screen.dart`)
- **Top Welcome Hero:** Host processor architecture, macOS Darwin kernel version, load average pills, and system online badges.
- **4 Primary Metric Cards:** Host CPU, Host Memory, Running VMs, Running Applications with real-time sparklines.
- **Two-Column Data Grid:**
  - Left: Real-time Host CPU utilization chart (100% fixed max) and Physical RAM allocation chart.
  - Right: Virtual Machine Status Overview Card (active VM specs, guest agent state) and Resource Enforcement Status Card (cgroups v2 controller health, GPU unsupported badge).
- **Lower Workspace:** Running Application Workloads table and Recent Audit Events Stream (from SQLite `application_events`).

### 2.7 Virtual Machines Screen (`frontend/lib/presentation/vm/vm_screen.dart` & `widgets/vm_card.dart`)
- QEMU 11.1.2 & EDK2 UEFI firmware notice banner.
- Proxmox-style VM cards with vCPU, Memory, State badges, and Mac task info.
- Search and state filter bar (`ALL`, `RUNNING`, `STOPPED`).
- Quick action menu (Start, Stop, Force Kill, Terminal/Console, Delete).
- Integrated Alpine Linux VM Creation Wizard.

### 2.8 Applications Screen (`frontend/lib/presentation/app/app_screen.dart`)
- Verified Linux cgroups v2 pre-execution synchronization handshake banner.
- 3 Tabs: Registered Applications, Active Workload Instances, In-Guest Discovery.
- Add Application from Finder / Device directory with automatic macOS `.app` bundle executable resolution.
- Dual Benchmark Engine: Evaluates applications under Minimum Requirements (1 vCPU, 256MB) vs High-End Requirements (4 vCPUs, 1024MB).
- Dual Report Generation:
  1. Technical Report (CFS periods, OOM killer thresholds, IOPS, fork limits).
  2. Non-Tech Plain English Report (comprehensible summary for everyday users).
- Historical Baseline Comparison: Automatically retrieves previous runs from SQLite DBMS, flags performance regressions, and creates an isolated `issues` report.
- Live Hands-On Testing HUD for real-world creative software testing (e.g., Lightroom RAW photo editing) with active session timer and metric tracking.

### 2.9 Resource Management Screen (`frontend/lib/presentation/profile/profile_screen.dart`)
- Cloud resource governance dashboard.
- Explicit `VGPU: UNSUPPORTED (CPU FALLBACK)` badge explaining Apple Silicon headless guest behavior.
- Preset Profiles Catalog (Lightweight, Balanced, High Performance, Custom) with verified kernel constraints.
- Interactive Resource Control Console with 4 fine-grained tuning panels:
  - **CPU Control:** `cpu.max` quota in microseconds / 100,000µs CFS period, `cpu.weight` CFS scheduler priority.
  - **Memory Control:** `memory.max` hard ceiling in MB, swap limits, and OOM killer events.
  - **Process Control:** `pids.max` task limits for fork-bomb mitigation.
  - **Disk I/O Control:** `io.max` read & write bandwidth throttling.
- Live kernel readback verification bar with SQLite persistence.

### 2.10 Observability & Monitoring Screen (`frontend/lib/presentation/monitoring/monitoring_screen.dart`)
- Redesigned into 3 clean tabs:
  1. `Host System`: Mach kernel host statistics and Darwin memory breakdown.
  2. `Virtual Machines`: QEMU hypervisor task info, Host RSS, and guest virtio state.
  3. `Applications`: Linux cgroups v2 controller enforcement and running instance metrics.
- Rolling time-window selector (`10s`, `30s`, `60s`).
- Live 1Hz cubic spline charts with peak and average callout pills.
- Explicit metric source indicators (`MACH_HOST_STATISTICS`, `TASK_BASIC_INFO_64`, `GUEST_RPC`, `CGROUPS_V2_CONTROLLER`).

### 2.11 OS Algorithms Screen (`frontend/lib/presentation/simulator/simulator_screen.dart`)
- Pure C simulation sandbox segregated from live hypervisor scheduling.
- 3 Tabs:
  1. `CPU Scheduling`: FCFS, SJF, SRTF, Priority, and Round Robin with Gantt chart visualization and waiting time metrics.
  2. `Memory Allocation`: First Fit, Best Fit, and Worst Fit with dynamic memory partition block maps.
  3. `Banker's Algorithm`: Multi-resource deadlock detection and safe execution sequence calculation.

### 2.12 Event Logs Screen (`frontend/lib/presentation/events/events_screen.dart`)
- Immutable audit log backed by SQLite WAL mode.
- 5 Top statistic summary cards (Total records, Benchmarks, Launches, Stops, Enforcements).
- Filter bar by VM, search keyword, and event category (`ALL`, `TEST`, `TEST_ISSUE`, `TEST_LIVE`, `LAUNCH`, `STOP`, `CGROUP`).
- Modal audit inspector with direct one-click access to generated `.txt` benchmark reports, copy-to-clipboard actions, and monospace JSON payload viewer.

### 2.13 Settings & Diagnostics Screen (`frontend/lib/presentation/settings/settings_screen.dart`)
- Veylora brand showcase with official metallic logo emblem.
- Theme selector with immediate dark/light mode toggle.
- C11 Backend dynamic library diagnostics (`libvos_backend.dylib`, Apple Silicon ARM64 `-fPIC`).
- Hypervisor and EDK2 UEFI firmware path verification.
- SQLite WAL mode database statistics and active table list.

---

## 3. Preserved Backend Integration

All frontend changes bind directly to the existing C11 dynamic library (`libvos_backend.dylib`) and SQLite database:
- `vos_api_init()`, `vos_api_shutdown()`
- `vos_api_get_host_telemetry()`
- `vos_api_vm_create()`, `vos_api_vm_list()`, `vos_api_vm_start()`, `vos_api_vm_stop()`
- `vos_api_profile_save()`, `vos_api_profile_list()`
- `vos_api_app_register()`, `vos_api_app_launch()`, `vos_api_app_stop()`
- `vos_api_sched_simulate()`, `vos_api_mem_simulate()`, `vos_api_bankers_simulate()`
- `vos_api_event_log()`, `vos_api_events_list()`

