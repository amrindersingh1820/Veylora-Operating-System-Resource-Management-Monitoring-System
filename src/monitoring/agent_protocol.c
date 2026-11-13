/* agent_protocol.c – Telemetry Protocol Serialization and Deserialization
 *
 * Implements lightweight, zero-heap-allocation JSON serialization and
 * robust parsing for guest telemetry payloads.
 */

#include "agent_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ Helpers */

static void escape_json_str(const char *in, char *out, size_t out_sz)
{
    if (!in || !out || out_sz == 0) return;
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 2 < out_sz; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            if (o + 2 >= out_sz) break;
            out[o++] = '\\';
            out[o++] = c;
        } else if (c == '\n') {
            if (o + 2 >= out_sz) break;
            out[o++] = '\\'; out[o++] = 'n';
        } else if (c == '\r') {
            if (o + 2 >= out_sz) break;
            out[o++] = '\\'; out[o++] = 'r';
        } else if (c == '\t') {
            if (o + 2 >= out_sz) break;
            out[o++] = '\\'; out[o++] = 't';
        } else if (c >= 32 && c < 127) {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

/* ------------------------------------------------------------------ Serialization */

int agent_protocol_serialize(const GuestTelemetryPayload *p,
                             char *buf, size_t sz)
{
    if (!p || !buf || sz == 0) return -1;

    char escaped_vmid[128];
    escape_json_str(p->vm_id, escaped_vmid, sizeof(escaped_vmid));

    int offset = snprintf(buf, sz,
        "{\"version\":%d,\"vm_id\":\"%s\",\"timestamp_ms\":%llu,\"sequence\":%llu,"
        "\"cpu\":{\"total_percent\":%.2f,\"user_percent\":%.2f,\"system_percent\":%.2f,"
        "\"idle_percent\":%.2f,\"iowait_percent\":%.2f,\"core_count\":%d,\"sample_interval_ms\":%llu,\"cores\":[",
        p->version, escaped_vmid,
        (unsigned long long)p->timestamp_ms,
        (unsigned long long)p->sequence,
        p->cpu.total_percent, p->cpu.user_percent, p->cpu.system_percent,
        p->cpu.idle_percent, p->cpu.iowait_percent,
        p->cpu.core_count,
        (unsigned long long)p->cpu.sample_interval_ms);

    if (offset < 0 || (size_t)offset >= sz) return -1;

    for (int i = 0; i < p->cpu.core_count && i < AGENT_MAX_CORES; i++) {
        int n = snprintf(buf + offset, sz - offset, "%s%.2f",
                         (i > 0) ? "," : "", p->cpu.core_percents[i]);
        if (n < 0 || (size_t)(offset + n) >= sz) return -1;
        offset += n;
    }

    char escaped_primary_disk[64];
    escape_json_str(p->disk.primary_disk[0] ? p->disk.primary_disk : "vda",
                    escaped_primary_disk, sizeof(escaped_primary_disk));

    char escaped_iface[64];
    escape_json_str(p->network.iface_name[0] ? p->network.iface_name : "eth0",
                    escaped_iface, sizeof(escaped_iface));

    int total_procs = (p->total_process_count > 0) ? p->total_process_count : p->process_count;
    uint64_t proc_ts = (p->process_snapshot_timestamp_ms > 0) ? p->process_snapshot_timestamp_ms : p->timestamp_ms;

    int mem_net_off = snprintf(buf + offset, sz - offset,
        "],\"ticks\":{\"user\":%llu,\"nice\":%llu,\"system\":%llu,\"idle\":%llu,\"iowait\":%llu}},"
        "\"memory\":{\"total_bytes\":%llu,\"used_bytes\":%llu,\"free_bytes\":%llu,"
        "\"available_bytes\":%llu,\"buffers_bytes\":%llu,\"cached_bytes\":%llu,"
        "\"swap_total\":%llu,\"swap_used\":%llu},"
        "\"disk\":{\"read_bytes\":%llu,\"write_bytes\":%llu,\"read_ops\":%llu,\"write_ops\":%llu,"
        "\"io_in_flight\":%llu,\"read_bytes_per_sec\":%.2f,\"write_bytes_per_sec\":%.2f,\"primary_disk\":\"%s\"},"
        "\"network\":{\"rx_bytes\":%llu,\"tx_bytes\":%llu,\"rx_packets\":%llu,\"tx_packets\":%llu,"
        "\"rx_errors\":%llu,\"tx_errors\":%llu,\"rx_bytes_per_sec\":%.2f,\"tx_bytes_per_sec\":%.2f,"
        "\"iface\":\"%s\",\"interface_count\":%d},"
        "\"total_process_count\":%d,\"process_snapshot_timestamp_ms\":%llu,"
        "\"processes\":[",
        (unsigned long long)p->cpu.user_ticks,
        (unsigned long long)p->cpu.nice_ticks,
        (unsigned long long)p->cpu.system_ticks,
        (unsigned long long)p->cpu.idle_ticks,
        (unsigned long long)p->cpu.iowait_ticks,
        (unsigned long long)p->memory.total_bytes,
        (unsigned long long)p->memory.used_bytes,
        (unsigned long long)p->memory.free_bytes,
        (unsigned long long)p->memory.available_bytes,
        (unsigned long long)p->memory.buffers_bytes,
        (unsigned long long)p->memory.cached_bytes,
        (unsigned long long)p->memory.swap_total_bytes,
        (unsigned long long)p->memory.swap_used_bytes,
        (unsigned long long)p->disk.read_bytes,
        (unsigned long long)p->disk.write_bytes,
        (unsigned long long)p->disk.read_ops,
        (unsigned long long)p->disk.write_ops,
        (unsigned long long)p->disk.io_in_progress,
        p->disk.read_bytes_per_sec,
        p->disk.write_bytes_per_sec,
        escaped_primary_disk,
        (unsigned long long)p->network.rx_bytes,
        (unsigned long long)p->network.tx_bytes,
        (unsigned long long)p->network.rx_packets,
        (unsigned long long)p->network.tx_packets,
        (unsigned long long)p->network.rx_errors,
        (unsigned long long)p->network.tx_errors,
        p->network.rx_bytes_per_sec,
        p->network.tx_bytes_per_sec,
        escaped_iface,
        p->network.interface_count,
        total_procs,
        (unsigned long long)proc_ts);

    if (mem_net_off < 0 || (size_t)(offset + mem_net_off) >= sz) return -1;
    offset += mem_net_off;

    /* Append process list */
    for (int i = 0; i < p->process_count && i < AGENT_MAX_PROCESSES; i++) {
        char escaped_name[128];
        escape_json_str(p->processes[i].name, escaped_name, sizeof(escaped_name));

        int pn = snprintf(buf + offset, sz - offset,
            "%s{\"pid\":%d,\"ppid\":%d,\"name\":\"%s\",\"state\":\"%s\","
            "\"cpu\":%.2f,\"rss\":%llu,\"virt\":%llu,\"start\":%llu}",
            (i > 0) ? "," : "",
            (int)p->processes[i].pid,
            (int)p->processes[i].ppid,
            escaped_name,
            p->processes[i].state[0] ? p->processes[i].state : "S",
            p->processes[i].cpu_percent,
            (unsigned long long)p->processes[i].memory_rss_bytes,
            (unsigned long long)p->processes[i].memory_virt_bytes,
            (unsigned long long)p->processes[i].start_time_s);

        if (pn < 0 || (size_t)(offset + pn) >= sz) return -1;
        offset += pn;
    }

    if ((size_t)(offset + 2) >= sz) return -1;
    buf[offset++] = ']';
    buf[offset++] = '}';
    buf[offset]   = '\0';

    return offset;
}

/* ------------------------------------------------------------------ Deserialization */

/* Simple token search helpers */
static const char *find_key(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) {
        p++;
    }
    return p;
}

static int parse_double_field(const char *json, const char *key, double *out_val)
{
    const char *p = find_key(json, key);
    if (!p) return -1;
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return -1;
    if (out_val) *out_val = v;
    return 0;
}

static int parse_uint64_field(const char *json, const char *key, uint64_t *out_val)
{
    const char *p = find_key(json, key);
    if (!p) return -1;
    char *end = NULL;
    unsigned long long v = strtoull(p, &end, 10);
    if (end == p) return -1;
    if (out_val) *out_val = (uint64_t)v;
    return 0;
}

static int parse_int_field(const char *json, const char *key, int *out_val)
{
    const char *p = find_key(json, key);
    if (!p) return -1;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return -1;
    if (out_val) *out_val = (int)v;
    return 0;
}

static int parse_int64_field(const char *json, const char *key, int64_t *out_val)
{
    const char *p = find_key(json, key);
    if (!p) return -1;
    char *end = NULL;
    long long v = strtoll(p, &end, 10);
    if (end == p) return -1;
    if (out_val) *out_val = (int64_t)v;
    return 0;
}

static int parse_bool_field(const char *json, const char *key, int *out_val)
{
    const char *p = find_key(json, key);
    if (!p) return -1;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "true", 4) == 0) {
        if (out_val) *out_val = 1;
        return 0;
    } else if (strncmp(p, "false", 5) == 0) {
        if (out_val) *out_val = 0;
        return 0;
    } else if (*p == '1') {
        if (out_val) *out_val = 1;
        return 0;
    } else if (*p == '0') {
        if (out_val) *out_val = 0;
        return 0;
    }
    return -1;
}

static int parse_string_field(const char *json, const char *key, char *out_buf, size_t out_sz)
{
    if (!out_buf || out_sz == 0) return -1;
    out_buf[0] = '\0';
    const char *p = find_key(json, key);
    if (!p || *p != '"') return -1;
    p++; /* skip opening quote */
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < out_sz) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            if (*p == 'n') out_buf[o++] = '\n';
            else if (*p == 'r') out_buf[o++] = '\r';
            else if (*p == 't') out_buf[o++] = '\t';
            else out_buf[o++] = *p;
        } else {
            out_buf[o++] = *p;
        }
        p++;
    }
    out_buf[o] = '\0';
    return 0;
}

int agent_protocol_deserialize(const char *json, GuestTelemetryPayload *p)
{
    if (!json || !p) return -1;
    memset(p, 0, sizeof(*p));

    /* Version check */
    int ver = 0;
    if (parse_int_field(json, "version", &ver) != 0) return -1;
    if (ver != AGENT_PROTOCOL_VERSION) return -2;
    p->version = ver;

    parse_string_field(json, "vm_id", p->vm_id, sizeof(p->vm_id));
    parse_uint64_field(json, "timestamp_ms", &p->timestamp_ms);
    parse_uint64_field(json, "sequence", &p->sequence);
    parse_int_field(json, "total_process_count", &p->total_process_count);
    parse_uint64_field(json, "process_snapshot_timestamp_ms", &p->process_snapshot_timestamp_ms);

    /* CPU block */
    const char *cpu_obj = find_key(json, "cpu");
    if (cpu_obj && *cpu_obj == '{') {
        parse_double_field(cpu_obj, "total_percent", &p->cpu.total_percent);
        parse_double_field(cpu_obj, "user_percent", &p->cpu.user_percent);
        parse_double_field(cpu_obj, "system_percent", &p->cpu.system_percent);
        parse_double_field(cpu_obj, "idle_percent", &p->cpu.idle_percent);
        parse_double_field(cpu_obj, "iowait_percent", &p->cpu.iowait_percent);
        parse_int_field(cpu_obj, "core_count", &p->cpu.core_count);
        parse_uint64_field(cpu_obj, "sample_interval_ms", &p->cpu.sample_interval_ms);

        /* Parse cores array */
        const char *cores_arr = find_key(cpu_obj, "cores");
        if (cores_arr && *cores_arr == '[') {
            cores_arr++;
            int c_idx = 0;
            while (*cores_arr && *cores_arr != ']' && c_idx < AGENT_MAX_CORES) {
                while (*cores_arr == ' ' || *cores_arr == ',' || *cores_arr == '\t') cores_arr++;
                if (*cores_arr == ']') break;
                char *end = NULL;
                double cv = strtod(cores_arr, &end);
                if (end > cores_arr) {
                    p->cpu.core_percents[c_idx++] = cv;
                    cores_arr = end;
                } else {
                    break;
                }
            }
        }

        /* CPU ticks */
        const char *ticks_obj = find_key(cpu_obj, "ticks");
        if (ticks_obj && *ticks_obj == '{') {
            parse_uint64_field(ticks_obj, "user", &p->cpu.user_ticks);
            parse_uint64_field(ticks_obj, "nice", &p->cpu.nice_ticks);
            parse_uint64_field(ticks_obj, "system", &p->cpu.system_ticks);
            parse_uint64_field(ticks_obj, "idle", &p->cpu.idle_ticks);
            parse_uint64_field(ticks_obj, "iowait", &p->cpu.iowait_ticks);
        }
    }

    /* Memory block */
    const char *mem_obj = find_key(json, "memory");
    if (mem_obj && *mem_obj == '{') {
        parse_uint64_field(mem_obj, "total_bytes", &p->memory.total_bytes);
        parse_uint64_field(mem_obj, "used_bytes", &p->memory.used_bytes);
        parse_uint64_field(mem_obj, "free_bytes", &p->memory.free_bytes);
        parse_uint64_field(mem_obj, "available_bytes", &p->memory.available_bytes);
        parse_uint64_field(mem_obj, "buffers_bytes", &p->memory.buffers_bytes);
        parse_uint64_field(mem_obj, "cached_bytes", &p->memory.cached_bytes);
        parse_uint64_field(mem_obj, "swap_total", &p->memory.swap_total_bytes);
        parse_uint64_field(mem_obj, "swap_used", &p->memory.swap_used_bytes);
    }

    /* Disk block */
    const char *disk_obj = find_key(json, "disk");
    if (disk_obj && *disk_obj == '{') {
        parse_uint64_field(disk_obj, "read_bytes", &p->disk.read_bytes);
        parse_uint64_field(disk_obj, "write_bytes", &p->disk.write_bytes);
        parse_uint64_field(disk_obj, "read_ops", &p->disk.read_ops);
        parse_uint64_field(disk_obj, "write_ops", &p->disk.write_ops);
        parse_uint64_field(disk_obj, "io_in_flight", &p->disk.io_in_progress);
        parse_double_field(disk_obj, "read_bytes_per_sec", &p->disk.read_bytes_per_sec);
        parse_double_field(disk_obj, "write_bytes_per_sec", &p->disk.write_bytes_per_sec);
        parse_string_field(disk_obj, "primary_disk", p->disk.primary_disk, sizeof(p->disk.primary_disk));
    }

    /* Network block */
    const char *net_obj = find_key(json, "network");
    if (net_obj && *net_obj == '{') {
        parse_uint64_field(net_obj, "rx_bytes", &p->network.rx_bytes);
        parse_uint64_field(net_obj, "tx_bytes", &p->network.tx_bytes);
        parse_uint64_field(net_obj, "rx_packets", &p->network.rx_packets);
        parse_uint64_field(net_obj, "tx_packets", &p->network.tx_packets);
        parse_uint64_field(net_obj, "rx_errors", &p->network.rx_errors);
        parse_uint64_field(net_obj, "tx_errors", &p->network.tx_errors);
        parse_double_field(net_obj, "rx_bytes_per_sec", &p->network.rx_bytes_per_sec);
        parse_double_field(net_obj, "tx_bytes_per_sec", &p->network.tx_bytes_per_sec);
        parse_string_field(net_obj, "iface", p->network.iface_name, sizeof(p->network.iface_name));
        parse_int_field(net_obj, "interface_count", &p->network.interface_count);
    }

    /* Processes array */
    const char *proc_arr = find_key(json, "processes");
    if (proc_arr && *proc_arr == '[') {
        proc_arr++;
        int count = 0;
        while (*proc_arr && *proc_arr != ']' && count < AGENT_MAX_PROCESSES) {
            const char *elem_start = strchr(proc_arr, '{');
            if (!elem_start) break;
            const char *elem_end = strchr(elem_start, '}');
            if (!elem_end) break;

            GuestProcessMetric *m = &p->processes[count];
            parse_int_field(elem_start, "pid", (int *)&m->pid);
            parse_int_field(elem_start, "ppid", (int *)&m->ppid);
            parse_string_field(elem_start, "name", m->name, sizeof(m->name));
            parse_string_field(elem_start, "state", m->state, sizeof(m->state));
            parse_double_field(elem_start, "cpu", &m->cpu_percent);
            parse_uint64_field(elem_start, "rss", &m->memory_rss_bytes);
            parse_uint64_field(elem_start, "virt", &m->memory_virt_bytes);
            parse_uint64_field(elem_start, "start", &m->start_time_s);

            count++;
            proc_arr = elem_end + 1;
        }
        p->process_count = count;
    }

    if (p->total_process_count == 0 && p->process_count > 0) {
        p->total_process_count = p->process_count;
    }
    if (p->process_snapshot_timestamp_ms == 0) {
        p->process_snapshot_timestamp_ms = p->timestamp_ms;
    }

    return 0;
}

/* ------------------------------------------------------------------ Phase 4: RPC Protocol Implementation */

const char *agent_resource_op_name(AgentResourceOp op)
{
    switch (op) {
        case AGENT_OP_GET_CAPABILITIES: return "GET_CAPABILITIES";
        case AGENT_OP_CREATE_GROUP:      return "CREATE_GROUP";
        case AGENT_OP_SET_CPU_LIMIT:     return "SET_CPU_LIMIT";
        case AGENT_OP_SET_CPU_WEIGHT:    return "SET_CPU_WEIGHT";
        case AGENT_OP_SET_MEMORY_LIMIT:  return "SET_MEMORY_LIMIT";
        case AGENT_OP_SET_PIDS_LIMIT:    return "SET_PIDS_LIMIT";
        case AGENT_OP_SET_IO_LIMIT:      return "SET_IO_LIMIT";
        case AGENT_OP_ATTACH_PROCESS:    return "ATTACH_PROCESS";
        case AGENT_OP_GET_GROUP_STATUS:  return "GET_GROUP_STATUS";
        case AGENT_OP_REMOVE_GROUP:      return "REMOVE_GROUP";
        case AGENT_OP_DISCOVER_APPS:     return "DISCOVER_APPS";
        case AGENT_OP_LAUNCH_APP:        return "LAUNCH_APP";
        case AGENT_OP_STOP_APP:          return "STOP_APP";
        case AGENT_OP_GET_APP_METRICS:   return "GET_APP_METRICS";
        default:                         return "UNKNOWN";
    }
}

AgentResourceOp agent_resource_op_from_name(const char *name)
{
    if (!name) return AGENT_OP_UNKNOWN;
    if (strcmp(name, "GET_CAPABILITIES") == 0) return AGENT_OP_GET_CAPABILITIES;
    if (strcmp(name, "CREATE_GROUP") == 0)      return AGENT_OP_CREATE_GROUP;
    if (strcmp(name, "SET_CPU_LIMIT") == 0)     return AGENT_OP_SET_CPU_LIMIT;
    if (strcmp(name, "SET_CPU_WEIGHT") == 0)    return AGENT_OP_SET_CPU_WEIGHT;
    if (strcmp(name, "SET_MEMORY_LIMIT") == 0)  return AGENT_OP_SET_MEMORY_LIMIT;
    if (strcmp(name, "SET_PIDS_LIMIT") == 0)    return AGENT_OP_SET_PIDS_LIMIT;
    if (strcmp(name, "SET_IO_LIMIT") == 0)      return AGENT_OP_SET_IO_LIMIT;
    if (strcmp(name, "ATTACH_PROCESS") == 0)    return AGENT_OP_ATTACH_PROCESS;
    if (strcmp(name, "GET_GROUP_STATUS") == 0)  return AGENT_OP_GET_GROUP_STATUS;
    if (strcmp(name, "REMOVE_GROUP") == 0)      return AGENT_OP_REMOVE_GROUP;
    if (strcmp(name, "DISCOVER_APPS") == 0)     return AGENT_OP_DISCOVER_APPS;
    if (strcmp(name, "LAUNCH_APP") == 0)        return AGENT_OP_LAUNCH_APP;
    if (strcmp(name, "STOP_APP") == 0)          return AGENT_OP_STOP_APP;
    if (strcmp(name, "GET_APP_METRICS") == 0)   return AGENT_OP_GET_APP_METRICS;
    return AGENT_OP_UNKNOWN;
}

const char *agent_status_code_name(AgentStatusCode code)
{
    switch (code) {
        case AGENT_STATUS_SUCCESS:              return "SUCCESS";
        case AGENT_STATUS_INVALID_CONFIG:       return "INVALID_CONFIGURATION";
        case AGENT_STATUS_UNSUPPORTED:          return "UNSUPPORTED_BY_GUEST_KERNEL";
        case AGENT_STATUS_PERMISSION_DENIED:    return "PERMISSION_DENIED";
        case AGENT_STATUS_GROUP_NOT_FOUND:      return "GROUP_NOT_FOUND";
        case AGENT_STATUS_PROCESS_NOT_FOUND:    return "PROCESS_NOT_FOUND";
        case AGENT_STATUS_COMMUNICATION_FAILED: return "COMMUNICATION_FAILED";
        case AGENT_STATUS_VERIFICATION_FAILED:  return "VERIFICATION_FAILED";
        case AGENT_STATUS_LIMIT_BELOW_USAGE:    return "LIMIT_BELOW_CURRENT_USAGE";
        case AGENT_STATUS_GROUP_BUSY:           return "GROUP_BUSY";
        case AGENT_STATUS_FAILED:               return "FAILED";
        case AGENT_STATUS_PROCESS_IDENTITY_CHANGED: return "PROCESS_IDENTITY_CHANGED";
        case AGENT_STATUS_UNSUPPORTED_BY_HOST_HYPERVISOR: return "UNSUPPORTED_BY_HOST_HYPERVISOR";
        case AGENT_STATUS_UNSUPPORTED_PLATFORM: return "UNSUPPORTED_PLATFORM";
        case AGENT_STATUS_UNSUPPORTED_HARDWARE: return "UNSUPPORTED_HARDWARE";
        case AGENT_STATUS_APP_NOT_FOUND:        return "APP_NOT_FOUND";
        case AGENT_STATUS_APP_ALREADY_RUNNING:  return "APP_ALREADY_RUNNING";
        case AGENT_STATUS_LAUNCH_FAILED:        return "LAUNCH_FAILED";
        case AGENT_STATUS_CGROUP_ATTACH_FAILED: return "CGROUP_ATTACH_FAILED";
        default:                                return "UNKNOWN";
    }
}

AgentStatusCode agent_status_code_from_name(const char *name)
{
    if (!name) return AGENT_STATUS_FAILED;
    if (strcmp(name, "SUCCESS") == 0)                       return AGENT_STATUS_SUCCESS;
    if (strcmp(name, "INVALID_CONFIGURATION") == 0)         return AGENT_STATUS_INVALID_CONFIG;
    if (strcmp(name, "UNSUPPORTED_BY_GUEST_KERNEL") == 0)   return AGENT_STATUS_UNSUPPORTED;
    if (strcmp(name, "PERMISSION_DENIED") == 0)             return AGENT_STATUS_PERMISSION_DENIED;
    if (strcmp(name, "GROUP_NOT_FOUND") == 0)               return AGENT_STATUS_GROUP_NOT_FOUND;
    if (strcmp(name, "PROCESS_NOT_FOUND") == 0)             return AGENT_STATUS_PROCESS_NOT_FOUND;
    if (strcmp(name, "COMMUNICATION_FAILED") == 0)          return AGENT_STATUS_COMMUNICATION_FAILED;
    if (strcmp(name, "VERIFICATION_FAILED") == 0)           return AGENT_STATUS_VERIFICATION_FAILED;
    if (strcmp(name, "LIMIT_BELOW_CURRENT_USAGE") == 0)     return AGENT_STATUS_LIMIT_BELOW_USAGE;
    if (strcmp(name, "GROUP_BUSY") == 0)                    return AGENT_STATUS_GROUP_BUSY;
    if (strcmp(name, "PROCESS_IDENTITY_CHANGED") == 0)      return AGENT_STATUS_PROCESS_IDENTITY_CHANGED;
    if (strcmp(name, "UNSUPPORTED_BY_HOST_HYPERVISOR") == 0) return AGENT_STATUS_UNSUPPORTED_BY_HOST_HYPERVISOR;
    if (strcmp(name, "UNSUPPORTED_PLATFORM") == 0)          return AGENT_STATUS_UNSUPPORTED_PLATFORM;
    if (strcmp(name, "UNSUPPORTED_HARDWARE") == 0)          return AGENT_STATUS_UNSUPPORTED_HARDWARE;
    if (strcmp(name, "APP_NOT_FOUND") == 0)                 return AGENT_STATUS_APP_NOT_FOUND;
    if (strcmp(name, "APP_ALREADY_RUNNING") == 0)           return AGENT_STATUS_APP_ALREADY_RUNNING;
    if (strcmp(name, "LAUNCH_FAILED") == 0)                 return AGENT_STATUS_LAUNCH_FAILED;
    if (strcmp(name, "CGROUP_ATTACH_FAILED") == 0)          return AGENT_STATUS_CGROUP_ATTACH_FAILED;
    return AGENT_STATUS_FAILED;
}

int agent_rpc_serialize_request(const AgentResourceRequest *req, char *out_buf, size_t out_sz)
{
    if (!req || !out_buf || out_sz == 0) return -1;

    char esc_req_id[128];
    char esc_vm_id[128];
    char esc_grp_id[128];
    char esc_grp_name[128];
    char esc_app_id[128];
    char esc_app_path[512];
    char esc_app_args[1024];
    escape_json_str(req->request_id, esc_req_id, sizeof(esc_req_id));
    escape_json_str(req->vm_id, esc_vm_id, sizeof(esc_vm_id));
    escape_json_str(req->group_id, esc_grp_id, sizeof(esc_grp_id));
    escape_json_str(req->group_name, esc_grp_name, sizeof(esc_grp_name));
    escape_json_str(req->app_id, esc_app_id, sizeof(esc_app_id));
    escape_json_str(req->app_path, esc_app_path, sizeof(esc_app_path));
    escape_json_str(req->app_args, esc_app_args, sizeof(esc_app_args));

    int n = snprintf(out_buf, out_sz,
        "{\"version\":%d,\"request_id\":\"%s\",\"vm_id\":\"%s\",\"operation\":\"%s\","
        "\"group_id\":\"%s\",\"group_name\":\"%s\","
        "\"cpu_quota_us\":%lld,\"cpu_period_us\":%lld,\"cpu_cores\":%.2f,\"cpu_weight\":%d,"
        "\"memory_max_bytes\":%llu,\"pids_max\":%lld,\"io_read_bps\":%llu,\"io_write_bps\":%llu,"
        "\"pid\":%d,\"expected_start_time\":%llu,"
        "\"app_id\":\"%s\",\"app_path\":\"%s\",\"app_args\":\"%s\",\"app_timeout_ms\":%d,\"app_signal\":%d}",
        req->version ? req->version : AGENT_PROTOCOL_VERSION,
        esc_req_id, esc_vm_id, agent_resource_op_name(req->operation),
        esc_grp_id, esc_grp_name,
        (long long)req->cpu_quota_us, (long long)req->cpu_period_us,
        req->cpu_cores, req->cpu_weight,
        (unsigned long long)req->memory_max_bytes, (long long)req->pids_max,
        (unsigned long long)req->io_read_bps, (unsigned long long)req->io_write_bps,
        (int)req->pid, (unsigned long long)req->expected_start_time,
        esc_app_id, esc_app_path, esc_app_args, req->app_timeout_ms, req->app_signal);

    if (n < 0 || (size_t)n >= out_sz) return -1;
    return n;
}

int agent_rpc_deserialize_request(const char *json, AgentResourceRequest *req)
{
    if (!json || !req) return -1;
    memset(req, 0, sizeof(*req));

    int ver = 0;
    if (parse_int_field(json, "version", &ver) != 0) ver = AGENT_PROTOCOL_VERSION;
    req->version = ver;

    parse_string_field(json, "request_id", req->request_id, sizeof(req->request_id));
    parse_string_field(json, "vm_id", req->vm_id, sizeof(req->vm_id));

    char op_str[64] = {0};
    if (parse_string_field(json, "operation", op_str, sizeof(op_str)) == 0) {
        req->operation = agent_resource_op_from_name(op_str);
    }
    parse_string_field(json, "group_id", req->group_id, sizeof(req->group_id));
    parse_string_field(json, "group_name", req->group_name, sizeof(req->group_name));

    /* Parameters */
    parse_int64_field(json, "cpu_quota_us", &req->cpu_quota_us);
    parse_int64_field(json, "cpu_period_us", &req->cpu_period_us);
    parse_double_field(json, "cpu_cores", &req->cpu_cores);
    parse_int_field(json, "cpu_weight", &req->cpu_weight);
    parse_uint64_field(json, "memory_max_bytes", &req->memory_max_bytes);
    parse_int64_field(json, "pids_max", &req->pids_max);
    parse_uint64_field(json, "io_read_bps", &req->io_read_bps);
    parse_uint64_field(json, "io_write_bps", &req->io_write_bps);
    parse_uint64_field(json, "expected_start_time", &req->expected_start_time);
    int p = 0;
    if (parse_int_field(json, "pid", &p) == 0) {
        req->pid = (pid_t)p;
    }

    /* Phase 5 Application Fields */
    parse_string_field(json, "app_id", req->app_id, sizeof(req->app_id));
    parse_string_field(json, "app_path", req->app_path, sizeof(req->app_path));
    parse_string_field(json, "app_args", req->app_args, sizeof(req->app_args));
    parse_int_field(json, "app_timeout_ms", &req->app_timeout_ms);
    parse_int_field(json, "app_signal", &req->app_signal);

    return 0;
}

static void parse_discovered_apps(const char *json, AgentAppDiscoveryList *list)
{
    if (!json || !list) return;
    list->count = 0;
    const char *arr = find_key(json, "discovered_apps");
    if (!arr || *arr != '[') return;
    arr++;

    while (*arr && *arr != ']' && list->count < AGENT_MAX_DISCOVERED_APPS) {
        while (*arr && *arr != '{' && *arr != ']') arr++;
        if (*arr != '{') break;
        const char *obj_start = arr;
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;

        size_t obj_len = (size_t)(obj_end - obj_start + 1);
        char obj_buf[1024];
        if (obj_len < sizeof(obj_buf)) {
            memcpy(obj_buf, obj_start, obj_len);
            obj_buf[obj_len] = '\0';

            AgentDiscoveredApp *item = &list->apps[list->count];
            memset(item, 0, sizeof(*item));
            parse_string_field(obj_buf, "name", item->name, sizeof(item->name));
            parse_string_field(obj_buf, "path", item->path, sizeof(item->path));
            parse_bool_field(obj_buf, "executable", &item->is_executable);
            int m = 0;
            if (parse_int_field(obj_buf, "mode", &m) == 0) item->mode = (uint32_t)m;
            parse_string_field(obj_buf, "package", item->package, sizeof(item->package));
            list->count++;
        }
        arr = obj_end + 1;
    }
}

int agent_rpc_serialize_response(const AgentResourceResponse *resp, char *out_buf, size_t out_sz)
{
    if (!resp || !out_buf || out_sz == 0) return -1;

    char esc_req_id[128], esc_vm_id[128], esc_err[512], esc_grp[128], esc_app[128];
    escape_json_str(resp->request_id, esc_req_id, sizeof(esc_req_id));
    escape_json_str(resp->vm_id, esc_vm_id, sizeof(esc_vm_id));
    escape_json_str(resp->error_message, esc_err, sizeof(esc_err));
    escape_json_str(resp->applied.group_id, esc_grp, sizeof(esc_grp));
    escape_json_str(resp->app_id, esc_app, sizeof(esc_app));

    char pids_buf[512] = "[";
    size_t plen = 1;
    for (int i = 0; i < resp->applied.attached_pids_count && i < 64; i++) {
        plen += snprintf(pids_buf + plen, sizeof(pids_buf) - plen,
                         i == 0 ? "%d" : ",%d", (int)resp->applied.attached_pids[i]);
    }
    if (plen < sizeof(pids_buf) - 2) {
        pids_buf[plen++] = ']';
        pids_buf[plen] = '\0';
    } else {
        strcat(pids_buf, "]");
    }

    char disc_buf[16384] = "[";
    size_t dlen = 1;
    for (int i = 0; i < resp->discovery.count && i < AGENT_MAX_DISCOVERED_APPS; i++) {
        char en[128], ep[512], epkg[128];
        escape_json_str(resp->discovery.apps[i].name, en, sizeof(en));
        escape_json_str(resp->discovery.apps[i].path, ep, sizeof(ep));
        escape_json_str(resp->discovery.apps[i].package, epkg, sizeof(epkg));
        int wn = snprintf(disc_buf + dlen, sizeof(disc_buf) - dlen,
                          "%s{\"name\":\"%s\",\"path\":\"%s\",\"executable\":%s,\"mode\":%u,\"package\":\"%s\"}",
                          (i == 0 ? "" : ","), en, ep,
                          resp->discovery.apps[i].is_executable ? "true" : "false",
                          (unsigned int)resp->discovery.apps[i].mode,
                          epkg);
        if (wn > 0 && dlen + (size_t)wn < sizeof(disc_buf) - 2) {
            dlen += (size_t)wn;
        } else {
            break;
        }
    }
    if (dlen < sizeof(disc_buf) - 1) {
        disc_buf[dlen++] = ']';
        disc_buf[dlen] = '\0';
    } else {
        disc_buf[sizeof(disc_buf) - 2] = ']';
        disc_buf[sizeof(disc_buf) - 1] = '\0';
    }

    int n = snprintf(out_buf, out_sz,
        "{\"version\":%d,\"request_id\":\"%s\",\"vm_id\":\"%s\",\"operation\":\"%s\","
        "\"status_code\":%d,\"status_msg\":\"%s\",\"verified\":%s,\"error_message\":\"%s\","
        "\"applied\":{\"group_id\":\"%s\",\"exists\":%s,\"cpu_quota_us\":%lld,\"cpu_period_us\":%lld,"
        "\"cpu_weight\":%d,\"cpu_usage_usec\":%llu,\"cpu_throttled_usec\":%llu,\"memory_max_bytes\":%llu,\"memory_current_bytes\":%llu,"
        "\"memory_peak_bytes\":%llu,\"memory_oom_events\":%llu,\"pids_max\":%lld,\"pids_current\":%lld,\"pids_events\":%lld,"
        "\"io_read_bps\":%llu,\"io_write_bps\":%llu,\"io_read_bytes\":%llu,\"io_write_bytes\":%llu,\"attached_pids\":%s},"
        "\"capabilities\":{\"cgroup_v2_supported\":%s,\"cgroup_v2_mounted\":%s,\"cpu_controller\":%s,"
        "\"memory_controller\":%s,\"pids_controller\":%s,\"io_controller\":%s,\"gpu_supported\":%s,"
        "\"gpu_status\":\"%s\",\"kernel_release\":\"%s\",\"cgroup_root\":\"%s\"},"
        "\"app_id\":\"%s\",\"app_pid\":%d,\"app_start_time\":%llu,\"app_exit_code\":%d,"
        "\"discovered_apps\":%s}",
        resp->version ? resp->version : AGENT_PROTOCOL_VERSION,
        esc_req_id, esc_vm_id, agent_resource_op_name(resp->operation),
        (int)resp->status_code, resp->status_msg[0] ? resp->status_msg : agent_status_code_name(resp->status_code),
        resp->verified ? "true" : "false", esc_err,
        esc_grp, resp->applied.exists ? "true" : "false",
        (long long)resp->applied.cpu_quota_us, (long long)resp->applied.cpu_period_us,
        resp->applied.cpu_weight, (unsigned long long)resp->applied.cpu_usage_usec,
        (unsigned long long)resp->applied.cpu_throttled_usec,
        (unsigned long long)resp->applied.memory_max_bytes, (unsigned long long)resp->applied.memory_current_bytes,
        (unsigned long long)resp->applied.memory_peak_bytes,
        (unsigned long long)resp->applied.memory_oom_events,
        (long long)resp->applied.pids_max, (long long)resp->applied.pids_current, (long long)resp->applied.pids_events,
        (unsigned long long)resp->applied.io_read_bps, (unsigned long long)resp->applied.io_write_bps,
        (unsigned long long)resp->applied.io_read_bytes, (unsigned long long)resp->applied.io_write_bytes,
        pids_buf,
        resp->capabilities.cgroup_v2_supported ? "true" : "false",
        resp->capabilities.cgroup_v2_mounted ? "true" : "false",
        resp->capabilities.cpu_controller ? "true" : "false",
        resp->capabilities.memory_controller ? "true" : "false",
        resp->capabilities.pids_controller ? "true" : "false",
        resp->capabilities.io_controller ? "true" : "false",
        resp->capabilities.gpu_supported ? "true" : "false",
        resp->capabilities.gpu_status[0] ? resp->capabilities.gpu_status : "UNSUPPORTED_BY_HOST_HYPERVISOR",
        resp->capabilities.kernel_release,
        resp->capabilities.cgroup_root,
        esc_app, (int)resp->app_pid, (unsigned long long)resp->app_start_time, resp->app_exit_code,
        disc_buf);

    if (n < 0 || (size_t)n >= out_sz) return -1;
    return n;
}

int agent_rpc_deserialize_response(const char *json, AgentResourceResponse *resp)
{
    if (!json || !resp) return -1;
    memset(resp, 0, sizeof(*resp));

    int ver = 0;
    if (parse_int_field(json, "version", &ver) != 0) ver = AGENT_PROTOCOL_VERSION;
    resp->version = ver;

    parse_string_field(json, "request_id", resp->request_id, sizeof(resp->request_id));
    parse_string_field(json, "vm_id", resp->vm_id, sizeof(resp->vm_id));

    char op_str[64] = {0};
    if (parse_string_field(json, "operation", op_str, sizeof(op_str)) == 0) {
        resp->operation = agent_resource_op_from_name(op_str);
    }

    int sc = 0;
    if (parse_int_field(json, "status_code", &sc) == 0) {
        resp->status_code = (AgentStatusCode)sc;
    }
    parse_string_field(json, "status_msg", resp->status_msg, sizeof(resp->status_msg));
    parse_bool_field(json, "verified", &resp->verified);
    resp->applied.verified = resp->verified;
    parse_string_field(json, "error_message", resp->error_message, sizeof(resp->error_message));

    /* Phase 5 Application Fields */
    parse_string_field(json, "app_id", resp->app_id, sizeof(resp->app_id));
    int apid = 0;
    if (parse_int_field(json, "app_pid", &apid) == 0) resp->app_pid = (pid_t)apid;
    parse_uint64_field(json, "app_start_time", &resp->app_start_time);
    parse_int_field(json, "app_exit_code", &resp->app_exit_code);
    parse_discovered_apps(json, &resp->discovery);

    /* Applied block */
    const char *appl_obj = find_key(json, "applied");
    if (appl_obj && *appl_obj == '{') {
        parse_string_field(appl_obj, "group_id", resp->applied.group_id, sizeof(resp->applied.group_id));
        parse_bool_field(appl_obj, "exists", &resp->applied.exists);
        parse_int64_field(appl_obj, "cpu_quota_us", &resp->applied.cpu_quota_us);
        parse_int64_field(appl_obj, "cpu_period_us", &resp->applied.cpu_period_us);
        parse_int_field(appl_obj, "cpu_weight", &resp->applied.cpu_weight);
        parse_uint64_field(appl_obj, "cpu_usage_usec", &resp->applied.cpu_usage_usec);
        parse_uint64_field(appl_obj, "cpu_throttled_usec", &resp->applied.cpu_throttled_usec);
        parse_uint64_field(appl_obj, "memory_max_bytes", &resp->applied.memory_max_bytes);
        parse_uint64_field(appl_obj, "memory_current_bytes", &resp->applied.memory_current_bytes);
        parse_uint64_field(appl_obj, "memory_peak_bytes", &resp->applied.memory_peak_bytes);
        parse_uint64_field(appl_obj, "memory_oom_events", &resp->applied.memory_oom_events);
        parse_int64_field(appl_obj, "pids_max", &resp->applied.pids_max);
        parse_int64_field(appl_obj, "pids_current", &resp->applied.pids_current);
        parse_int64_field(appl_obj, "pids_events", &resp->applied.pids_events);
        parse_uint64_field(appl_obj, "io_read_bps", &resp->applied.io_read_bps);
        parse_uint64_field(appl_obj, "io_write_bps", &resp->applied.io_write_bps);
        parse_uint64_field(appl_obj, "io_read_bytes", &resp->applied.io_read_bytes);
        parse_uint64_field(appl_obj, "io_write_bytes", &resp->applied.io_write_bytes);

        const char *pids_arr = find_key(appl_obj, "attached_pids");
        if (pids_arr && *pids_arr == '[') {
            pids_arr++;
            int pcnt = 0;
            while (*pids_arr && *pids_arr != ']' && pcnt < 64) {
                while (*pids_arr == ' ' || *pids_arr == ',' || *pids_arr == '\t') pids_arr++;
                if (*pids_arr == ']') break;
                char *end = NULL;
                long pv = strtol(pids_arr, &end, 10);
                if (end > pids_arr) {
                    resp->applied.attached_pids[pcnt++] = (pid_t)pv;
                    pids_arr = end;
                } else {
                    break;
                }
            }
            resp->applied.attached_pids_count = pcnt;
        }
    }

    /* Capabilities block */
    const char *caps_obj = find_key(json, "capabilities");
    if (caps_obj && *caps_obj == '{') {
        parse_bool_field(caps_obj, "cgroup_v2_supported", &resp->capabilities.cgroup_v2_supported);
        parse_bool_field(caps_obj, "cgroup_v2_mounted", &resp->capabilities.cgroup_v2_mounted);
        parse_bool_field(caps_obj, "cpu_controller", &resp->capabilities.cpu_controller);
        parse_bool_field(caps_obj, "memory_controller", &resp->capabilities.memory_controller);
        parse_bool_field(caps_obj, "pids_controller", &resp->capabilities.pids_controller);
        parse_bool_field(caps_obj, "io_controller", &resp->capabilities.io_controller);
        parse_bool_field(caps_obj, "gpu_supported", &resp->capabilities.gpu_supported);
        parse_string_field(caps_obj, "gpu_status", resp->capabilities.gpu_status, sizeof(resp->capabilities.gpu_status));
        parse_string_field(caps_obj, "kernel_release", resp->capabilities.kernel_release, sizeof(resp->capabilities.kernel_release));
        parse_string_field(caps_obj, "cgroup_root", resp->capabilities.cgroup_root, sizeof(resp->capabilities.cgroup_root));
    }

    return 0;
}
