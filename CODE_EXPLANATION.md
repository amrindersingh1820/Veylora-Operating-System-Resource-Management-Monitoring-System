# Virtual OS Simulator — Comprehensive Code & Architecture Guide

This document provides an in-depth explanation of the codebase, system architecture, core operating system algorithms, data structures, database persistence, and cross-platform graphical and terminal interfaces.

---

## 1. System Architecture Overview

The **Virtual OS Simulator** is organized into four modular layers with clear separation of concerns:

```
+-----------------------------------------------------------------------------------+
|                               USER INTERFACES                                     |
|  +---------------------------+  +--------------------------+  +----------------+  |
|  | Cross-Platform GUI (SDL2) |  | Interactive CLI / TUI    |  | Win32 GUI      |  |
|  | (macOS, Linux, Windows)   |  | (Terminal, ANSI colors)  |  | (Windows only) |  |
|  | src/gui_sdl/              |  | src/cli/                 |  | src/gui/       |  |
|  +---------------------------+  +--------------------------+  +----------------+  |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                       CORE SIMULATION ENGINE (Pure C11)                           |
|  +-------------------+  +-------------------+  +-------------------------------+  |
|  | CPU Scheduler     |  | Memory Allocator  |  | Resource & Deadlock (Banker's)|  |
|  | (src/core/sched*) |  | (src/core/memory*)|  | (src/core/resources*)         |  |
|  +-------------------+  +-------------------+  +-------------------------------+  |
|  +-----------------------------------------------------------------------------+  |
|  | Utility & Platform Path Abstractions (src/core/util*)                       |  |
|  +-----------------------------------------------------------------------------+  |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                        DATABASE & PERSISTENCE LAYER                               |
|  +-----------------------------------------------------------------------------+  |
|  | SQLite Storage Engine (src/db/database*)                                    |  |
|  | Embedded SQLite Amalgamation (third_party/sqlite3.c)                        |  |
|  +-----------------------------------------------------------------------------+  |
+-----------------------------------------------------------------------------------+
```

---

## 2. Directory & File Structure

| Directory / File | Role & Contents |
|---|---|
| `src/main.c` | Universal application entry point. Routes to SDL2 GUI, interactive CLI, or Win32 GUI based on platform and CLI flags. |
| `src/core/types.h` | Shared C structs and enums (`Process`, `SchedResult`, `MemResult`, `ResResult`, `RunBundle`). |
| `src/core/scheduler.c/.h` | CPU scheduling algorithms: FCFS, SJF, SRTF, Priority (Non-Preemptive & Preemptive), Round Robin, and the Auto-Selection Heuristic. |
| `src/core/memory.c/.h` | Contiguous memory allocation strategies: First Fit, Best Fit, Worst Fit, and Auto Selector. |
| `src/core/resources.c/.h` | System resource matrix calculations and Dijkstra's Banker's Algorithm for deadlock avoidance. |
| `src/core/util.c/.h` | String integer parsing and cross-platform executable directory discovery for database path resolution. |
| `src/db/database.c/.h` | SQLite schema creation, CRUD operations for processes, simulation runs, Gantt timelines, memory maps, and settings. |
| `src/gui_sdl/gui_sdl.c/.h` | Cross-platform hardware-accelerated GUI built with Nuklear and SDL2. Features high-DPI crisp TTF font scaling, OS boot sequence, interactive Gantt charts, memory maps, tabs, dialogs, and dark/light modes. |
| `src/cli/cli.c/.h` | Interactive terminal user interface with ANSI colored Gantt charts, tables, process management, and live comparisons. |
| `src/gui/*` | Legacy native Win32 desktop interface using Windows API and GDI (used when building on Windows without SDL2). |
| `third_party/sqlite3.*` | Public domain SQLite C amalgamation library (compiled once into `build/sqlite3.o`). |
| `tests/test_core.c` | Automated unit test suite with 256 assertions validating scheduling math, memory allocations, Banker's algorithm, and database logic. |
| `Makefile` | Multi-platform Makefile supporting macOS, Linux, and Windows with target auto-detection. |
| `CMakeLists.txt` | Standard CMake configuration for IDEs (VS Code, CLion, Xcode, Visual Studio). |
| `build.sh` | One-click build script for macOS and Linux. |
| `build.bat` | One-click build script for Windows (MinGW-w64 / MSYS2). |

---

## 3. Core Data Structures (`src/core/types.h`)

All data structures in the core engine are standard C types with no platform dependencies:

### 1. `Process`
Represents a process definition:
```c
typedef struct {
    int  pid;                  /* Database unique identifier */
    char name[NAME_LEN];       /* Display label, e.g. "P1" */
    int  arrival;              /* Arrival time (>= 0) */
    int  burst;                /* Total CPU execution time needed (> 0) */
    int  priority;             /* Priority level (1 = highest priority) */
    int  mem_kb;               /* Memory footprint in KB */
    int  max[NRES];            /* Max units of each resource it may request */
    int  held[NRES];           /* Units of each resource currently allocated */
} Process;
```
There are `NRES = 4` system resource types: **Printer**, **Scanner**, **Disk**, and **Network**.

### 2. `ProcResult` & `GanttSeg`
Store individual execution results and timeline slices:
```c
typedef struct {
    Process p;
    int completion;            /* Time at which process finished */
    int tat;                   /* Turnaround Time = Completion - Arrival */
    int wt;                    /* Waiting Time = TAT - Burst */
    int rt;                    /* Response Time = First CPU Time - Arrival */
} ProcResult;

typedef struct {
    int pid;                   /* Process ID (-1 indicates CPU was idle) */
    int start;                 /* Segment start time */
    int end;                   /* Segment end time */
} GanttSeg;
```

### 3. `SchedResult`
Aggregated CPU scheduling performance metrics:
- `avg_wt`: Average Waiting Time across all executed processes.
- `avg_tat`: Average Turnaround Time.
- `avg_rt`: Average Response Time.
- `cpu_util`: Percentage of total time CPU was actively executing processes.
- `throughput`: Completed processes per unit of time ($N / \text{total\_time}$).
- `ctx_switches`: Number of times the CPU switched from one process to another.
- `g[MAX_GANTT]`: Array of consecutive Gantt slices spanning from time $0$ to `total_time`.

### 4. `MemAlloc` & `MemResult`
Tracks contiguous block placements:
- `block`: 1-based index of allocated block (0 if placement failed).
- `block_size`: Size of the partition.
- `frag`: Internal fragmentation ($\text{block\_size} - \text{requested\_size}$).
- `failed`: Number of processes that could not be placed due to insufficient block capacity.

### 5. `ResResult`
Deadlock assessment using Dijkstra's Banker's Algorithm:
- `safe`: `1` if the system is in a safe state; `0` if in an unsafe state.
- `seq[MAX_PROC]`: Safe execution sequence indices (e.g., $P_2 \to P_4 \to P_1 \to P_3 \to P_5$).
- `need[MAX_PROC][NRES]`: Remaining resource need ($Max - Held$).

---

## 4. CPU Scheduling Module (`src/core/scheduler.c`)

### Mathematical Formulas
For each process $i$:
1. **Completion Time ($C_i$)**: The clock tick at which the process finishes execution.
2. **Turnaround Time ($TAT_i$)**: Total time spent in the system:
   $$TAT_i = C_i - A_i$$
3. **Waiting Time ($WT_i$)**: Total time spent waiting in the ready queue:
   $$WT_i = TAT_i - B_i$$
4. **Response Time ($RT_i$)**: Time from arrival until the process first gets the CPU:
   $$RT_i = \text{FirstStartTime}_i - A_i$$
5. **CPU Utilization**:
   $$\text{CPU Utilization} = \left(\frac{\text{Busy Time}}{\text{Total Time}}\right) \times 100\%$$
6. **Throughput**:
   $$\text{Throughput} = \frac{N}{\text{Total Time}}$$

---

### Scheduling Algorithms Explained

#### 1. First-Come, First-Served (FCFS)
- **Strategy**: Non-preemptive.
- **Logic**: Sorts arriving processes strictly by arrival time. The CPU runs the first arriving job to completion. If no process is available, the CPU logs an **Idle** segment until the next arrival.

#### 2. Shortest Job First (SJF)
- **Strategy**: Non-preemptive.
- **Logic**: At any point when the CPU is free, it scans all processes that have arrived ($A_i \le \text{current\_time}$) and picks the one with the smallest burst time $B_i$. Ties are broken by earlier arrival time.

#### 3. Shortest Remaining Time First (SRTF)
- **Strategy**: Preemptive counterpart to SJF.
- **Logic**: Simulates time step-by-step. At each time tick, any newly arrived process with a remaining burst shorter than the currently running process causes an immediate preemption. The preempted process returns to the ready state.

#### 4. Priority Scheduling (Non-Preemptive)
- **Strategy**: Non-preemptive.
- **Logic**: Lower numerical value denotes higher priority ($1 = \text{highest}$). Among all arrived processes, selects the one with the lowest priority value. Runs each to full completion before choosing the next.

#### 5. Priority Scheduling (Preemptive)
- **Strategy**: Preemptive.
- **Logic**: At every time tick, checks if a newly arrived process has a strictly higher priority (lower numerical value) than the currently running job. If so, preempts the current job, saves its remaining burst, and schedules the higher-priority job.

#### 6. Round Robin (RR)
- **Strategy**: Preemptive with fixed time quantum $q$.
- **Logic**: Maintains a FIFO ready queue. The running process is allocated the CPU for up to $q$ time units. If it completes before $q$, it terminates. If its quantum expires while burst remains, it is preempted, any newly arriving processes are appended to the queue, and the preempted job is placed at the tail.

---

### The Auto-Selection Heuristic (`sched_auto_select`)

When the user selects **Auto**, the simulator analyzes the process workload and selects the optimal algorithm based on operating system principles:

```
                      Workload Analysis
                              │
                    ┌─────────┴─────────┐
             N == 1 ?                   │
               YES ──> FCFS             │
                                        │
           Distinct Priorities >= 3 ?   │
               YES ──> Different Arrivals ? ──> YES: Preemptive Priority
                              │                 NO:  Non-Preemptive Priority
                              │
          Max Burst >= 3x Min Burst ?
               YES ──> Staggered Arrivals ? ──> YES: SRTF
                              │                 NO:  SJF
                              │
         N >= 5 and Similar Bursts ?
               YES ──> Round Robin (Quantum = avg_burst / 2)
                              │
                              └──> Otherwise: FCFS
```

1. **Rule 1: Single Process**: If only 1 process exists, no scheduling contention exists $\to$ **FCFS**.
2. **Rule 2: High Priority Differentiation**: If 3 or more distinct priority levels exist, priorities are critical:
   - If arrivals are staggered $\to$ **Priority (Preemptive)**.
   - If all arrive simultaneously $\to$ **Priority (Non-Preemptive)**.
3. **Rule 3: High Burst Variance**: If the longest burst is $\ge 3\times$ the shortest burst, shortest-job-first policies minimize average waiting time:
   - If arrivals are staggered $\to$ **SRTF**.
   - If all arrive at once $\to$ **SJF**.
4. **Rule 4: Time-Sharing Candidate**: If 5 or more processes have similar burst durations, fair CPU sharing is preferred $\to$ **Round Robin** with $q \approx \text{avg\_burst} / 2$.
5. **Rule 5: Default Fallback**: If none of the conditions trigger $\to$ **FCFS**.

---

## 5. Memory Allocation Module (`src/core/memory.c`)

Simulates contiguous memory allocation in fixed partitions.

### Strategies
- **First Fit (`MEM_FIRST`)**: Scans blocks from index $1$ to $M$ and assigns the process to the first free block large enough ($\text{block\_size} \ge \text{req\_size}$).
- **Best Fit (`MEM_BEST`)**: Searches all free blocks and selects the smallest block capable of fitting the process, minimizing internal fragmentation.
- **Worst Fit (`MEM_WORST`)**: Selects the largest free block, leaving the largest remaining unused partition.
- **Auto Strategy (`MEM_AUTO`)**: Runs First, Best, and Worst fit behind the scenes. It evaluates them by:
  1. Maximizing the number of placed processes (lowest failure count).
  2. Minimizing total internal fragmentation.

### Internal Fragmentation
When a process of size $S$ is placed in a block of size $B$ ($B \ge S$), the unused space inside the partition is **internal fragmentation**:
$$\text{Frag} = B - S$$
Unallocated blocks remain labeled **FREE**.

---

## 6. Resources & Banker's Algorithm (`src/core/resources.c`)

Implements **Dijkstra's Banker's Algorithm** for deadlock avoidance with multiple resource types ($m = 4$):

### Vectors & Matrices
- **$\text{Available}[j]$**: Unallocated units of resource $j$.
  $$\text{Available}[j] = \text{Total}[j] - \sum_{i=1}^n \text{Held}[i][j]$$
- **$\text{Max}[i][j]$**: Maximum units of resource $j$ that process $i$ may request.
- **$\text{Held}[i][j]$**: Units of resource $j$ currently held by process $i$.
- **$\text{Need}[i][j]$**: Potential future request:
  $$\text{Need}[i][j] = \text{Max}[i][j] - \text{Held}[i][j]$$

### Safety Algorithm Walkthrough
1. Initialize $\text{Work}[j] = \text{Available}[j]$ for all resources $j = 0 \dots 3$.
2. Initialize $\text{Finish}[i] = \text{false}$ for all processes $i = 0 \dots n-1$.
3. Find an index $i$ such that:
   $$\text{Finish}[i] == \text{false} \quad \text{and} \quad \forall j, \, \text{Need}[i][j] \le \text{Work}[j]$$
4. If such an $i$ exists:
   - Assume process $i$ completes and releases all held resources:
     $$\text{Work}[j] = \text{Work}[j] + \text{Held}[i][j]$$
   - Set $\text{Finish}[i] = \text{true}$ and append $i$ to the safe sequence.
   - Repeat from Step 3.
5. If $\text{Finish}[i] == \text{true}$ for all $i$, the system is in a **SAFE STATE**, and the accumulated sequence is the **Safe Execution Sequence**.
6. If any process cannot finish, the system is in an **UNSAFE STATE** (susceptible to deadlock). The module identifies the exact processes causing the deadlock.

---

## 7. Database Layer (`src/db/database.c`)

Uses embedded SQLite (via `third_party/sqlite3.c`). The database file `virtual_os.db` is automatically created next to the application on startup.

### Relational Schema
```sql
CREATE TABLE IF NOT EXISTS processes (
    pid INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    arrival INTEGER, burst INTEGER, priority INTEGER, mem_kb INTEGER,
    max_p INTEGER, max_s INTEGER, max_d INTEGER, max_n INTEGER,
    held_p INTEGER, held_s INTEGER, held_d INTEGER, held_n INTEGER
);

CREATE TABLE IF NOT EXISTS runs (
    run_id INTEGER PRIMARY KEY AUTOINCREMENT,
    created TEXT, algo TEXT, quantum INTEGER, mem_strategy TEXT,
    nproc INTEGER, total_time INTEGER, busy_time INTEGER, ctx_switches INTEGER,
    avg_wt REAL, avg_tat REAL, avg_rt REAL, cpu_util REAL, throughput REAL,
    auto_reason TEXT, mem_blocks TEXT, res_totals TEXT,
    res_safe INTEGER, res_seq TEXT, res_message TEXT
);

CREATE TABLE IF NOT EXISTS run_results (
    run_id INTEGER, pid INTEGER, name TEXT,
    arrival INTEGER, burst INTEGER, priority INTEGER,
    completion INTEGER, tat INTEGER, wt INTEGER, rt INTEGER
);

CREATE TABLE IF NOT EXISTS run_gantt (
    run_id INTEGER, seq INTEGER, pid INTEGER, start INTEGER, end INTEGER
);

CREATE TABLE IF NOT EXISTS run_memory (
    run_id INTEGER, pid INTEGER, name TEXT,
    size INTEGER, block INTEGER, block_size INTEGER, frag INTEGER, allocated INTEGER
);

CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY, val TEXT
);
```

### Persistence Features
- **Process Catalog**: Added, edited, or deleted processes persist permanently across sessions.
- **Run History**: Complete historical runs (including exact Gantt segments, memory layout, resource matrices, and averages) are saved and can be reloaded at any time.
- **Settings Memory**: Remembers last-used algorithm, time quantum, memory blocks string, and resource totals.

---

## 8. Cross-Platform Interfaces

### 1. Hardware-Accelerated Nuklear GUI (`src/gui_sdl/gui_sdl.c`)
- **Framework**: Powered by [Nuklear Immediate Mode UI](https://github.com/Immediate-Mode-UI/Nuklear.git) + SDL2.
- **Operating System Boot / Loading Screen**:
  - Realistic OS kernel startup sequence displaying ASCII system logo, kernel initialization log with timestamps and `[ OK ]` tags (ACPI, CPU, Memory partitions, SQLite VFS, Device managers, Scheduler, and Banker's algorithm).
  - Glowing animated progress bar from 0% to 100%.
  - Supports interactive skip on any keypress or mouse click.
- **Light Mode & Dark Mode Theming**:
  - **Dark Mode**: Sleek dark slate/charcoal background (`#181c24`), panels (`#222834`), crisp light text (`#f0f4f8`), and vibrant blue accents.
  - **Light Mode**: Clean macOS-inspired light background (`#f1f5f9`), white card panels (`#ffffff`), soft borders (`#cbd5e1`), and dark slate text.
  - Real-time toggle via the `[ Light Mode ]` / `[ Dark Mode ]` button on the top navigation bar.
  - Theme preference is saved to SQLite (`virtual_os.db`) and restored on launch.
- **Responsive Screen & High-DPI Crisp Text Architecture**:
  - **Full High-DPI / Retina Native Rendering**:
    - Created with `SDL_WINDOW_ALLOW_HIGHDPI`, enabling native resolution output on 2x/3x Retina displays and 4K monitors (e.g., 2560x1520 physical backbuffer for 1280x760 logical points).
    - `SDL_RenderSetScale(renderer, dpi_scale, dpi_scale)` maps all Nuklear vector shapes, clipping rectangles, and layout bounds seamlessly to physical display pixels without distortion.
  - **Crisp Subpixel-Oversampled Font Baking**:
    - TrueType fonts are rasterized at `16.0f * dpi_scale` (e.g. 32px on Retina) with 3x horizontal and 3x vertical subpixel oversampling (`cfg.oversample_h = 3, cfg.oversample_v = 3`) and integer pixel snapping (`cfg.pixel_snap = 1`).
    - The logical font handle is normalized by `dpi_scale` (`font->handle.height /= dpi_scale`), ensuring 1:1 logical layout alignment with zero blurring and razor-sharp, crystal-clear glyph edges.
    - Automatic font discovery chain selects modern system typefaces: SF Pro, SFNS, Arial, Segoe UI, DejaVu Sans, or Monaco.
  - **Fluid Dynamic Layout**:
    - Top configuration bar, Process management panel, and Tabbed Results panel dynamically adapt to 100% of the window width and height upon resizing or maximizing.
- **Interactive Visualizations**:
  - **Dynamic Gantt Chart**: Rendered directly on the Nuklear canvas with process-specific colors, idle hatching, timeline tick marks, and interactive hover tooltips showing start, end, and duration.
  - **Visual Memory Map**: Proportional partition blocks displaying allocated process slice, internal fragmentation, and free space.
  - **Banker's Algorithm Card**: Real-time Safe Sequence banner ($P_2 \to P_4 \to P_1 \to P_3 \to P_5$) or deadlock warnings.
  - **Algorithm Compare View**: Side-by-side benchmark table comparing all 6 scheduling algorithms.
  - **Add / Edit Process Modal Dialog**: Interactive form with input validation.

### 2. Interactive Terminal UI (`src/cli/cli.c`)
- **Compatibility**: All systems with ANSI terminal support (macOS Terminal, Linux Bash/Zsh, Windows Terminal / PowerShell, SSH sessions).
- **Zero GUI Dependencies**: Compiles with standard C and SQLite; does not require graphics libraries or X11/Cocoa display servers.
- **Features**:
  - Process manager with checkbox toggles (`[*]`).
  - Terminal-rendered ASCII Gantt chart with time scale coordinates.
  - Side-by-side algorithm comparison matrix.
  - Full access to SQLite history and simulation execution.

### 3. Native Windows Win32 GUI (`src/gui/`)
- Maintained for Windows environments requiring zero runtime dependencies beyond standard Windows DLLs (`comctl32`, `gdi32`, `user32`).

---

## 9. How to Build & Run on Every Platform

### macOS
1. Install SDL2 (via Homebrew):
   ```bash
   brew install sdl2
   ```
2. Build and run:
   ```bash
   ./build.sh --run
   ```
   *Or using make:*
   ```bash
   make
   ./virtual_os         # Launches SDL2 GUI
   ./virtual_os --cli   # Launches Terminal UI
   ```

### Linux (Ubuntu / Debian / Fedora / Arch)
1. Install compiler and SDL2 development headers:
   - **Ubuntu/Debian**:
     ```bash
     sudo apt update && sudo apt install build-essential libsdl2-dev
     ```
   - **Fedora**:
     ```bash
     sudo dnf install gcc make SDL2-devel
     ```
   - **Arch Linux**:
     ```bash
     sudo pacman -S base-devel sdl2
     ```
2. Build and run:
   ```bash
   ./build.sh --run
   ```

### Windows
- **Option A (Native Win32 GUI, Zero Dependencies)**:
  Run `build.bat` in Command Prompt / PowerShell with MinGW-w64:
  ```cmd
  build.bat
  VirtualOS.exe
  ```
- **Option B (Terminal UI)**:
  ```cmd
  build.bat cli
  virtual_os_cli.exe
  ```
- **Option C (Cross-Platform SDL2 GUI via MSYS2 / MinGW)**:
  ```bash
  make sdl
  ./virtual_os_sdl.exe
  ```

### Running Unit Tests
To run all 256 engine and database validation tests:
```bash
make test
```
Outputs:
```text
auto reason sample: Auto: only 2 processes with similar burst times and few priorities - plain FCFS is enough.
256 checks, 0 failed
```
