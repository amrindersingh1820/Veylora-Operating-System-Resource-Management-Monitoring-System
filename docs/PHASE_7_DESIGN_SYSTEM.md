# Virtual OS Simulator — Phase 7 Design System Specification

## 1. Executive Summary & Aesthetic Direction

Phase 7 transforms the Virtual OS Simulator into a premium, modern macOS desktop application engineered for systems programmers, cloud architects, and systems researchers. Drawing direct inspiration from the refined ergonomics of **Linear**, the crisp typography and layout discipline of **Vercel**, the operational authority of **VMware Fusion**, and the native desktop fluidity of **Raycast**, the design system rejects amateur web trends (such as heavy cartoon gradients, garish drop shadows, and oversized border radii) in favor of restrained elegance, dense information hierarchy, and meticulous engineering craft.

---

## 2. Color System & Adaptive Theming

The application features a dual-theme architecture supporting dynamic switching between **Dark Theme** (default) and **Light Theme**. Theme state is managed reactively via `VosService` and resolved dynamically across all widgets via `VosColors.of(context)`.

### 2.1 Theme Palette Matrix

| Semantic Token | Dark Theme Value | Light Theme Value | Intended Usage |
|---|---|---|---|
| `bg` | `#090D14` (Deep Space Navy) | `#F8FAFC` (Slate Tint) | Primary application background |
| `surface` | `#101622` (Charcoal Slate) | `#FFFFFF` (Pure White) | Sidebar, top bar, modal backdrops |
| `card` | `#161E2E` (Layer 2 Surface) | `#FFFFFF` (Pure White) | KPI cards, data tables, list tiles |
| `cardHover` | `#1E283D` (Elevated Surface) | `#F1F5F9` (Subtle Hover) | Hover state for interactive cards |
| `border` | `#243047` (Precision Stroke) | `#E2E8F0` (Soft Edge) | Card borders, input strokes, dividers |
| `borderSubtle` | `#1A2332` (Micro Stroke) | `#EDF2F7` (Hairline) | Internal list dividers, grid lines |
| `textPrimary` | `#F1F5F9` (High Contrast) | `#0F172A` (Deep Slate) | Screen titles, KPI values, headers |
| `textSecondary`| `#94A3B8` (Muted Slate) | `#475569` (Charcoal) | Subtitles, field labels, metadata |
| `textMuted` | `#64748B` (Secondary Muted) | `#94A3B8` (Soft Gray) | Timestamps, inactive states, hints |
| `primary` | `#3B82F6` (Electric Blue) | `#2563EB` (Cobalt Blue) | Primary actions, active indicators |
| `secondary` | `#6366F1` (Indigo Pulse) | `#4F46E5` (Deep Indigo) | Secondary badges, accents |
| `success` | `#10B981` (Emerald Leaf) | `#059669` (Dark Emerald) | Running VMs, healthy cgroups, OK |
| `warning` | `#F59E0B` (Amber Flame) | `#D97706` (Deep Amber) | Throttled tasks, standby agents |
| `danger` | `#EF4444` (Crimson Alert) | `#DC2626` (Pure Red) | OOM kills, failed VMs, errors |

### 2.2 Semantic Status Mapping

Status representations across the application follow strict visual semantics:

* **RUNNING / ONLINE / PASS**: Emerald badge (`#10B981`) with a live pulsing indicator dot.
* **STARTING / READY / CONNECTED**: Electric Blue badge (`#3B82F6`).
* **STANDBY / PAUSED / THROTTLED**: Amber badge (`#F59E0B`).
* **STOPPED / CREATED / INACTIVE**: Neutral Slate badge (`#64748B`).
* **FAILED / REJECTED / OOM**: Crimson badge (`#EF4444`).

---

## 3. Typography & Numerical Layout

The typography hierarchy is optimized for dense technical dashboards and monospaced telemetry:

| Level | Size | Weight | Tracking | Typical Element |
|---|---|---|---|---|
| **Display Title** | 24 px | Bold (700) | -0.5 px | Hero views, welcome overview |
| **View Header** | 20 px | Bold (700) | -0.4 px | Screen titles |
| **Section Header** | 16 px | SemiBold (600) | -0.3 px | Sub-sections, modal headers |
| **KPI Value** | 22 px | Bold (700) | -0.5 px | Metric counters, percentages |
| **Body Primary** | 13 px | Regular (400) | -0.1 px | Standard labels, table cells |
| **Body Strong** | 13 px | SemiBold (600) | -0.1 px | Table headers, button text |
| **Sub-text / Meta**| 11 px | Regular (400) | 0.0 px | Timestamps, status subtitles |
| **Code / Micro** | 10-11 px | Regular (400) | 0.0 px | File paths, PIDs, cgroup paths |

---

## 4. Layout Architecture & Spatial Grid

1. **4px Base Grid**: All padding, margins, and component dimensions adhere to multiples of 4 (4, 8, 12, 16, 20, 24, 32, 48).
2. **Window Shell**:
   - Fixed-collapsible left navigation sidebar (230px expanded, 68px collapsed).
   - Global persistent `AppTopBar` (60px fixed height) with breadcrumbs, active VM selector, in-guest agent heartbeat, and instant theme toggling.
   - Screen content area with smooth page transitions using an `IndexedStack` preserving sub-view state across navigation switches.
3. **Corner Radii Hierarchy**:
   - Status Pills & Badges: `4 px` (micro radius) or `10 px` (pill radius).
   - Input Controls & Small Buttons: `6 px`.
   - Cards, Modals & Dropdown Sheets: `8 px` to `10 px`.
   - Hero Banners & Dialog Windows: `12 px`.

---

## 5. Reusable Component Catalog

### 5.1 `NavigationSidebar`
Collapsible navigation sidebar featuring:
- Brand identity with hardware detection tag (`Apple Silicon M5`).
- Active link highlighting with vertical accent pill and low-alpha primary background tint.
- 8 distinct operational categories: Dashboard, Virtual Machines, Applications, Resource Profiles, Live Monitoring, OS Simulators, Audit & Events, Settings.
- Quick collapse/expand toggle preserving user preference.

### 5.2 `AppTopBar`
Persistent top header providing:
- Screen title and contextual subtitle.
- Quick VM switcher with live state indicator.
- Guest agent status badge (`AGENT READY` / `STANDBY`).
- Single-click theme toggle (Dark / Light).
- Manual telemetry sync trigger.

### 5.3 `MetricCard`
Compact KPI display card providing:
- Icon header with primary/status tone.
- High-contrast numerical metric value.
- Descriptive subtitle and percentage trend indicators.
- Optional embedded sparkline graph.

### 5.4 `MetricChart`
High-performance custom canvas time-series chart widget providing:
- Bezier smoothing curve with zero jitter.
- Dual-stop gradient area fill beneath the metric path.
- Dynamic min/max baseline and ceiling markers with grid lines.
- Restrained data point markers for the latest telemetry reading.
- Empty state fallback for cold-boot telemetry intervals.

### 5.5 `StatusBadge`
Semantic status pill widget providing:
- Glowing circle dot indicator.
- Contrast-adjusted text styling.
- Automatic adaptation between dark and light themes.

### 5.6 `VMCard`
Virtual machine management card providing:
- Header with VM name, ID, and live execution status.
- Grid breakdown of hardware configuration (vCPUs, configured RAM, storage backend).
- Live QEMU HVF host telemetry integration (Host RSS, Host CPU utilization).
- Contextual lifecycle controls (Start, Graceful Stop, Force Stop, Delete).

### 5.7 `ResourceProfileCard`
Cgroups v2 resource profile card providing:
- Profile identifier and description.
- Cgroups v2 specification badges: CPU cores (`cpu.max`), CPU weight (`cpu.weight`), RAM limit (`memory.max`), PID limit (`pids.max`), and I/O bandwidth (`io.max`).
- Visual indicator confirming verified kernel readback and GPU unsupported-platform classification.
- Action buttons for assigning, editing, or deleting profiles.

### 5.8 `FilterBar`
Universal desktop filter and search toolbar:
- Instant text filter input with search icon and clear trigger.
- Category / state dropdown selector.
- Primary action slot for view-specific operations (e.g. "+ New Profile", "Scan In-Guest").

### 5.9 `ConfirmationDialog`
Refined modal dialog for destructive or high-impact actions:
- Clear, unambiguous title and explanatory message.
- Explicit destructive action button (`Delete`, `Force Stop`) styled in crimson.
- Dismissal via `Cancel` or background tap.

### 5.10 `EmptyState`
Minimalist placeholder state:
- Clean icon glyph inside a rounded border.
- Bold heading with helpful secondary description.
- Optional primary action button to resolve the empty state immediately.

