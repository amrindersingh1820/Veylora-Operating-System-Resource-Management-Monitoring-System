/* vm_monitor.h – Virtual Machine Resource Monitor & Telemetry Engine
 *
 * Combines host-side QEMU hypervisor observation with in-guest kernel telemetry
 * into a unified, source-attributed resource measurement.
 * Manages background monitoring threads, SQLite persistence, and API endpoints.
 */

#ifndef VOS_VM_MONITOR_H
#define VOS_VM_MONITOR_H

#include "host_monitor.h"
#include "agent_protocol.h"
#include "process_monitor.h"
#include "resource_limits.h"
#include "../virtualization/vm_manager.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    METRIC_SOURCE_NONE     = 0,
    METRIC_SOURCE_HOST     = 1, /* Host QEMU process metrics only */
    METRIC_SOURCE_GUEST    = 2, /* In-guest kernel telemetry only */
    METRIC_SOURCE_COMBINED = 3  /* Both host and guest metrics active */
} MetricSource;

const char *metric_source_name(MetricSource src);

typedef enum {
    TELEMETRY_UNAVAILABLE = 0, /* No telemetry received yet or agent disconnected */
    TELEMETRY_LIVE        = 1, /* Fresh data received within timeout while QEMU alive */
    TELEMETRY_STALE       = 2  /* Data is old (> timeout) or QEMU has exited */
} TelemetryFreshness;

const char *telemetry_freshness_name(TelemetryFreshness f);

/* Comprehensive VM sample snapshot */
typedef struct {
    char               vm_id[64];
    uint64_t           timestamp_ms;
    MetricSource       metric_source;
    VmState            vm_state;
    pid_t              qemu_pid;
    uint64_t           uptime_seconds;

    /* Configured boundaries */
    int                vcpu_allocated;
    int                memory_mb_configured;

    /* Telemetry freshness & integrity */
    TelemetryFreshness freshness;
    uint64_t           telemetry_age_ms;
    uint64_t           last_telemetry_timestamp_ms;
    int                has_integrity_warning;
    char               integrity_warning[256];

    /* Host system metrics (macOS Mach / sysctl) */
    double             host_cpu_percent;
    uint64_t           host_memory_total_bytes;
    uint64_t           host_memory_used_bytes;

    /* Host observations of QEMU hypervisor process */
    double             qemu_cpu_percent;
    uint64_t           qemu_memory_rss_bytes;

    /* In-guest kernel metrics (reported as -1.0 / 0 if unavailable) */
    int                has_guest_telemetry;
    double             guest_cpu_percent;
    uint64_t           guest_memory_total_bytes;
    uint64_t           guest_memory_used_bytes;
    uint64_t           guest_memory_available_bytes;
    uint64_t           disk_read_bytes;
    uint64_t           disk_write_bytes;
    uint64_t           network_rx_bytes;
    uint64_t           network_tx_bytes;

    /* In-guest process snapshot */
    int                guest_process_count;
} VmResourceMetrics;

/* ------------------------------------------------------------------ API */

/* Initialize the monitoring engine and connect to the SQLite database.
 * If db_path is NULL, uses "virtual_os.db". */
int vm_monitor_init(const char *db_path);

/* Cleanly stop all active VM monitoring threads, flush pending writes, and release resources. */
void vm_monitor_shutdown(void);

/* Ensure database schema for resource_samples, process_samples, and resource_limits. */
int vm_monitor_db_ensure_schema(void);

/* Start background resource monitoring for a specific VM.
 * @param vm_id        Identifier of the target VM.
 * @param interval_ms  Sampling interval (minimum 100ms, default 1000ms).
 * @param mgr          Optional pointer to VmManager for live PID resolution (can be NULL).
 * Returns 0 on success, -1 on failure. */
int vm_monitor_start(const char *vm_id, int interval_ms, VmManager *mgr);

/* Stop background monitoring for a VM without affecting VM lifecycle. */
int vm_monitor_stop(const char *vm_id);

/* Check whether active background monitoring is running for a VM. */
bool vm_monitor_is_active(const char *vm_id);

/* Retrieve the latest resource metrics sample for a VM.
 * Returns 0 on success, -1 if no sample is available. */
int vm_monitor_get_latest(const char *vm_id, VmResourceMetrics *out);

/* Retrieve historical metrics samples from SQLite for a VM.
 * @param max_samples Maximum number of samples to return.
 * @param out_samples Output array.
 * @param out_count   Pointer receiving actual sample count. */
int vm_monitor_get_history(const char *vm_id, int max_samples,
                           VmResourceMetrics *out_samples, int *out_count);

/* Retrieve current in-guest process list for a VM with optional filtering. */
int vm_monitor_get_processes(const char *vm_id, const ProcessFilter *filter,
                             GuestProcessMetric *out_procs, int max_procs,
                             int *out_count);

/* Feed external/test telemetry directly into a VM monitor channel.
 * Validates against VM configuration boundaries and flags any integrity discrepancies. */
int vm_monitor_ingest_telemetry(const char *vm_id, const GuestTelemetryPayload *payload);

/* Validates incoming guest telemetry against VM configuration boundaries.
 * Returns 0 if valid and consistent.
 * Returns > 0 if integrity violations/warnings are detected (written to warn_buf). */
int vm_monitor_validate_telemetry(const VmConfig *cfg,
                                  const GuestTelemetryPayload *payload,
                                  char *warn_buf, size_t warn_sz);

/* Query host-wide metrics. */
int vm_monitor_get_host_metrics(HostMetrics *out);

/* Resource limits interface */
int vm_monitor_apply_limit(const char *vm_id, const AppResourceLimit *limit,
                           char *err_buf, size_t err_sz);

int vm_monitor_get_limit_status(const char *vm_id, const char *proc_name,
                                ResourceEnforcementRecord *out);

#ifdef __cplusplus
}
#endif

#endif /* VOS_VM_MONITOR_H */
