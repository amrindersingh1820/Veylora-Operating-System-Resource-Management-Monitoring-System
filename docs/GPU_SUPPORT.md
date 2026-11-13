# Virtual OS Simulator – GPU Virtualization & Desktop Application Analysis

## 1. Executive Summary

This document presents a systems analysis of graphics virtualization on **macOS Apple Silicon (M-series ARM64)**, the virtualization capabilities and limitations of **QEMU**, and the practical realities of running compute- and graphics-intensive desktop applications such as **Adobe Photoshop**, **Adobe Lightroom**, and **JetBrains IDEs** in isolated virtual machine environments.

### Fundamental Engineering Principle
In strict adherence to the project's zero-fabrication mandate:
> **GPU utilization metrics for guests running under QEMU on Apple Silicon are explicitly reported as `UNAVAILABLE` rather than generating simulated or estimated values.**
> The host system does not have access to in-guest hardware GPU telemetry, because the guest does not possess a virtualized 3D hardware accelerator.

---

## 2. Apple Silicon Host Graphics Architecture

Apple Silicon (e.g. M1 through M5) employs a **Unified Memory Architecture (UMA)**:
- **Shared Physical RAM**: The CPU cores, GPU execution units, Neural Engine, and Display Engine share a single high-bandwidth memory pool on the SoC package (up to 150+ GB/s memory bandwidth on base M-series, higher on Pro/Max).
- **Driver Model**: Graphics hardware access is mediated exclusively through Apple's proprietary `IOAccelerator` kernel extensions and the **Metal API**.
- **Host GPU Inspection**: Host GPU metrics can be observed at the kernel level via the macOS `IOKit` framework (`IOAccelerator` service matching `IOProperty` dictionary keys like `PerformanceStatistics`).
  - These metrics represent the aggregate activity of the entire macOS window server, Metal applications, and host display rendering.

---

## 3. QEMU Virtualization Capabilities & Limits on Apple Silicon

When running `qemu-system-aarch64` with hardware acceleration enabled via Apple's Hypervisor.framework (`-accel hvf`):

### What Is Supported
1. **CPU & Memory Virtualization**:
   Guest ARM64 instructions execute natively on Apple Silicon CPU cores with near bare-metal CPU performance.
2. **Standard Emulated Framebuffers**:
   - `ramfb`: A simple unaccelerated linear framebuffer allocated in guest RAM.
   - `virtio-gpu-pci` (2D Mode): A virtio standard 2D framebuffer providing resolution resizing and 2D blits.
3. **Display Output Interfaces**:
   - Native macOS Cocoa window (`-display cocoa`) rendering the framebuffer to a host window.
   - VNC server (`-display vnc=127.0.0.1:0`) streaming uncompressed or tile-compressed pixels.
   - Headless serial redirection (`-display none`).

### What Is NOT Supported (Architectural Barriers)
1. **No PCIe GPU Passthrough (VFIO)**:
   Apple Silicon does not use discrete PCIe graphics cards. The GPU is an integrated on-die block with proprietary registers and no standard IOMMU/VFIO virtualization interfaces. Direct GPU device assignment is physically impossible.
2. **No Virgl 3D Acceleration over HVF**:
   QEMU's `virglrenderer` translates guest OpenGL calls to host OpenGL calls. However:
   - macOS deprecated OpenGL in 2018 and does not provide modern OpenGL support.
   - `virglrenderer` requires a modern OpenGL or Vulkan host backend with EGL/GBM, which is unavailable in standard macOS userland without specialized translation layers (like MoltenVK/Zink), which are not integrated into upstream QEMU for macOS ARM64.
3. **No Apple Virtualization.framework (VZ) Metal Sharing**:
   Apple's native `Virtualization.framework` (introduced in macOS 13) provides a proprietary `VZVirtioGraphicsDeviceConfiguration` that exposes Metal acceleration to macOS guest VMs. However, this is a proprietary Apple API for macOS guests, not supported in general-purpose QEMU for Linux ARM64 or Windows ARM64.

---

## 4. Realistic Impact on Desktop Applications

The project plan anticipates evaluating real desktop applications in virtualized environments:

### A. Adobe Photoshop & Adobe Lightroom
| Dimension | Compatibility & Performance Reality |
| :--- | :--- |
| **OS Architecture** | Adobe Creative Cloud desktop applications **do not run on Linux ARM64**. They require either **macOS ARM64** or **Windows 11 on ARM64**. |
| **GPU Requirement** | Photoshop and Lightroom modern neural filters, camera raw processing, canvas panning/zooming, and liquify filters **mandate Direct3D 12 Feature Level 12_0 or Apple Metal**. |
| **Behavior in QEMU** | Under QEMU ARM64 with an emulated 2D framebuffer: |
| | - Photoshop will either fail GPU checks at launch or fall back to slow CPU software rendering. |
| | - Lightroom GPU-accelerated previews and export pipelines will be disabled. |
| | - Canvas redraws suffer noticeable lag because every frame is rendered on CPU and copied over emulated PCI. |
| **Path Forward** | Running Adobe applications requires either **Apple Virtualization.framework (macOS guest)** or a Windows 11 ARM64 VM with specialized para-virtualized DX12 drivers (currently only viable in commercial hypervisors like Parallels Desktop, not vanilla QEMU). |

### B. JetBrains IDEs (IntelliJ IDEA, CLion, PyCharm, WebStorm)
| Dimension | Compatibility & Performance Reality |
| :--- | :--- |
| **OS Architecture** | **Fully Compatible**: JetBrains provides first-class native **Linux ARM64** builds (with bundled JetBrains Runtime / OpenJDK 17/21 for AArch64). |
| **GPU Requirement** | The Java Swing / JetBrains UI rendering pipeline uses OpenGL or software rendering. |
| **Behavior in QEMU** | - Runs completely successfully in Linux ARM64 guests. |
| | - Java 2D software rendering (`-Dsun.java2d.opengl=false`) renders the IDE interface cleanly over `virtio-gpu-pci` or Cocoa display. |
| | - Code indexing, compilation (GCC/Clang ARM64), syntax parsing, and debugging execute at native Apple Silicon CPU speeds via `-accel hvf`. |
| **Verdict** | **Tier 1 Recommended Target**: Ideal candidate for early isolated development environment testing. |

### C. Web Browsers (Chromium, Firefox)
| Dimension | Compatibility & Performance Reality |
| :--- | :--- |
| **OS Architecture** | Native Linux ARM64 packages are standard in Alpine (`apk add chromium`), Debian (`apt install firefox-esr`), and Ubuntu. |
| **GPU Requirement** | Hardware WebGL and WebGPU acceleration will be disabled; browsers gracefully fall back to **SwiftShader** (CPU software rasterizer for Vulkan/GL). |
| **Behavior in QEMU** | Standard web browsing, DOM manipulation, DevTools, and web app testing work reliably. Video playback is decoded on CPU. |

---

## 5. Metric Source Taxonomy & Verification

In `src/monitoring/`:
- **`host_monitor.c`**: Reports Apple Silicon host CPU and physical RAM breakdown. Does not attempt to guess in-guest GPU usage.
- **`agent_protocol.c`**: Defines guest CPU, RAM, Disk, and Network telemetry. GPU metrics are omitted from the protocol because no in-guest hardware counter exists.
- **`vm_monitor.c`**:
  ```c
  /* GPU utilization is not measurable under QEMU on Apple Silicon.
   * Rather than fabricating simulated percentages, the monitoring
   * engine marks GPU telemetry as unavailable (METRIC_UNAVAILABLE). */
  ```

### Summary Matrix
| Metric | Source Available? | Mechanism |
| :--- | :--- | :--- |
| Host CPU % | **YES** | Mach Kernel `HOST_CPU_LOAD_INFO` |
| Host RAM Distribution | **YES** | Mach Kernel `HOST_VM_INFO64` |
| QEMU Process Host RSS | **YES** | `proc_pidinfo(PROC_PIDTASKINFO)` |
| QEMU Process Host CPU % | **YES** | `proc_pidinfo(PROC_PIDTASKINFO)` deltas |
| Guest CPU % | **YES** | Guest Agent `/proc/stat` |
| Guest RAM Breakdown | **YES** | Guest Agent `/proc/meminfo` |
| Guest Disk & Network I/O | **YES** | Guest Agent `/proc/diskstats`, `/proc/net/dev` |
| In-Guest Process Tree | **YES** | Guest Agent `/proc/[pid]/stat`, `/proc/[pid]/status` |
| Guest GPU 3D % | **NO** | Emulated 2D framebuffer only; marked **UNAVAILABLE** |

