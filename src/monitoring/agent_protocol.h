/* agent_protocol.h – Structured Telemetry Protocol for Guest Agent & Backend
 *
 * Defines the wire format and serialization for metrics transmitted from the
 * guest monitoring agent to the Virtual OS Simulator backend.
 *
 * Requirements:
 *   - Structured JSON format.
 *   - Metric timestamps & sequence counter.
 *   - VM identifier association.
 *   - Message validation and error handling.
 *   - Cumulative counters vs. interval rates clearly distinguished.
 *   - Strictly monitoring-only: NO arbitrary remote command execution.
 */

#ifndef VOS_AGENT_PROTOCOL_H
#define VOS_AGENT_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define AGENT_PROTOCOL_VERSION 1
#define AGENT_DEFAULT_VIRTIO_PORT "/dev/virtio-ports/org.virtualos.agent.0"
#define AGENT_DEFAULT_TCP_PORT 9888
#define AGENT_MAX_PROCESSES 256
#define AGENT_MAX_CORES 64
#define AGENT_MAX_JSON_SZ 65536

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ Metric Structs */

typedef struct {
    double   total_percent;       /* -1.0 if uncomputed/first sample */
    double   user_percent;
    double   system_percent;
    double   idle_percent;
    double   iowait_percent;
    int      core_count;
    double   core_percents[AGENT_MAX_CORES];
    uint64_t user_ticks;
    uint64_t nice_ticks;
    uint64_t system_ticks;
    uint64_t idle_ticks;
    uint64_t iowait_ticks;
    uint64_t irq_ticks;
    uint64_t softirq_ticks;
    uint64_t sample_interval_ms;  /* Interval between consecutive /proc/stat samples */
} GuestCpuMetrics;

typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t available_bytes;
    uint64_t buffers_bytes;
    uint64_t cached_bytes;
    uint64_t swap_total_bytes;
    uint64_t swap_used_bytes;
    uint64_t swap_free_bytes;
} GuestMemoryMetrics;

typedef struct {
    uint64_t read_bytes;          /* Cumulative bytes read */
    uint64_t write_bytes;         /* Cumulative bytes written */
    uint64_t read_ops;            /* Cumulative read I/O operations */
    uint64_t write_ops;           /* Cumulative write I/O operations */
    uint64_t io_in_progress;      /* In-flight I/O requests */
    double   read_bytes_per_sec;  /* Interval read rate */
    double   write_bytes_per_sec; /* Interval write rate */
    char     primary_disk[32];    /* e.g. "vda", "sda" */
} GuestDiskMetrics;

typedef struct {
    uint64_t rx_bytes;            /* Cumulative received bytes */
    uint64_t tx_bytes;            /* Cumulative transmitted bytes */
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_errors;
    uint64_t tx_errors;
    double   rx_bytes_per_sec;    /* Interval receive rate */
    double   tx_bytes_per_sec;    /* Interval transmit rate */
    char     iface_name[32];      /* Primary interface name (e.g. "eth0") */
    int      interface_count;     /* Number of active non-loopback interfaces */
} GuestNetworkMetrics;

typedef struct {
    pid_t    pid;
    pid_t    ppid;
    char     name[64];
    char     state[8];          /* "R", "S", "D", "Z", "T" */
    double   cpu_percent;
    uint64_t memory_rss_bytes;
    uint64_t memory_virt_bytes;
    uint64_t start_time_s;
} GuestProcessMetric;

typedef struct {
    int                 version;
    char                vm_id[64];
    uint64_t            timestamp_ms;
    uint64_t            sequence;
    GuestCpuMetrics     cpu;
    GuestMemoryMetrics  memory;
    GuestDiskMetrics    disk;
    GuestNetworkMetrics network;
    int                 total_process_count;           /* Total active processes discovered in guest OS */
    uint64_t            process_snapshot_timestamp_ms; /* Timestamp of process snapshot */
    int                 process_count;                 /* Filtered/top-N processes serialized in array */
    GuestProcessMetric  processes[AGENT_MAX_PROCESSES];
} GuestTelemetryPayload;

/* ------------------------------------------------------------------ Serialization */

/* Serialize telemetry payload to compact JSON.
 * Returns bytes written (>0) on success, -1 on buffer overflow or error. */
int agent_protocol_serialize(const GuestTelemetryPayload *payload,
                             char *out_buf, size_t out_sz);

/* Parse a JSON telemetry message into payload struct.
 * Validates protocol version and required fields.
 * Returns 0 on success, -1 on parse failure, -2 on unsupported version. */
int agent_protocol_deserialize(const char *json_str,
                               GuestTelemetryPayload *out_payload);

/* ------------------------------------------------------------------ Phase 4: RPC Protocol */

/* Resource management RPC operations */
typedef enum {
    AGENT_OP_UNKNOWN = 0,
    AGENT_OP_GET_CAPABILITIES,
    AGENT_OP_CREATE_GROUP,
    AGENT_OP_SET_CPU_LIMIT,
    AGENT_OP_SET_CPU_WEIGHT,
    AGENT_OP_SET_MEMORY_LIMIT,
    AGENT_OP_SET_PIDS_LIMIT,
    AGENT_OP_SET_IO_LIMIT,
    AGENT_OP_ATTACH_PROCESS,
    AGENT_OP_GET_GROUP_STATUS,
    AGENT_OP_REMOVE_GROUP,
    /* Phase 5: Application Management */
    AGENT_OP_DISCOVER_APPS,
    AGENT_OP_LAUNCH_APP,
    AGENT_OP_STOP_APP,
    AGENT_OP_GET_APP_METRICS
} AgentResourceOp;

/* Status codes */
typedef enum {
    AGENT_STATUS_SUCCESS = 0,
    AGENT_STATUS_INVALID_CONFIG = -1,
    AGENT_STATUS_UNSUPPORTED = -2,
    AGENT_STATUS_PERMISSION_DENIED = -3,
    AGENT_STATUS_GROUP_NOT_FOUND = -4,
    AGENT_STATUS_PROCESS_NOT_FOUND = -5,
    AGENT_STATUS_COMMUNICATION_FAILED = -6,
    AGENT_STATUS_VERIFICATION_FAILED = -7,
    AGENT_STATUS_LIMIT_BELOW_USAGE = -8,
    AGENT_STATUS_GROUP_BUSY = -9,
    AGENT_STATUS_FAILED = -10,
    AGENT_STATUS_PROCESS_IDENTITY_CHANGED = -11,
    AGENT_STATUS_UNSUPPORTED_BY_HOST_HYPERVISOR = -12,
    AGENT_STATUS_UNSUPPORTED_PLATFORM = -13,
    AGENT_STATUS_UNSUPPORTED_HARDWARE = -14,
    /* Phase 5: Application Status */
    AGENT_STATUS_APP_NOT_FOUND = -15,
    AGENT_STATUS_APP_ALREADY_RUNNING = -16,
    AGENT_STATUS_LAUNCH_FAILED = -17,
    AGENT_STATUS_CGROUP_ATTACH_FAILED = -18
} AgentStatusCode;

#define AGENT_MAX_DISCOVERED_APPS 64

typedef struct {
    char     name[64];
    char     path[256];
    int      is_executable;
    uint32_t mode;
    char     package[64];
} AgentDiscoveredApp;

typedef struct {
    int      count;
    AgentDiscoveredApp apps[AGENT_MAX_DISCOVERED_APPS];
} AgentAppDiscoveryList;

/* Request struct */
typedef struct {
    int             version;
    char            request_id[64];
    char            vm_id[64];
    AgentResourceOp operation;
    char            group_id[64];
    char            group_name[64];
    /* Parameters */
    int64_t         cpu_quota_us;        /* Quota in us, or -1 for unconstrained */
    int64_t         cpu_period_us;       /* Period in us, typically 100000 */
    double          cpu_cores;           /* Fractional cores, e.g. 1.5. If >0, converts to quota/period */
    int             cpu_weight;          /* 1-10000, or -1 for default (100) */
    uint64_t        memory_max_bytes;    /* Memory ceiling in bytes, or 0 for unconstrained */
    int64_t         pids_max;            /* Max PIDs, or -1 for max */
    uint64_t        io_read_bps;         /* Max read bytes/sec, 0 for unlimited */
    uint64_t        io_write_bps;        /* Max write bytes/sec, 0 for unlimited */
    pid_t           pid;                 /* Target PID for attach */
    uint64_t        expected_start_time; /* Expected process start time from /proc/<pid>/stat (0 = unvalidated) */
    /* Phase 5 Application Launch & Lifecycle Parameters */
    char            app_id[64];
    char            app_path[256];
    char            app_args[512];
    int             app_timeout_ms;
    int             app_signal;
} AgentResourceRequest;

/* Live Cgroup status/metrics struct */
typedef struct {
    char            group_id[64];
    int             exists;
    int             verified;
    int64_t         cpu_quota_us;
    int64_t         cpu_period_us;
    int             cpu_weight;
    uint64_t        cpu_usage_usec;
    uint64_t        cpu_throttled_usec;
    uint64_t        memory_max_bytes;
    uint64_t        memory_current_bytes;
    uint64_t        memory_peak_bytes;
    uint64_t        memory_oom_events;
    int64_t         pids_max;
    int64_t         pids_current;
    int64_t         pids_events;
    uint64_t        io_read_bps;
    uint64_t        io_write_bps;
    uint64_t        io_read_bytes;
    uint64_t        io_write_bytes;
    int             attached_pids_count;
    pid_t           attached_pids[64];
} AgentCgroupStatus;

/* Capabilities struct */
typedef struct {
    int             cgroup_v2_supported;
    int             cgroup_v2_mounted;
    int             cpu_controller;
    int             memory_controller;
    int             pids_controller;
    int             io_controller;
    int             gpu_supported;       /* Strictly 0 (unsupported on QEMU HVF Apple Silicon) */
    char            gpu_status[64];      /* "UNSUPPORTED_BY_HOST_HYPERVISOR" */
    char            kernel_release[64];
    char            cgroup_root[128];
} AgentCapabilities;

/* Response struct */
typedef struct {
    int                   version;
    char                  request_id[64];
    char                  vm_id[64];
    AgentResourceOp       operation;
    AgentStatusCode       status_code;
    char                  status_msg[32];     /* "SUCCESS", "UNSUPPORTED", etc. */
    int                   verified;           /* 1 = Confirmed via OS readback */
    char                  error_message[256];
    AgentCgroupStatus     applied;
    AgentCapabilities     capabilities;
    /* Phase 5 Application Response Parameters */
    char                  app_id[64];
    pid_t                 app_pid;
    uint64_t              app_start_time;
    int                   app_exit_code;
    AgentAppDiscoveryList discovery;
} AgentResourceResponse;

/* RPC Serialization and Parsing */
int agent_rpc_serialize_request(const AgentResourceRequest *req, char *out_buf, size_t out_sz);
int agent_rpc_deserialize_request(const char *json_str, AgentResourceRequest *out_req);
int agent_rpc_serialize_response(const AgentResourceResponse *resp, char *out_buf, size_t out_sz);
int agent_rpc_deserialize_response(const char *json_str, AgentResourceResponse *out_resp);

const char *agent_resource_op_name(AgentResourceOp op);
AgentResourceOp agent_resource_op_from_name(const char *name);
const char *agent_status_code_name(AgentStatusCode code);
AgentStatusCode agent_status_code_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* VOS_AGENT_PROTOCOL_H */
