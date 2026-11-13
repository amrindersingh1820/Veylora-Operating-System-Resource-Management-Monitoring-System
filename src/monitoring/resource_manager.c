/* resource_manager.c – Virtual OS Simulator Resource Management & Enforcement
 *
 * Implements VM-level hardware boundaries, guest application cgroups v2
 * enforcement via virtio-serial RPC, SQLite persistence, and audit logging.
 */

#include "resource_manager.h"
#include "vm_monitor.h"
#include "../virtualization/vm_config.h"
#include "../virtualization/vm_manager.h"
#include "sqlite3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

static sqlite3 *s_rm_db = NULL;
static char s_rm_db_path[512] = "virtual_os.db";
static pthread_mutex_t s_rm_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t s_req_seq = 1;

static uint64_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

const char *resource_result_str(ResourceResult res)
{
    switch (res) {
        case RESOURCE_SUCCESS:                  return "SUCCESS";
        case RESOURCE_ERR_INVALID_CONFIG:       return "INVALID_CONFIGURATION";
        case RESOURCE_ERR_UNSUPPORTED:          return "UNSUPPORTED_BY_GUEST_KERNEL";
        case RESOURCE_ERR_PERMISSION_DENIED:    return "PERMISSION_DENIED";
        case RESOURCE_ERR_GROUP_NOT_FOUND:      return "GROUP_NOT_FOUND";
        case RESOURCE_ERR_PROCESS_NOT_FOUND:    return "PROCESS_NOT_FOUND";
        case RESOURCE_ERR_COMMUNICATION_FAILED: return "COMMUNICATION_FAILED";
        case RESOURCE_ERR_VERIFICATION_FAILED:  return "VERIFICATION_FAILED";
        case RESOURCE_ERR_LIMIT_BELOW_USAGE:    return "LIMIT_BELOW_CURRENT_USAGE";
        case RESOURCE_ERR_GROUP_BUSY:           return "GROUP_BUSY";
        case RESOURCE_ERR_FAILED:               return "FAILED";
        case RESOURCE_ERR_PROCESS_IDENTITY_CHANGED: return "PROCESS_IDENTITY_CHANGED";
        case RESOURCE_ERR_UNSUPPORTED_BY_HOST_HYPERVISOR: return "UNSUPPORTED_BY_HOST_HYPERVISOR";
        case RESOURCE_ERR_UNSUPPORTED_PLATFORM: return "UNSUPPORTED_PLATFORM";
        case RESOURCE_ERR_UNSUPPORTED_HARDWARE: return "UNSUPPORTED_HARDWARE";
        default:                                return "UNKNOWN";
    }
}

static const char *get_enforcement_status_str(ResourceResult res, int verified)
{
    if (res == RESOURCE_ERR_UNSUPPORTED ||
        res == RESOURCE_ERR_UNSUPPORTED_BY_HOST_HYPERVISOR ||
        res == RESOURCE_ERR_UNSUPPORTED_PLATFORM ||
        res == RESOURCE_ERR_UNSUPPORTED_HARDWARE) {
        return "UNSUPPORTED";
    }
    if (res != RESOURCE_SUCCESS) return "FAILED";
    if (verified) return "VERIFIED";
    return "APPLIED";
}

static int open_db_if_needed(void)
{
    if (s_rm_db) return 0;
    if (sqlite3_open(s_rm_db_path, &s_rm_db) != SQLITE_OK) {
        if (s_rm_db) {
            sqlite3_close(s_rm_db);
            s_rm_db = NULL;
        }
        return -1;
    }
    sqlite3_busy_timeout(s_rm_db, 2000);
    sqlite3_exec(s_rm_db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

    const char *schema =
        "CREATE TABLE IF NOT EXISTS resource_groups("
        "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  vm_id        TEXT    NOT NULL,"
        "  group_id     TEXT    NOT NULL,"
        "  group_name   TEXT    NOT NULL DEFAULT '',"
        "  cgroup_path  TEXT    NOT NULL DEFAULT '',"
        "  status       TEXT    NOT NULL DEFAULT 'ACTIVE',"
        "  created_at   INTEGER NOT NULL,"
        "  updated_at   INTEGER NOT NULL,"
        "  UNIQUE(vm_id, group_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_res_groups_vm ON resource_groups(vm_id);"
        "CREATE TABLE IF NOT EXISTS enforcement_events("
        "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  vm_id           TEXT    NOT NULL,"
        "  group_id        TEXT    NOT NULL,"
        "  operation       TEXT    NOT NULL,"
        "  requested_value TEXT    NOT NULL DEFAULT '',"
        "  applied_value   TEXT    NOT NULL DEFAULT '',"
        "  status          TEXT    NOT NULL DEFAULT 'SUCCESS',"
        "  verified        INTEGER NOT NULL DEFAULT 0,"
        "  error_message   TEXT    NOT NULL DEFAULT '',"
        "  timestamp       INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_enf_events_vm_grp ON enforcement_events(vm_id, group_id, timestamp);"
        "CREATE TABLE IF NOT EXISTS resource_limits("
        "  id                 INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  vm_id              TEXT    NOT NULL,"
        "  group_id           TEXT    NOT NULL DEFAULT '',"
        "  process_identifier TEXT    NOT NULL DEFAULT '',"
        "  cpu_quota_us       INTEGER NOT NULL DEFAULT -1,"
        "  cpu_period_us      INTEGER NOT NULL DEFAULT 100000,"
        "  cpu_cores          REAL    NOT NULL DEFAULT -1.0,"
        "  cpu_weight         INTEGER NOT NULL DEFAULT 100,"
        "  memory_max_bytes   INTEGER NOT NULL DEFAULT 0,"
        "  pids_max           INTEGER NOT NULL DEFAULT -1,"
        "  io_read_bps        INTEGER NOT NULL DEFAULT 0,"
        "  io_write_bps       INTEGER NOT NULL DEFAULT 0,"
        "  verified           INTEGER NOT NULL DEFAULT 0,"
        "  enforcement_status TEXT    NOT NULL DEFAULT 'pending',"
        "  detail             TEXT    NOT NULL DEFAULT '',"
        "  created_at         TEXT    NOT NULL DEFAULT (datetime('now','localtime')),"
        "  updated_at         TEXT    NOT NULL DEFAULT (datetime('now','localtime'))"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_res_limits_vm ON resource_limits(vm_id, group_id);";

    sqlite3_exec(s_rm_db, schema, NULL, NULL, NULL);
    return 0;
}

int resource_manager_init(const char *db_path)
{
    pthread_mutex_lock(&s_rm_lock);
    if (db_path && db_path[0]) {
        snprintf(s_rm_db_path, sizeof(s_rm_db_path), "%s", db_path);
    }
    if (open_db_if_needed() != 0) {
        pthread_mutex_unlock(&s_rm_lock);
        return -1;
    }
    pthread_mutex_unlock(&s_rm_lock);

    /* Ensure schema is up to date */
    vm_monitor_db_ensure_schema();
    return 0;
}

void resource_manager_shutdown(void)
{
    pthread_mutex_lock(&s_rm_lock);
    if (s_rm_db) {
        sqlite3_close(s_rm_db);
        s_rm_db = NULL;
    }
    pthread_mutex_unlock(&s_rm_lock);
}

/* ------------------------------------------------------------------ Host-to-Guest RPC */

ResourceResult resource_manager_rpc(const char *vm_id,
                                    const AgentResourceRequest *req,
                                    AgentResourceResponse *out_resp,
                                    int timeout_ms)
{
    if (!vm_id || !vm_id[0] || !req) return RESOURCE_ERR_INVALID_CONFIG;
    if (timeout_ms <= 0) timeout_ms = 3000;

    /* Check potential socket paths */
    char sock_path[512];
    snprintf(sock_path, sizeof(sock_path), "/tmp/vos_agent_%s.sock", vm_id);
    if (access(sock_path, F_OK) != 0) {
        snprintf(sock_path, sizeof(sock_path), "/tmp/virtualos_%s_agent.sock", vm_id);
        if (access(sock_path, F_OK) != 0) {
            return RESOURCE_ERR_COMMUNICATION_FAILED;
        }
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return RESOURCE_ERR_COMMUNICATION_FAILED;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return RESOURCE_ERR_COMMUNICATION_FAILED;
    }

    /* Serialize and send request */
    char send_buf[AGENT_MAX_JSON_SZ];
    int send_len = agent_rpc_serialize_request(req, send_buf, sizeof(send_buf) - 2);
    if (send_len <= 0) {
        close(fd);
        return RESOURCE_ERR_INVALID_CONFIG;
    }
    send_buf[send_len++] = '\n';
    send_buf[send_len] = '\0';

    if (write(fd, send_buf, (size_t)send_len) != send_len) {
        close(fd);
        return RESOURCE_ERR_COMMUNICATION_FAILED;
    }

    /* Wait for response */
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0 || !(pfd.revents & POLLIN)) {
        close(fd);
        return RESOURCE_ERR_COMMUNICATION_FAILED;
    }

    char recv_buf[AGENT_MAX_JSON_SZ];
    size_t total_read = 0;
    while (total_read + 1 < sizeof(recv_buf)) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) break;
        if (c == '\n' || c == '\r') {
            if (total_read > 0) {
                recv_buf[total_read] = '\0';
                if (strchr(recv_buf, '{') != NULL) {
                    break;
                }
                total_read = 0;
            }
            continue;
        }
        recv_buf[total_read++] = c;
    }
    recv_buf[total_read] = '\0';
    close(fd);

    if (total_read == 0) return RESOURCE_ERR_COMMUNICATION_FAILED;

    AgentResourceResponse resp;
    if (agent_rpc_deserialize_response(recv_buf, &resp) != 0) {
        return RESOURCE_ERR_COMMUNICATION_FAILED;
    }

    if (out_resp) *out_resp = resp;

    switch (resp.status_code) {
        case AGENT_STATUS_SUCCESS:              return RESOURCE_SUCCESS;
        case AGENT_STATUS_INVALID_CONFIG:       return RESOURCE_ERR_INVALID_CONFIG;
        case AGENT_STATUS_UNSUPPORTED:          return RESOURCE_ERR_UNSUPPORTED;
        case AGENT_STATUS_PERMISSION_DENIED:    return RESOURCE_ERR_PERMISSION_DENIED;
        case AGENT_STATUS_GROUP_NOT_FOUND:      return RESOURCE_ERR_GROUP_NOT_FOUND;
        case AGENT_STATUS_PROCESS_NOT_FOUND:    return RESOURCE_ERR_PROCESS_NOT_FOUND;
        case AGENT_STATUS_COMMUNICATION_FAILED: return RESOURCE_ERR_COMMUNICATION_FAILED;
        case AGENT_STATUS_VERIFICATION_FAILED:  return RESOURCE_ERR_VERIFICATION_FAILED;
        case AGENT_STATUS_LIMIT_BELOW_USAGE:    return RESOURCE_ERR_LIMIT_BELOW_USAGE;
        case AGENT_STATUS_GROUP_BUSY:           return RESOURCE_ERR_GROUP_BUSY;
        case AGENT_STATUS_PROCESS_IDENTITY_CHANGED: return RESOURCE_ERR_PROCESS_IDENTITY_CHANGED;
        case AGENT_STATUS_UNSUPPORTED_BY_HOST_HYPERVISOR: return RESOURCE_ERR_UNSUPPORTED_BY_HOST_HYPERVISOR;
        case AGENT_STATUS_UNSUPPORTED_PLATFORM: return RESOURCE_ERR_UNSUPPORTED_PLATFORM;
        case AGENT_STATUS_UNSUPPORTED_HARDWARE: return RESOURCE_ERR_UNSUPPORTED_HARDWARE;
        default:                                return RESOURCE_ERR_FAILED;
    }
}

/* ------------------------------------------------------------------ Audit Logging */

int resource_manager_audit_log(const char *vm_id,
                               const char *group_id,
                               const char *operation,
                               const char *req_val,
                               const char *applied_val,
                               const char *status,
                               int verified,
                               const char *err_msg)
{
    pthread_mutex_lock(&s_rm_lock);
    if (open_db_if_needed() != 0) {
        pthread_mutex_unlock(&s_rm_lock);
        return -1;
    }

    const char *sql =
        "INSERT INTO enforcement_events "
        "(vm_id, group_id, operation, requested_value, applied_value, status, verified, error_message, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s_rm_db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_rm_lock);
        return -1;
    }

    sqlite3_bind_text(st, 1, vm_id ? vm_id : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, group_id ? group_id : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, operation ? operation : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, req_val ? req_val : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, applied_val ? applied_val : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 6, status ? status : "UNKNOWN", -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 7, verified);
    sqlite3_bind_text(st, 8, err_msg ? err_msg : "", -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 9, (sqlite3_int64)get_time_ms());

    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_rm_lock);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* ------------------------------------------------------------------ High-Level APIs */

ResourceResult resource_manager_detect_capabilities(const char *vm_id,
                                                    AgentCapabilities *out_caps,
                                                    char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "req-%llu-%llu", (unsigned long long)get_time_ms(), (unsigned long long)s_req_seq++);
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id ? vm_id : "");
    req.operation = AGENT_OP_GET_CAPABILITIES;

    AgentResourceResponse resp;
    ResourceResult res = resource_manager_rpc(vm_id, &req, &resp, 3000);
    if (res == RESOURCE_SUCCESS) {
        if (out_caps) *out_caps = resp.capabilities;
    } else {
        if (err && err_sz > 0) snprintf(err, err_sz, "%s", resp.error_message[0] ? resp.error_message : resource_result_str(res));
    }

    resource_manager_audit_log(vm_id, "", "GET_CAPABILITIES", "",
                               resp.capabilities.cgroup_v2_supported ? "cgroup2=yes" : "cgroup2=no",
                               resource_result_str(res), resp.verified, resp.error_message);
    return res;
}

ResourceResult resource_manager_create_group(const char *vm_id,
                                             const char *group_id,
                                             const char *group_name,
                                             char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!vm_id || !group_id || !group_id[0]) return RESOURCE_ERR_INVALID_CONFIG;

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "req-%llu-%llu", (unsigned long long)get_time_ms(), (unsigned long long)s_req_seq++);
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_CREATE_GROUP;
    snprintf(req.group_id, sizeof(req.group_id), "%s", group_id);
    if (group_name && group_name[0]) {
        snprintf(req.group_name, sizeof(req.group_name), "%s", group_name);
    }

    AgentResourceResponse resp;
    ResourceResult res = resource_manager_rpc(vm_id, &req, &resp, 3000);

    /* Persist group in SQLite */
    pthread_mutex_lock(&s_rm_lock);
    if (open_db_if_needed() == 0) {
        const char *sql =
            "INSERT INTO resource_groups (vm_id, group_id, group_name, cgroup_path, status, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(vm_id, group_id) DO UPDATE SET "
            "group_name=excluded.group_name, status=excluded.status, updated_at=excluded.updated_at;";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(s_rm_db, sql, -1, &st, NULL) == SQLITE_OK) {
            uint64_t now = get_time_ms();
            char cpath[256];
            snprintf(cpath, sizeof(cpath), "/sys/fs/cgroup/virtualos/applications/%s", group_id);
            sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, group_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 3, group_name ? group_name : group_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 4, cpath, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 5, res == RESOURCE_SUCCESS ? "ACTIVE" : "PENDING", -1, SQLITE_STATIC);
            sqlite3_bind_int64(st, 6, (sqlite3_int64)now);
            sqlite3_bind_int64(st, 7, (sqlite3_int64)now);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    pthread_mutex_unlock(&s_rm_lock);

    if (res != RESOURCE_SUCCESS && err && err_sz > 0) {
        snprintf(err, err_sz, "%s", resp.error_message[0] ? resp.error_message : resource_result_str(res));
    }

    resource_manager_audit_log(vm_id, group_id, "CREATE_GROUP", group_name ? group_name : "",
                               res == RESOURCE_SUCCESS ? "CREATED" : "FAILED",
                               resource_result_str(res), resp.verified, resp.error_message);
    return res;
}

ResourceResult resource_manager_set_cpu_limit(const char *vm_id,
                                              const char *group_id,
                                              double cpu_cores,
                                              int64_t period_us,
                                              char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!vm_id || !group_id || !group_id[0]) return RESOURCE_ERR_INVALID_CONFIG;
    if (period_us <= 0) period_us = 100000;

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "req-%llu-%llu", (unsigned long long)get_time_ms(), (unsigned long long)s_req_seq++);
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_SET_CPU_LIMIT;
    snprintf(req.group_id, sizeof(req.group_id), "%s", group_id);
    req.cpu_cores = cpu_cores;
    req.cpu_period_us = period_us;
    if (cpu_cores > 0.0) {
        req.cpu_quota_us = (int64_t)(cpu_cores * (double)period_us);
    } else {
        req.cpu_quota_us = -1;
    }

    AgentResourceResponse resp;
    ResourceResult res = resource_manager_rpc(vm_id, &req, &resp, 3000);

    /* Persist limit record in SQLite */
    pthread_mutex_lock(&s_rm_lock);
    if (open_db_if_needed() == 0) {
        const char *sql =
            "INSERT INTO resource_limits (vm_id, group_id, cpu_quota_us, cpu_period_us, cpu_cores, verified, enforcement_status) "
            "VALUES (?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(s_rm_db, sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, group_id, -1, SQLITE_STATIC);
            sqlite3_bind_int64(st, 3, (sqlite3_int64)req.cpu_quota_us);
            sqlite3_bind_int64(st, 4, (sqlite3_int64)req.cpu_period_us);
            sqlite3_bind_double(st, 5, cpu_cores);
            sqlite3_bind_int(st, 6, resp.verified);
            sqlite3_bind_text(st, 7, get_enforcement_status_str(res, resp.verified), -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    pthread_mutex_unlock(&s_rm_lock);

    char req_str[64], appl_str[64];
    snprintf(req_str, sizeof(req_str), "cores=%.2f,period=%lld", cpu_cores, (long long)period_us);
    snprintf(appl_str, sizeof(appl_str), "quota=%lld,period=%lld", (long long)resp.applied.cpu_quota_us, (long long)resp.applied.cpu_period_us);

    if (res != RESOURCE_SUCCESS && err && err_sz > 0) {
        snprintf(err, err_sz, "%s", resp.error_message[0] ? resp.error_message : resource_result_str(res));
    }

    resource_manager_audit_log(vm_id, group_id, "SET_CPU_LIMIT", req_str, appl_str,
                               resource_result_str(res), resp.verified, resp.error_message);
    return res;
}

ResourceResult resource_manager_set_cpu_weight(const char *vm_id,
                                               const char *group_id,
                                               int weight,
                                               char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!vm_id || !group_id || !group_id[0]) return RESOURCE_ERR_INVALID_CONFIG;

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "req-%llu-%llu", (unsigned long long)get_time_ms(), (unsigned long long)s_req_seq++);
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_SET_CPU_WEIGHT;
    snprintf(req.group_id, sizeof(req.group_id), "%s", group_id);
    req.cpu_weight = weight;

    AgentResourceResponse resp;
    ResourceResult res = resource_manager_rpc(vm_id, &req, &resp, 3000);

    char req_str[32], appl_str[32];
    snprintf(req_str, sizeof(req_str), "weight=%d", weight);
    snprintf(appl_str, sizeof(appl_str), "weight=%d", resp.applied.cpu_weight);

    if (res != RESOURCE_SUCCESS && err && err_sz > 0) {
        snprintf(err, err_sz, "%s", resp.error_message[0] ? resp.error_message : resource_result_str(res));
    }

    resource_manager_audit_log(vm_id, group_id, "SET_CPU_WEIGHT", req_str, appl_str,
                               resource_result_str(res), resp.verified, resp.error_message);
    return res;
}

ResourceResult resource_manager_set_memory_limit(const char *vm_id,
                                                 const char *group_id,
                                                 uint64_t memory_bytes,
                                                 char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!vm_id || !group_id || !group_id[0]) return RESOURCE_ERR_INVALID_CONFIG;

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "req-%llu-%llu", (unsigned long long)get_time_ms(), (unsigned long long)s_req_seq++);
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_SET_MEMORY_LIMIT;
    snprintf(req.group_id, sizeof(req.group_id), "%s", group_id);
    req.memory_max_bytes = memory_bytes;

    AgentResourceResponse resp;
    ResourceResult res = resource_manager_rpc(vm_id, &req, &resp, 3000);

    /* Persist limit record in SQLite */
    pthread_mutex_lock(&s_rm_lock);
    if (open_db_if_needed() == 0) {
        const char *sql =
            "INSERT INTO resource_limits (vm_id, group_id, memory_max_bytes, verified, enforcement_status) "
            "VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(s_rm_db, sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, group_id, -1, SQLITE_STATIC);
            sqlite3_bind_int64(st, 3, (sqlite3_int64)memory_bytes);
            sqlite3_bind_int(st, 4, resp.verified);
            sqlite3_bind_text(st, 5, get_enforcement_status_str(res, resp.verified), -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    pthread_mutex_unlock(&s_rm_lock);

    char req_str[64], appl_str[64];
    snprintf(req_str, sizeof(req_str), "bytes=%llu", (unsigned long long)memory_bytes);
    snprintf(appl_str, sizeof(appl_str), "max=%llu,curr=%llu",
             (unsigned long long)resp.applied.memory_max_bytes, (unsigned long long)resp.applied.memory_current_bytes);

    if (res != RESOURCE_SUCCESS && err && err_sz > 0) {
        snprintf(err, err_sz, "%s", resp.error_message[0] ? resp.error_message : resource_result_str(res));
    }

    resource_manager_audit_log(vm_id, group_id, "SET_MEMORY_LIMIT", req_str, appl_str,
                               resource_result_str(res), resp.verified, resp.error_message);
    return res;
}

ResourceResult resource_manager_set_process_limit(const char *vm_id,
                                                  const char *group_id,
                                                  int64_t max_pids,
                                                  char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!vm_id || !group_id || !group_id[0]) return RESOURCE_ERR_INVALID_CONFIG;

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "req-%llu-%llu", (unsigned long long)get_time_ms(), (unsigned long long)s_req_seq++);
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_SET_PIDS_LIMIT;
    snprintf(req.group_id, sizeof(req.group_id), "%s", group_id);
    req.pids_max = max_pids;

    AgentResourceResponse resp;
    ResourceResult res = resource_manager_rpc(vm_id, &req, &resp, 3000);

    /* Persist limit record in SQLite */
    pthread_mutex_lock(&s_rm_lock);
    if (open_db_if_needed() == 0) {
        const char *sql =
            "INSERT INTO resource_limits (vm_id, group_id, pids_max, verified, enforcement_status) "
            "VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(s_rm_db, sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, group_id, -1, SQLITE_STATIC);
            sqlite3_bind_int64(st, 3, (sqlite3_int64)max_pids);
            sqlite3_bind_int(st, 4, resp.verified);
            sqlite3_bind_text(st, 5, get_enforcement_status_str(res, resp.verified), -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    pthread_mutex_unlock(&s_rm_lock);

    char req_str[64], appl_str[64];
    snprintf(req_str, sizeof(req_str), "pids_max=%lld", (long long)max_pids);
    snprintf(appl_str, sizeof(appl_str), "pids_max=%lld,current=%lld", (long long)resp.applied.pids_max, (long long)resp.applied.pids_current);

    if (res != RESOURCE_SUCCESS && err && err_sz > 0) {
        snprintf(err, err_sz, "%s", resp.error_message[0] ? resp.error_message : resource_result_str(res));
    }

    resource_manager_audit_log(vm_id, group_id, "SET_PIDS_LIMIT", req_str, appl_str,
                               resource_result_str(res), resp.verified, resp.error_message);
    return res;
}

ResourceResult resource_manager_set_io_limit(const char *vm_id,
                                             const char *group_id,
                                             uint64_t read_bps,
                                             uint64_t write_bps,
                                             char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!vm_id || !group_id || !group_id[0]) return RESOURCE_ERR_INVALID_CONFIG;

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "req-%llu-%llu", (unsigned long long)get_time_ms(), (unsigned long long)s_req_seq++);
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_SET_IO_LIMIT;
    snprintf(req.group_id, sizeof(req.group_id), "%s", group_id);
    req.io_read_bps = read_bps;
    req.io_write_bps = write_bps;

    AgentResourceResponse resp;
    ResourceResult res = resource_manager_rpc(vm_id, &req, &resp, 3000);

    /* Persist limit record in SQLite */
    pthread_mutex_lock(&s_rm_lock);
    if (open_db_if_needed() == 0) {
        const char *sql =
            "INSERT INTO resource_limits (vm_id, group_id, io_read_bps, io_write_bps, verified, enforcement_status) "
            "VALUES (?, ?, ?, ?, ?, ?);";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(s_rm_db, sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, group_id, -1, SQLITE_STATIC);
            sqlite3_bind_int64(st, 3, (sqlite3_int64)read_bps);
            sqlite3_bind_int64(st, 4, (sqlite3_int64)write_bps);
            sqlite3_bind_int(st, 5, resp.verified);
            sqlite3_bind_text(st, 6, get_enforcement_status_str(res, resp.verified), -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    pthread_mutex_unlock(&s_rm_lock);

    char req_str[64], appl_str[64];
    snprintf(req_str, sizeof(req_str), "rbps=%llu,wbps=%llu", (unsigned long long)read_bps, (unsigned long long)write_bps);
    snprintf(appl_str, sizeof(appl_str), "rbps=%llu,wbps=%llu", (unsigned long long)resp.applied.io_read_bps, (unsigned long long)resp.applied.io_write_bps);

    if (res != RESOURCE_SUCCESS && err && err_sz > 0) {
        snprintf(err, err_sz, "%s", resp.error_message[0] ? resp.error_message : resource_result_str(res));
    }

    resource_manager_audit_log(vm_id, group_id, "SET_IO_LIMIT", req_str, appl_str,
                               resource_result_str(res), resp.verified, resp.error_message);
    return res;
}

ResourceResult resource_manager_attach_process_with_identity(const char *vm_id,
                                                             const char *group_id,
                                                             pid_t pid,
                                                             uint64_t expected_start_time,
                                                             char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!vm_id || !group_id || !group_id[0]) return RESOURCE_ERR_INVALID_CONFIG;

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "req-%llu-%llu", (unsigned long long)get_time_ms(), (unsigned long long)s_req_seq++);
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_ATTACH_PROCESS;
    snprintf(req.group_id, sizeof(req.group_id), "%s", group_id);
    req.pid = pid;
    req.expected_start_time = expected_start_time;

    AgentResourceResponse resp;
    ResourceResult res = resource_manager_rpc(vm_id, &req, &resp, 3000);

    char req_str[64], appl_str[32];
    snprintf(req_str, sizeof(req_str), "pid=%d,starttime=%llu", (int)pid, (unsigned long long)expected_start_time);
    snprintf(appl_str, sizeof(appl_str), "attached_count=%d", resp.applied.attached_pids_count);

    if (res != RESOURCE_SUCCESS && err && err_sz > 0) {
        snprintf(err, err_sz, "%s", resp.error_message[0] ? resp.error_message : resource_result_str(res));
    }

    resource_manager_audit_log(vm_id, group_id, "ATTACH_PROCESS", req_str, appl_str,
                               resource_result_str(res), resp.verified, resp.error_message);
    return res;
}

ResourceResult resource_manager_attach_process(const char *vm_id,
                                               const char *group_id,
                                               pid_t pid,
                                               char *err, size_t err_sz)
{
    return resource_manager_attach_process_with_identity(vm_id, group_id, pid, 0, err, err_sz);
}

ResourceResult resource_manager_get_group_status(const char *vm_id,
                                                 const char *group_id,
                                                 AgentCgroupStatus *out_status,
                                                 char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!vm_id || !group_id || !group_id[0]) return RESOURCE_ERR_INVALID_CONFIG;

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "req-%llu-%llu", (unsigned long long)get_time_ms(), (unsigned long long)s_req_seq++);
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_GET_GROUP_STATUS;
    snprintf(req.group_id, sizeof(req.group_id), "%s", group_id);

    AgentResourceResponse resp;
    ResourceResult res = resource_manager_rpc(vm_id, &req, &resp, 3000);
    if (res == RESOURCE_SUCCESS) {
        if (out_status) {
            *out_status = resp.applied;
            out_status->verified = resp.verified;
        }
    } else {
        if (err && err_sz > 0) snprintf(err, err_sz, "%s", resp.error_message[0] ? resp.error_message : resource_result_str(res));
    }

    return res;
}

ResourceResult resource_manager_remove_group(const char *vm_id,
                                             const char *group_id,
                                             char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!vm_id || !group_id || !group_id[0]) return RESOURCE_ERR_INVALID_CONFIG;

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "req-%llu-%llu", (unsigned long long)get_time_ms(), (unsigned long long)s_req_seq++);
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_REMOVE_GROUP;
    snprintf(req.group_id, sizeof(req.group_id), "%s", group_id);

    AgentResourceResponse resp;
    ResourceResult res = resource_manager_rpc(vm_id, &req, &resp, 3000);

    /* Update group status in SQLite */
    pthread_mutex_lock(&s_rm_lock);
    if (open_db_if_needed() == 0) {
        const char *sql = "UPDATE resource_groups SET status='DELETED', updated_at=? WHERE vm_id=? AND group_id=?;";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(s_rm_db, sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, (sqlite3_int64)get_time_ms());
            sqlite3_bind_text(st, 2, vm_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 3, group_id, -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    pthread_mutex_unlock(&s_rm_lock);

    if (res != RESOURCE_SUCCESS && err && err_sz > 0) {
        snprintf(err, err_sz, "%s", resp.error_message[0] ? resp.error_message : resource_result_str(res));
    }

    resource_manager_audit_log(vm_id, group_id, "REMOVE_GROUP", "", res == RESOURCE_SUCCESS ? "REMOVED" : "FAILED",
                               resource_result_str(res), resp.verified, resp.error_message);
    return res;
}

ResourceResult resource_manager_list_groups(const char *vm_id,
                                            char out_groups[][64],
                                            int max_groups,
                                            int *out_count)
{
    if (out_count) *out_count = 0;
    if (!vm_id || !vm_id[0] || !out_groups || max_groups <= 0) return RESOURCE_ERR_INVALID_CONFIG;

    pthread_mutex_lock(&s_rm_lock);
    if (open_db_if_needed() != 0) {
        pthread_mutex_unlock(&s_rm_lock);
        return RESOURCE_ERR_FAILED;
    }

    const char *sql = "SELECT group_id FROM resource_groups WHERE vm_id=? AND status!='DELETED' ORDER BY id ASC;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(s_rm_db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_rm_lock);
        return RESOURCE_ERR_FAILED;
    }
    sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);

    int count = 0;
    while (sqlite3_step(st) == SQLITE_ROW && count < max_groups) {
        const char *gid = (const char *)sqlite3_column_text(st, 0);
        if (gid) {
            snprintf(out_groups[count++], 64, "%s", gid);
        }
    }
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_rm_lock);

    if (out_count) *out_count = count;
    return RESOURCE_SUCCESS;
}

/* ------------------------------------------------------------------ VM Hardware Configuration */

ResourceResult resource_manager_set_vm_hardware(const char *vm_id,
                                                int cpu_count,
                                                int memory_mb,
                                                int *out_requires_restart,
                                                char *err, size_t err_sz)
{
    if (err && err_sz > 0) err[0] = '\0';
    if (!vm_id || !vm_id[0]) return RESOURCE_ERR_INVALID_CONFIG;

    if (cpu_count < 1 || cpu_count > VM_MAX_CPUS) {
        if (err && err_sz > 0) snprintf(err, err_sz, "Invalid cpu_count %d (allowed: 1 to %d)", cpu_count, VM_MAX_CPUS);
        return RESOURCE_ERR_INVALID_CONFIG;
    }
    if (memory_mb < VM_MIN_MEM_MB || memory_mb > 65536) {
        if (err && err_sz > 0) snprintf(err, err_sz, "Invalid memory_mb %d (allowed: %d to 65536)", memory_mb, VM_MIN_MEM_MB);
        return RESOURCE_ERR_INVALID_CONFIG;
    }

    VmConfig cfg;
    VmState st = VM_STATE_CREATED;
    if (vm_db_get(vm_id, &cfg, &st) != 0) {
        if (err && err_sz > 0) snprintf(err, err_sz, "VM '%s' not found in database", vm_id);
        return RESOURCE_ERR_INVALID_CONFIG;
    }

    cfg.cpu_count = cpu_count;
    cfg.memory_mb = memory_mb;

    VmHandle h;
    memset(&h, 0, sizeof(h));
    h.cfg = cfg;
    h.state = st;

    if (vm_db_save(&h) <= 0) {
        if (err && err_sz > 0) snprintf(err, err_sz, "Failed to save updated VM configuration to database");
        return RESOURCE_ERR_FAILED;
    }

    /* QEMU HVF on Apple Silicon does not support dynamic CPU/RAM hotplug; restart is strictly required */
    if (out_requires_restart) *out_requires_restart = 1;

    char req_str[64], appl_str[64];
    snprintf(req_str, sizeof(req_str), "cpus=%d,ram_mb=%d", cpu_count, memory_mb);
    snprintf(appl_str, sizeof(appl_str), "requires_restart=1");

    resource_manager_audit_log(vm_id, "", "SET_VM_HARDWARE", req_str, appl_str,
                               "SUCCESS", 1, "Config saved. Restart required to apply under QEMU HVF.");

    return RESOURCE_SUCCESS;
}
