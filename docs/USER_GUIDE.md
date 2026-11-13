# Virtual OS Simulator: User Guide & Desktop Manual

Welcome to the **Virtual OS Simulator** desktop environment. This application pairs a pure C11 operating system engine with a modern, high-performance Flutter desktop interface to manage QEMU virtual machines, run containerized applications with Linux cgroups v2 resource limits, monitor multi-layer telemetry, and simulate fundamental operating system algorithms.

---

## 1. Interface Overview & Navigation

The desktop application layout consists of:
* **Left Navigation Sidebar**: Persistent navigation across all 7 views, active VM dropdown selector, engine connectivity status indicator, and a one-click "Refresh All" button.
* **Top Status Banner**: Displays global alerts or error notifications with dismiss capability.
* **Main Content Area**: Active screen view rendered with smooth responsiveness.

### Status Indicators
* **Engine Online**: Green indicator confirming active C ABI communication with `libvos_backend.dylib`.
* **Connecting / Bridge Error**: Yellow/Red indicator with detailed error diagnostics.

---

## 2. Screens and Workflows

### View 1: System Overview Dashboard
The dashboard provides a high-level operational snapshot:
* **Host Resource Sparklines**: Real-time 60-sample historical charts of host CPU utilization (%) and physical memory usage.
* **Active VM Quick Stats**: Displays the selected VM's hypervisor CPU consumption, macOS host RSS, guest readiness, and telemetry freshness.
* **Virtual Machine Roster**: Summary table of all registered VMs with state badges (`RUNNING`, `STOPPED`, `FAILED`) and action buttons.
* **Architecture Integrity Banner**: Documents the strict separation between the Apple Silicon host, QEMU emulator process, and Alpine Linux guest kernel.

---

### View 2: Virtual Machine Management
Manage virtual machines running under QEMU HVF acceleration:
* **Creating a New VM**:
  1. Click **Create New VM**.
  2. Enter unique VM ID (e.g., `alpine-01`) and display Name.
  3. Select vCPU count (1–8 cores) and guest RAM (256–8192 MB).
  4. Specify the disk image path (`vm_images/alpine.qcow2`) and ISO path (`vm_images/alpine-arm64.iso`).
  5. Click **Create VM**.
* **Starting a VM**: Click the **Start** button on any stopped VM card. The engine launches QEMU in the background, attaches virtio-serial, and starts monitoring.
* **Stopping a VM**:
  * **Graceful Stop**: Sends `SIGTERM` to the QEMU process, allowing orderly guest filesystem flush.
  * **Force Stop**: Sends `SIGKILL` for emergency recovery if the guest hangs.
* **Deleting a VM**: Removes VM configuration from the SQLite database (only allowed when stopped).

---

### View 3: Application Management
Deploy and manage guest applications with verified cgroups v2 resource isolation:
* **In-Guest Application Discovery**:
  1. Select a running VM.
  2. Click **Discover Guest Apps**.
  3. The guest agent scans `/bin`, `/usr/bin`, and `/usr/sbin` and presents discovered binaries.
  4. Click **Quick Register** to prefill the registration form.
* **Registering an Application**:
  1. Click **Register Application**.
  2. Provide Application ID (e.g., `app-nginx`), Name, and guest Executable Path (e.g., `/usr/sbin/nginx` or `/bin/sleep`).
  3. Assign a default **Resource Profile** (e.g., `default`, `restricted`).
* **Launching with Verified Cgroup Handshake**:
  1. Click **Launch** next to any registered application.
  2. (Optional) Provide command-line arguments and select a Profile Override.
  3. Click **Launch in Cgroup**.
  4. *Security Guarantee*: The host backend executes a two-phase synchronization handshake with the guest agent. The target cgroup limits and PID are attached *before* `execve()` executes. Shell wrappers are forbidden.
* **Stopping an Application**: Click **Stop** on an active instance. The agent validates PID start time to prevent PID reuse attacks and captures the true exit code or signal.

---

### View 4: Resource Profiles
Configure fine-grained resource contracts enforced via Linux cgroups v2:
* **Catalog Profiles**:
  * `default`: 1.0 vCPU, 256 MB RAM, max 100 PIDs.
  * `high_performance`: 2.0 vCPUs, 512 MB RAM, max 200 PIDs.
  * `background_worker`: 0.5 vCPU, 128 MB RAM, max 50 PIDs.
  * `restricted`: 0.25 vCPU, 64 MB RAM, max 20 PIDs.
* **Configurable Parameters**:
  * **CPU Cores (`cpu.max`)**: Quota per 100ms period (e.g., 1.5 cores = 150,000 µs / 100,000 µs).
  * **CPU Weight (`cpu.weight`)**: Proportional CFS weight (1–10,000).
  * **Memory Max (`memory.max`)**: Hard memory ceiling; triggers Linux kernel OOM killer upon exhaustion.
  * **Process Limit (`pids.max`)**: Maximum allowable fork/clone count to prevent fork bombs.
  * **Block I/O Throttling (`io.max`)**: Maximum read/write bytes per second for guest block devices.
  * **GPU Policy**: Apple Silicon host + headless Linux guest does not provide virtualized GPU pass-through; GPU limits are permanently badged as `UNSUPPORTED`.

---

### View 5: Live Telemetry & Monitoring
The live monitoring view cleanly segregates telemetry into 4 independent layers:
1. **Layer 1: Host Native** (Mach kernel & sysctl): Host CPU %, Total Physical RAM, Load Averages.
2. **Layer 2: QEMU Hypervisor Process** (macOS `task_info`): Hypervisor PID, Host RSS memory consumption, Hypervisor CPU load.
3. **Layer 3: Guest Operating System** (Linux `/proc/stat` & `/proc/meminfo` via virtio-serial): In-guest CPU utilization, per-core breakdown, guest free/cached RAM.
4. **Layer 4: Application Cgroup** (Linux cgroups v2): Per-application CPU usage, resident memory, and process count.
* **Integrity Guard**: Detects anomalies automatically. For example, if Guest RAM exceeds QEMU Host RSS, an integrity warning is triggered.

---

### View 6: OS Algorithm Simulators
> **Important**: This section contains theoretical educational algorithms running in an isolated C simulation sandbox. These algorithms are completely separate from the real Linux kernel scheduler and QEMU hypervisor.

#### 1. CPU Scheduling Tab
* **Algorithms Available**: FCFS, SJF (Non-preemptive), SRTF (Preemptive), Priority (Non-preemptive & Preemptive), Round Robin (configurable time quantum), and **AUTO** (automatically evaluates all algorithms and recommends the one minimizing turnaround time).
* **Workflow**:
  1. Edit or add processes in the process table (Arrival Time, Burst Time, Priority).
  2. Choose an algorithm (or AUTO).
  3. If Round Robin, set the Time Quantum.
  4. Click **Run CPU Simulation**.
  5. Inspect the interactive **Gantt Chart**, average waiting/turnaround times, context switches, and individual completion times.

#### 2. Memory Management Tab
* **Strategies Available**: First Fit, Best Fit, Worst Fit, and **AUTO** (minimizes internal fragmentation).
* **Workflow**:
  1. Specify memory block partitions in KB (comma-separated, e.g. `100, 500, 200, 300, 600`).
  2. Configure process memory requests in the job table.
  3. Click **Run Memory Simulation**.
  4. Inspect the **Visual Partition Map** showing allocated blocks, internal fragmentation, and unallocated/failed requests.

#### 3. Banker's Algorithm (Deadlock Avoidance) Tab
* **Workflow**:
  1. Input the Available Resource Vector (e.g. `3, 3, 2`).
  2. Configure the Allocation and Maximum Demand matrices for processes P0–P4.
  3. Click **Evaluate Banker's Safety**.
  4. The engine determines whether the system is in a **SAFE** or **UNSAFE** state.
  5. If safe, the safe execution sequence is displayed (e.g., `P1 ➔ P3 ➔ P4 ➔ P0 ➔ P2`).

---

### View 7: Audit & Event History
Inspect an immutable timeline of system events stored in SQLite WAL mode:
* **Filter by VM**: Focus on events for a specific virtual machine.
* **Filter by Event Type**: Filter by `LAUNCH`, `STOP`, `CGROUP`, or `RECONCILE`.
* **Search Query**: Instant text search matching instance IDs, application IDs, and event descriptions.
* **KPI Metrics**: Counters showing total audit records, launches, terminations, and cgroup enforcement events.

---

## 3. Troubleshooting & FAQ

* **Q: The sidebar shows "Bridge Error: Failed to load libvos_backend.dylib".**  
  *A: Run `make dylib` in the repository root to build the dynamic shared library before starting the Flutter app.*

* **Q: Starting a VM fails with "QEMU executable not found".**  
  *A: Ensure QEMU is installed via Homebrew (`brew install qemu`) and that `/opt/homebrew/bin` is in your `PATH`.*

* **Q: An application launch fails with "cgroup attachment timeout".**  
  *A: Ensure the guest VM is fully booted (`guest_ready == 1`) and the guest agent is running inside Alpine Linux.*

* **Q: Why does the CPU scheduling simulation look different from live guest CPU?**  
  *A: The OS Simulator tab is an educational tool executing theoretical C models (FCFS, SJF, RR) and is strictly isolated from the real Linux CFS/cgroups scheduler.*

