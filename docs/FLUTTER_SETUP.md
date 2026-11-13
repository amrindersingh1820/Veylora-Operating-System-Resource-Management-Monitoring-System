# Virtual OS Simulator: Flutter Desktop Setup Guide

**Target OS**: macOS Apple Silicon M5 (ARM64, Darwin 27.2)  
**Flutter Target**: macOS Desktop (`darwin-arm64`)  
**Backend**: C11 Shared Dynamic Library (`build/libvos_backend.dylib`)

---

## 1. System Prerequisites

Before running the Flutter frontend on macOS, ensure the following tools are available in your environment:

1. **Flutter & Dart SDK**:
   * Flutter 3.27+ (Current environment: Flutter 3.47.8 / Dart 3.13.7)
   * Verify with:
     ```bash
     flutter doctor -v
     ```
2. **C11 Toolchain**:
   * Apple Clang / LLVM (installed via `xcode-select --install`)
   * POSIX threads (`-lpthread`) and math library (`-lm`)
3. **Virtualization & Hypervisor Tools**:
   * QEMU emulator with HVF acceleration:
     ```bash
     brew install qemu
     ```
   * EDK2 UEFI firmware for ARM64:
     ```bash
     # Typically located at /opt/homebrew/share/qemu/edk2-aarch64-code.fd
     ```
4. **Xcode for macOS Native App Bundle**:
   * If you have Xcode installed at `/Applications/Xcode.app` but see `Error: Xcode not installed`, macOS is likely pointing to the standalone Command Line Tools.
   * Fix system-wide (requires sudo):
     ```bash
     sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
     ```
   * Or export per shell session / in `~/.zshrc`:
     ```bash
     export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
     ```

---

## 2. Compiling the C Dynamic Library (`libvos_backend.dylib`)

The Flutter desktop app communicates directly with the C engine using `dart:ffi`. The shared library must be compiled with `-fPIC -dynamiclib`:

```bash
# In the repository root:
make dylib
```

This compiles:
* `src/core/scheduler.c`, `memory.c`, `resources.c`, `util.c`
* `src/db/database.c` (SQLite 3.43 in WAL mode)
* `src/virtualization/vm_config.c`, `vm_manager.c`
* `src/monitoring/host_monitor.c`, `process_monitor.c`, `resource_limits.c`, `vm_monitor.c`, `resource_manager.c`
* `src/application/app_manager.c`, `guest_agent/src/cgroup_manager.c`, `guest_agent/src/app_launcher.c`
* `src/api/vos_api.c`

Output: `build/libvos_backend.dylib`

Verify the dynamic library using the automated API test:
```bash
make test_api
```

---

## 3. Setting Up the Flutter Frontend

1. Navigate to the `frontend/` directory:
   ```bash
   cd frontend
   ```

2. Fetch Flutter package dependencies:
   ```bash
   flutter pub get
   ```

3. Run static code analysis to confirm zero lints:
   ```bash
   flutter analyze
   ```

4. Run the widget and component test suite:
   ```bash
   flutter test
   ```

---

## 4. Running the Flutter Desktop Application

To launch the desktop application in debug mode:

```bash
cd frontend
flutter run -d macos
```

### Dynamic Library Resolution Path
The `VosFfiBridge` dynamically searches the following paths for `libvos_backend.dylib`:
1. `build/libvos_backend.dylib` (from the repository root)
2. `../build/libvos_backend.dylib` (relative to the Flutter app bundle)
3. `../../build/libvos_backend.dylib`
4. `/Users/amrindersingh/Downloads/VirtualOS_Simulator/build/libvos_backend.dylib`
5. Process symbol table fallback (`DynamicLibrary.process()`)

If launching the application outside the repository root, you can place `libvos_backend.dylib` alongside the macOS executable inside `Contents/Frameworks/` or export:
```bash
export DYLD_LIBRARY_PATH="/path/to/VirtualOS_Simulator/build:$DYLD_LIBRARY_PATH"
```

---

## 5. Building for Release

To produce an optimized macOS application bundle:

```bash
cd frontend
flutter build macos --release
```

The compiled bundle is generated at:
`frontend/build/macos/Build/Products/Release/virtual_os_simulator.app`

Copy `build/libvos_backend.dylib` to the app's Frameworks folder:
```bash
mkdir -p frontend/build/macos/Build/Products/Release/virtual_os_simulator.app/Contents/Frameworks/
cp build/libvos_backend.dylib frontend/build/macos/Build/Products/Release/virtual_os_simulator.app/Contents/Frameworks/
```

