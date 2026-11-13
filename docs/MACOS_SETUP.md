# Virtual OS Simulator – macOS Setup & Developer Guide

This guide describes how to configure, build, run, and test **Virtual OS Simulator** on macOS, specifically optimized for **Apple Silicon (ARM64: M1, M2, M3, M4, M5)**.

---

## 1. System Requirements & Prerequisites

* **Operating System**: macOS Sonoma (14.0) or Sequoia (15.0+) on Apple Silicon (arm64).
* **Compiler & Toolchain**: Apple Clang supporting C11 (`xcode-select --install`).
* **Build System**: GNU Make (built into Xcode tools) and/or CMake 3.16+.
* **Package Manager**: [Homebrew](https://brew.sh) (installs to `/opt/homebrew` on Apple Silicon).

### Installing Dependencies via Homebrew

```bash
# Update Homebrew
brew update

# Install SDL2 (for graphical GUI target)
brew install sdl2

# Install SQLite3 development headers and CLI
brew install sqlite3

# Install QEMU (for hardware-accelerated ARM64 guest virtualization)
brew install qemu
```

> **Note on Bundled SQLite**: The project includes `third_party/sqlite3.c` and `third_party/sqlite3.h` as an embedded amalgamation, ensuring consistent builds even if system SQLite headers differ.

---

## 2. Building the Project

The project provides two fully-supported build systems: **Make** and **CMake**.

### Option A: Building with Make (Recommended for Fast Iteration)

```bash
# 1. Clean previous build artifacts
make clean

# 2. Build the complete project with SDL2 GUI and CLI
make

# 3. Build standalone CLI version (zero graphical dependencies)
make cli

# 4. Build explicit SDL2 GUI version
make sdl
```

### Option B: Building with CMake

```bash
# 1. Configure CMake build directory
cmake -B build -S .

# 2. Compile all targets (core libraries, CLI, GUI, and test suites)
cmake --build build

# The resulting binaries will be in build/:
# - build/virtual_os
# - build/virtual_os_cli
# - build/test_core
# - build/test_vm
```

---

## 3. Running Automated Tests

The repository includes a two-tiered test suite with **370 automated checks**:
1. **Core Simulation Engine Tests** (`tests/test_core.c`): 256 checks verifying CPU schedulers, memory managers, Banker's algorithm, and database transactions.
2. **Virtual Machine Subsystem Tests** (`tests/test_vm.c`): 114 checks verifying configuration validation, display and net options, secondary disk handling, EDK2 firmware detection, QEMU detection, database stale state reconciliation, POSIX process management, signal handling, child reaping, and real ARM64 QEMU guest execution.

### Run All Tests via Make

```bash
make test_all
```

Expected output:
```
./build/test_core
256 checks, 0 failed
./build/test_vm
=== Virtual OS Simulator – VM Module Tests ===
...
=== 114 checks, 0 failed ===
```

### Run Tests Individually

```bash
# Run core simulation tests
make test

# Run virtualization module tests
make test_vm
```

---

## 4. Launching the Simulator

### Interactive Terminal CLI (`virtual_os_cli`)

Launch the standalone text-based simulator interface:
```bash
./virtual_os_cli
```
* Supports interactive process queue creation and simulation.
* Benchmarks FCFS, SJF, SRTF, Priority, and Round Robin scheduling.
* Runs First Fit, Best Fit, and Worst Fit memory allocation simulations.
* Demonstrates Banker's deadlock safety checks.
* **Option [8] Virtual Machine Manager (ARM64 / QEMU)**:
  * Configure real ARM64 VMs with vCPU, RAM, display, and networking.
  * Start and stop real guest operating systems via QEMU with HVF acceleration.
  * View live PID, console output logs, and lifecycle event history.
  * See [VM_OPERATIONS.md](VM_OPERATIONS.md) for full instructions.

### Graphical Desktop UI (`virtual_os`)

Launch the SDL2 hardware-accelerated desktop GUI:
```bash
./virtual_os
```
* Provides interactive GUI controls for simulation execution, process tables, and memory maps.

---

## 5. Troubleshooting macOS Build Issues

### Issue 1: `fatal error: 'SDL.h' file not found`
* **Cause**: Homebrew on Apple Silicon installs libraries to `/opt/homebrew`, whereas Intel Macs use `/usr/local`.
* **Fix**: The project Makefile automatically detects `sdl2-config` and adds `-I/opt/homebrew/include/SDL2`. If building manually, ensure Homebrew's bin directory is on your PATH:
  ```bash
  export PATH="/opt/homebrew/bin:$PATH"
  ```

### Issue 2: `Undefined symbols for architecture arm64: _sqlite3_...`
* **Cause**: Linker attempting to link system `/usr/lib/libsqlite3.dylib` without custom flags or missing compilation of `third_party/sqlite3.c`.
* **Fix**: Run `make clean && make`, ensuring `build/sqlite3.o` is compiled by the Makefile target.

### Issue 3: Linker error involving Cocoa framework
* **Cause**: SDL2 on macOS requires Cocoa framework integration.
* **Fix**: CMakeLists.txt and Makefile already pass `-Wl,-framework,Cocoa`. Ensure you are compiling on macOS with Xcode Command Line Tools installed (`xcode-select --install`).
