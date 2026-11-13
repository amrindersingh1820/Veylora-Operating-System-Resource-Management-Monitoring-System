# VEYLORA Design System

**Product Category:** Virtual Computing Environment  
**Tagline:** *Beyond Virtual. Beyond Limits.*  
**Version:** v1.6.0 Pro  
**Design Standard:** UI/UX Pro Max Desktop Specification  

---

## 1. Brand Identity & Personality

VEYLORA is an enterprise-grade virtual computing and operating system environment engineered for Apple Silicon and Linux guest hypervisors. The visual design language communicates:

- **Intelligent:** Data-dense telemetry presented with crystalline clarity.
- **Premium:** Deep charcoal surfaces, subtle 1px border hierarchy, metallic accents.
- **Minimalistic:** High signal-to-noise ratio; eliminates decorative fluff.
- **Futuristic:** Deep space gradients, micro-glow active indicators, live Mach kernel streaming.
- **Technical & Reliable:** Deterministic telemetry labels (`MACH_HOST_STATISTICS`, `TASK_BASIC_INFO_64`, `CGROUPS_V2_CONTROLLER`), tabular numerals, and SQLite WAL audit trails.

### Design Inspirations
- **Linear:** Compact information density, keyboard-first ergonomics, subtle active navigation pills.
- **Vercel:** Monospaced metadata badges, clean typography, stark contrast.
- **Raycast:** macOS-native desktop shell feel, fast transitions, polished window chrome.
- **Stripe Dashboard:** High-contrast metric cards, live sparklines with area fills, peak/average callouts.
- **Arc Browser:** Sophisticated border gradients, subtle hover feedback, refined dark theme palette.
- **Proxmox:** Hypervisor resource hierarchy, hardware quota isolation, explicit hardware capabilities.

---

## 2. Centralized Design Tokens (`frontend/lib/core/theme.dart`)

### 2.1 Color Palette

#### Dark Palette (Primary Interface)
| Token | Hex Value | Semantic Purpose |
|---|---|---|
| `bg` | `#090B10` | Deep near-black background canvas |
| `surface` | `#0D1117` | Sidebar, Top Bar, and structural chrome |
| `card` | `#131722` | Content cards, data tables, and modal dialogs |
| `cardHover` | `#1A2030` | Interactive card hover state |
| `border` | `#1F2738` | Structural container dividers (1px) |
| `borderSubtle`| `#171E2D` | Internal row dividers and minor borders |
| `textPrimary` | `#F1F5F9` | Near-white high-contrast text |
| `textSecondary`| `#94A3B8`| Neutral secondary labels and descriptions |
| `textMuted` | `#64748B` | Timestamps, units, and disabled text |
| `primary` | `#3B82F6` | Electric Blue accent |
| `accent` | `#6366F1` | Restrained Violet for secondary highlights |
| `success` | `#10B981` | Emerald for running VMs, live agent, and verified state |
| `warning` | `#F59E0B` | Amber for unsupported virtual GPU, cautions, and throttling |
| `danger` | `#EF4444` | Crimson for errors, stopped VMs, and OOM terminations |
| `info` | `#06B6D4` | Cyan for algorithms and informational callouts |

#### Layer-Specific Telemetry Colors
| Telemetry Layer | Color Hex | Source Indicator |
|---|---|---|
| **Host macOS (Darwin)** | `#3B82F6` (Blue) | `MACH_HOST_STATISTICS` |
| **QEMU Hypervisor** | `#8B5CF6` (Purple) | `TASK_BASIC_INFO_64` |
| **Guest OS (Alpine)** | `#10B981` (Green) | `GUEST_VIRTIO_PROCFS` |
| **Cgroup v2 Enforce** | `#F59E0B` (Amber) | `CGROUPS_V2_CONTROLLER` |

---

### 2.2 Typography Scale (`VeyloraTypography`)

- **Primary Font Family:** System Sans-Serif (`Inter` / `.AppleSystemUIFont` / `SF Pro`)
- **Monospace Font Family:** `SF Mono` / `Menlo` / `JetBrains Mono` / `Courier`
- **Tabular Numerals:** All performance metrics, CPU percentages, memory sizes, and durations use font feature `tnum` to eliminate layout jitter during 1Hz real-time telemetry updates.

| Style | Size | Weight | Line Height | Usage |
|---|---|---|---|---|
| `displayLarge` | 26px | 800 (Bold) | 1.2 | Splash hero, big headers |
| `displayMedium` | 20px | 700 (Bold) | 1.25 | View headers, dashboard titles |
| `title` | 15px | 600 (Semi) | 1.3 | Card titles, modal headers |
| `body` | 13px | 400 (Regular)| 1.45 | Standard prose and descriptions |
| `caption` | 11.5px| 400 (Regular)| 1.4 | Subtitles, helper text |
| `metricLarge` | 24px | 700 (Bold) | 1.1 | Big numeral metrics on cards |
| `metricMedium`| 18px | 600 (Semi) | 1.2 | Chart current values |
| `code` | 11px | 500 (Mono) | 1.4 | Paths, cgroup IDs, registers |

---

### 2.3 Spacing Scale (`VeyloraSpacing`)

```dart
static const double xxs  = 2.0;
static const double xs   = 4.0;
static const double sm   = 8.0;
static const double md   = 12.0;
static const double lg   = 16.0;
static const double xl   = 20.0;
static const double xxl  = 24.0;
static const double xxxl = 32.0;
```

---

### 2.4 Corner Radii (`VeyloraRadius`)

```dart
static const double xs   = 4.0;   // Status pills, badges
static const double sm   = 6.0;   // Inputs, chips, small buttons
static const double md   = 8.0;   // Nested panels, tables
static const double lg   = 12.0;  // Metric cards, dialogs
static const double xl   = 16.0;  // Large modal dialogs
static const double full = 999.0; // Round status pills
```

---

### 2.5 Motion & Transitions (`VeyloraMotion`)

- `fast`: `140ms` (`Curves.easeOutCubic`) — hover states, button presses.
- `normal`: `240ms` (`Curves.easeOutCubic`) — tab changes, drawer collapse.
- `smooth`: `360ms` (`Curves.easeInOutCubic`) — screen transitions, splash boot fade.

---

## 3. Component Architecture

### 3.1 Application Shell & Navigation Sidebar (`NavigationSidebar`)
- **Structure:** 5 Distinct Semantic Sections:
  1. `OVERVIEW`: Dashboard (index `0`)
  2. `MANAGEMENT`: Virtual Machines (`1`), Applications (`2`), Resource Management (`3`)
  3. `OBSERVABILITY`: Monitoring (`4`), Event Logs (`6`)
  4. `TOOLS`: OS Algorithms (`5`)
  5. `PREFERENCES`: Settings (`7`)
- **Visuals:** Active vertical indicator bar, running VM and application badges, metallic brand logo emblem, collapsible trigger, and host hardware spec footer.

### 3.2 Top Bar (`AppTopBar`)
- **Breadcrumbs:** `Veylora > [SECTION] > [PAGE]` with dynamic routing.
- **Engine Pill:** Live C11 Mach Core indicator with pulsing green dot.
- **Active VM Selector:** Dropdown selector with quick state transitions.
- **Guest Agent Badge:** Instant feedback on Virtio serial connection (`AGENT READY` / `STANDBY`).
- **Toolbar Actions:** Replay Boot Console / Splash, Theme Switcher, Force Telemetry Refresh.

### 3.3 Metric Cards (`MetricCard`)
- High-contrast numeral display with tabular font features.
- Custom-painted bezier sparkline with subtle gradient area fill and glowing endpoint dot.
- Compact icon badge with 12% opacity accent fill.

### 3.4 Metric Charts (`MetricChart`)
- Smooth cubic spline interpolation (`CubicBezier`) over 10s/30s/60s rolling telemetry windows.
- Dashed horizontal reference gridlines at 25%, 50%, 75%, and 100%.
- Dynamic peak and average calculation pills rendered directly in chart header.
- Status indicator pill (`LIVE 1Hz`).

### 3.5 Status Badges (`StatusBadge`)
- Semantic colors (`RUNNING`, `LIVE`, `STOPPED`, `UNSUPPORTED`, `ENFORCED`).
- Integrated 6px status dot with subtle border and monospace uppercase typography.

### 3.6 Boot Splash & Diagnostic Screen (`VeyloraSplashScreen`)
- High-resolution cinematic background with subtle dark vignette.
- Realistic C11 kernel and hypervisor boot console streaming actual initialization milestones.
- Skip shortcut (`Esc` or `Space`) and smooth opacity crossfade into main workspace.

