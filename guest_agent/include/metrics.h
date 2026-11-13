/* metrics.h – Guest Monitoring Agent Metric Declarations
 *
 * Defines collectors and data structures for in-guest Linux resource telemetry.
 */

#ifndef VOS_GUEST_METRICS_H
#define VOS_GUEST_METRICS_H

#include "agent_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configurable procfs root (defaults to /proc, overrideable via VOS_PROC_ROOT env or API) */
const char *agent_get_proc_root(void);
void        agent_set_proc_root(const char *root);

/* Initialize subsystem collectors */
int cpu_monitor_init(void);
int memory_monitor_init(void);
int disk_monitor_init(void);
int network_monitor_init(void);
int process_monitor_init(void);

/* Sample specific metrics */
int cpu_monitor_sample(GuestCpuMetrics *out);
int memory_monitor_sample(GuestMemoryMetrics *out);
int disk_monitor_sample(GuestDiskMetrics *out);
int network_monitor_sample(GuestNetworkMetrics *out);
int process_monitor_sample(GuestProcessMetric *out_list, int max_procs, int *out_count);
int process_monitor_sample_ex(GuestProcessMetric *out_list, int max_procs, int *out_count,
                              int *out_total_discovered, uint64_t *out_snapshot_time_ms);

/* Shutdown collectors */
void cpu_monitor_shutdown(void);
void memory_monitor_shutdown(void);
void disk_monitor_shutdown(void);
void network_monitor_shutdown(void);
void process_monitor_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* VOS_GUEST_METRICS_H */
