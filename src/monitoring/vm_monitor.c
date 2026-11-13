/* vm_monitor.c – Virtual Machine Resource Monitor & Telemetry Engine
 *
 * Implements:
 *   1. Background polling of virtio-serial guest agent socket.
 *   2. Host-side hypervisor process sampling (Mach / sysctl / proc_pidinfo).
 *   3. In-guest kernel telemetry validation and integrity checking.
 *   4. Telemetry freshness lifecycle tracking (LIVE, STALE, UNAVAILABLE).
 *   5. SQLite persistence with source attribution and integrity flags.
 *   6. Thread-safe monitoring slots and synchronization.
 */

#include "vm_monitor.h"
#include "host_monitor.h"
#include "agent_protocol.h"
#include "process_monitor.h"
#include "resource_limits.h"
#include "../virtualization/vm_manager.h"
#include "../virtualization/vm_config.h"
#include "../core/util.h"
#include "../db/database.h"
#include <sqlite3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#define MAX_MONITORED_VMS 16
#define MAX_STREAM_BUF    65536

typedef struct {
    char                  vm_id[64];
    int                   is_active;
    int                   stop_requested;
    int                   interval_ms;
    VmManager            *mgr;
    pthread_t             worker_thread;
    pthread_mutex_t       slot_lock;
    pthread_cond_t        slot_cond;

    uint64_t              start_time_ms;
    VmResourceMetrics     latest_metrics;
    GuestTelemetryPayload latest_guest;
    int                   has_guest_data;
    uint64_t              last_telemetry_rx_ms;
    int                   has_integrity_warning;
    char                  integrity_warning[256];

    int                   guest_sock_fd;
    char                  sock_path[VM_PATH_MAX_LEN];
} VmMonitorSlot;

static VmMonitorSlot s_slots[MAX_MONITORED_VMS];
static pthread_mutex_t s_engine_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_engine_initialized = 0;

static sqlite3 *s_mon_db = NULL;
static char     s_mon_db_path[VM_PATH_MAX_LEN] = {0};

/* ------------------------------------------------------------------ Helpers */

const char *metric_source_name(MetricSource src)
{
    switch (src) {
        case METRIC_SOURCE_HOST:     return "host";
        case METRIC_SOURCE_GUEST:    return "guest";
        case METRIC_SOURCE_COMBINED: return "combined";
        default:                     return "none";
    }
}

const char *telemetry_freshness_name(TelemetryFreshness f)
{
    switch (f) {
        case TELEMETRY_LIVE:        return "LIVE";
        case TELEMETRY_STALE:       return "STALE";
        case TELEMETRY_UNAVAILABLE: return "UNAVAILABLE";
        default:                    return "UNKNOWN";
    }
}

static uint64_t get_time_ms(void)
{
    return host_monitor_now_ms();
}

/* ------------------------------------------------------------------ Database */

static int mon_db_open_if_needed(void)
{
    if (s_mon_db) return 0;
    const char *path = s_mon_db_path[0] ? s_mon_db_path : "virtual_os.db";
    if (sqlite3_open(path, &s_mon_db) != SQLITE_OK) {
        s_mon_db = NULL;
        return -1;
    }
    sqlite3_busy_timeout(s_mon_db, 5000);
    sqlite3_exec(s_mon_db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    return 0;
}

int vm_monitor_db_ensure_schema(void)
{
    if (mon_db_open_if_needed() != 0) return -1;

    const char *SCHEMA =
        "CREATE TABLE IF NOT EXISTS resource_samples("
        "  id                 INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  vm_id              TEXT    NOT NULL,"
        "  timestamp_ms       INTEGER NOT NULL,"
        "  metric_source      TEXT    NOT NULL,"
        "  freshness_status   TEXT    NOT NULL DEFAULT 'UNAVAILABLE',"
        "  integrity_flags    INTEGER NOT NULL DEFAULT 0,"
        "  integrity_warning  TEXT    NOT NULL DEFAULT '',"
        "  qemu_pid           INTEGER NOT NULL DEFAULT 0,"
        "  host_cpu_percent   REAL    NOT NULL DEFAULT -1.0,"
        "  host_memory_used   INTEGER NOT NULL DEFAULT -1,"
        "  host_memory_total  INTEGER NOT NULL DEFAULT -1,"
        "  qemu_cpu_percent   REAL    NOT NULL DEFAULT -1.0,"
        "  qemu_memory_rss    INTEGER NOT NULL DEFAULT -1,"
        "  guest_cpu_percent  REAL    NOT NULL DEFAULT -1.0,"
        "  guest_memory_used  INTEGER NOT NULL DEFAULT -1,"
        "  guest_memory_total INTEGER NOT NULL DEFAULT -1,"
        "  disk_read_bytes    INTEGER NOT NULL DEFAULT -1,"
        "  disk_write_bytes   INTEGER NOT NULL DEFAULT -1,"
        "  network_rx_bytes   INTEGER NOT NULL DEFAULT -1,"
        "  network_tx_bytes   INTEGER NOT NULL DEFAULT -1"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_res_samples_vm_time ON resource_samples(vm_id, timestamp_ms);"
        ""
        "CREATE TABLE IF NOT EXISTS process_samples("
        "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  sample_id    INTEGER REFERENCES resource_samples(id) ON DELETE CASCADE,"
        "  vm_id        TEXT    NOT NULL,"
        "  timestamp    TEXT    NOT NULL DEFAULT (datetime('now','localtime')),"
        "  pid          INTEGER NOT NULL,"
        "  ppid         INTEGER NOT NULL DEFAULT 0,"
        "  process_name TEXT    NOT NULL,"
        "  state        TEXT    NOT NULL DEFAULT 'R',"
        "  cpu_percent  REAL    NOT NULL DEFAULT 0.0,"
        "  memory_bytes INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_proc_samples_sample ON process_samples(sample_id);"
        ""
        "CREATE TABLE IF NOT EXISTS resource_limits("
        "  id                 INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  vm_id              TEXT    NOT NULL,"
        "  process_identifier TEXT    NOT NULL DEFAULT '',"
        "  cpu_limit          REAL    NOT NULL DEFAULT -1.0,"
        "  memory_limit       INTEGER NOT NULL DEFAULT -1,"
        "  enforcement_status TEXT    NOT NULL DEFAULT 'pending',"
        "  detail             TEXT    NOT NULL DEFAULT '',"
        "  created_at         TEXT    NOT NULL DEFAULT (datetime('now','localtime')),"
        "  updated_at         TEXT    NOT NULL DEFAULT (datetime('now','localtime')),"
        "  group_id           TEXT    NOT NULL DEFAULT '',"
        "  cpu_quota_us       INTEGER NOT NULL DEFAULT -1,"
        "  cpu_period_us      INTEGER NOT NULL DEFAULT 100000,"
        "  cpu_cores          REAL    NOT NULL DEFAULT -1.0,"
        "  cpu_weight         INTEGER NOT NULL DEFAULT 100,"
        "  memory_max_bytes   INTEGER NOT NULL DEFAULT 0,"
        "  pids_max           INTEGER NOT NULL DEFAULT -1,"
        "  io_read_bps        INTEGER NOT NULL DEFAULT 0,"
        "  io_write_bps       INTEGER NOT NULL DEFAULT 0,"
        "  verified           INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_res_limits_vm ON resource_limits(vm_id, process_identifier);"
        "CREATE INDEX IF NOT EXISTS idx_res_limits_vm_group ON resource_limits(vm_id, group_id);"
        ""
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
        ""
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
        "CREATE INDEX IF NOT EXISTS idx_enf_events_vm_grp ON enforcement_events(vm_id, group_id, timestamp);";

    char *err = NULL;
    if (sqlite3_exec(s_mon_db, SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        sqlite3_free(err);
        return -1;
    }

    /* Auto-migrate existing tables from Phase 3 if needed */
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_samples ADD COLUMN freshness_status TEXT NOT NULL DEFAULT 'UNAVAILABLE';", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_samples ADD COLUMN integrity_flags INTEGER NOT NULL DEFAULT 0;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_samples ADD COLUMN integrity_warning TEXT NOT NULL DEFAULT '';", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_samples ADD COLUMN host_cpu_percent REAL NOT NULL DEFAULT -1.0;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_samples ADD COLUMN host_memory_used INTEGER NOT NULL DEFAULT -1;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_samples ADD COLUMN host_memory_total INTEGER NOT NULL DEFAULT -1;", NULL, NULL, NULL);

    /* Auto-migrate resource_limits for Phase 4 */
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_limits ADD COLUMN group_id TEXT NOT NULL DEFAULT '';", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_limits ADD COLUMN cpu_quota_us INTEGER NOT NULL DEFAULT -1;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_limits ADD COLUMN cpu_period_us INTEGER NOT NULL DEFAULT 100000;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_limits ADD COLUMN cpu_cores REAL NOT NULL DEFAULT -1.0;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_limits ADD COLUMN cpu_weight INTEGER NOT NULL DEFAULT 100;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_limits ADD COLUMN memory_max_bytes INTEGER NOT NULL DEFAULT 0;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_limits ADD COLUMN pids_max INTEGER NOT NULL DEFAULT -1;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_limits ADD COLUMN io_read_bps INTEGER NOT NULL DEFAULT 0;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_limits ADD COLUMN io_write_bps INTEGER NOT NULL DEFAULT 0;", NULL, NULL, NULL);
    sqlite3_exec(s_mon_db, "ALTER TABLE resource_limits ADD COLUMN verified INTEGER NOT NULL DEFAULT 0;", NULL, NULL, NULL);

    return 0;
}

static int db_save_sample(const VmResourceMetrics *m, const GuestProcessMetric *procs, int proc_count)
{
    if (!m) return -1;
    if (mon_db_open_if_needed() != 0) return -1;

    sqlite3_exec(s_mon_db, "BEGIN IMMEDIATE TRANSACTION;", NULL, NULL, NULL);

    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO resource_samples("
        "  vm_id, timestamp_ms, metric_source, freshness_status, integrity_flags, integrity_warning, qemu_pid,"
        "  host_cpu_percent, host_memory_used, host_memory_total,"
        "  qemu_cpu_percent, qemu_memory_rss, guest_cpu_percent,"
        "  guest_memory_used, guest_memory_total,"
        "  disk_read_bytes, disk_write_bytes, network_rx_bytes, network_tx_bytes"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

    if (sqlite3_prepare_v2(s_mon_db, sql, -1, &st, NULL) != SQLITE_OK) {
        sqlite3_exec(s_mon_db, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }

    sqlite3_bind_text  (st, 1,  m->vm_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64 (st, 2,  (sqlite3_int64)m->timestamp_ms);
    sqlite3_bind_text  (st, 3,  metric_source_name(m->metric_source), -1, SQLITE_STATIC);
    sqlite3_bind_text  (st, 4,  telemetry_freshness_name(m->freshness), -1, SQLITE_STATIC);
    sqlite3_bind_int   (st, 5,  m->has_integrity_warning ? 1 : 0);
    sqlite3_bind_text  (st, 6,  m->integrity_warning, -1, SQLITE_STATIC);
    sqlite3_bind_int   (st, 7,  (int)m->qemu_pid);
    sqlite3_bind_double(st, 8,  m->host_cpu_percent);
    sqlite3_bind_int64 (st, 9,  (sqlite3_int64)m->host_memory_used_bytes);
    sqlite3_bind_int64 (st, 10, (sqlite3_int64)m->host_memory_total_bytes);
    sqlite3_bind_double(st, 11, m->qemu_cpu_percent);
    sqlite3_bind_int64 (st, 12, (sqlite3_int64)m->qemu_memory_rss_bytes);
    sqlite3_bind_double(st, 13, m->guest_cpu_percent);
    sqlite3_bind_int64 (st, 14, (sqlite3_int64)m->guest_memory_used_bytes);
    sqlite3_bind_int64 (st, 15, (sqlite3_int64)m->guest_memory_total_bytes);
    sqlite3_bind_int64 (st, 16, (sqlite3_int64)m->disk_read_bytes);
    sqlite3_bind_int64 (st, 17, (sqlite3_int64)m->disk_write_bytes);
    sqlite3_bind_int64 (st, 18, (sqlite3_int64)m->network_rx_bytes);
    sqlite3_bind_int64 (st, 19, (sqlite3_int64)m->network_tx_bytes);

    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        sqlite3_exec(s_mon_db, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }

    int64_t sample_id = sqlite3_last_insert_rowid(s_mon_db);

    /* Insert top processes if present */
    if (procs && proc_count > 0) {
        sqlite3_stmt *pst = NULL;
        const char *psql =
            "INSERT INTO process_samples("
            "  sample_id, vm_id, pid, ppid, process_name, state, cpu_percent, memory_bytes"
            ") VALUES(?,?,?,?,?,?,?,?);";

        if (sqlite3_prepare_v2(s_mon_db, psql, -1, &pst, NULL) == SQLITE_OK) {
            int to_insert = (proc_count > 32) ? 32 : proc_count; /* Save top 32 per sample */
            for (int i = 0; i < to_insert; i++) {
                sqlite3_bind_int64 (pst, 1, sample_id);
                sqlite3_bind_text  (pst, 2, m->vm_id, -1, SQLITE_STATIC);
                sqlite3_bind_int   (pst, 3, procs[i].pid);
                sqlite3_bind_int   (pst, 4, procs[i].ppid);
                sqlite3_bind_text  (pst, 5, procs[i].name, -1, SQLITE_STATIC);
                sqlite3_bind_text  (pst, 6, procs[i].state[0] ? procs[i].state : "R", -1, SQLITE_STATIC);
                sqlite3_bind_double(pst, 7, procs[i].cpu_percent);
                sqlite3_bind_int64 (pst, 8, (sqlite3_int64)procs[i].memory_rss_bytes);
                sqlite3_step(pst);
                sqlite3_reset(pst);
            }
            sqlite3_finalize(pst);
        }
    }

    sqlite3_exec(s_mon_db, "COMMIT;", NULL, NULL, NULL);
    return 0;
}

/* ------------------------------------------------------------------ Telemetry Validation */

int vm_monitor_validate_telemetry(const VmConfig *cfg,
                                  const GuestTelemetryPayload *payload,
                                  char *warn_buf, size_t warn_sz)
{
    if (warn_buf && warn_sz > 0) {
        warn_buf[0] = '\0';
    }
    if (!payload) return -1;

    int warnings = 0;
    char issues[512] = {0};

    if (cfg) {
        /* Check 1: CPU core count mismatch */
        if (cfg->cpu_count > 0 && payload->cpu.core_count > 0) {
            if (payload->cpu.core_count != cfg->cpu_count) {
                char tmp[160];
                snprintf(tmp, sizeof(tmp),
                         "CPU core mismatch: VM configured with %d vCPUs, but guest telemetry reported %d cores. ",
                         cfg->cpu_count, payload->cpu.core_count);
                strncat(issues, tmp, sizeof(issues) - strlen(issues) - 1);
                warnings++;
            }
        }

        /* Check 2: Memory size contradiction */
        if (cfg->memory_mb > 0 && payload->memory.total_bytes > 0) {
            uint64_t expected_bytes = (uint64_t)cfg->memory_mb * 1024ULL * 1024ULL;
            /* In Linux, MemTotal is slightly lower than configured RAM due to kernel reserved memory (~3-12%).
             * But if it exceeds 115% of configured RAM or falls below 65% of configured RAM, it is a contradiction. */
            uint64_t max_allowed = (expected_bytes * 115ULL) / 100ULL;
            uint64_t min_allowed = (expected_bytes * 65ULL)  / 100ULL;

            if (payload->memory.total_bytes > max_allowed || payload->memory.total_bytes < min_allowed) {
                char tmp[160];
                double rep_mb = (double)payload->memory.total_bytes / (1024.0 * 1024.0);
                snprintf(tmp, sizeof(tmp),
                         "Memory size contradiction: VM configured with %d MB, but guest telemetry reported %.1f MB. ",
                         cfg->memory_mb, rep_mb);
                strncat(issues, tmp, sizeof(issues) - strlen(issues) - 1);
                warnings++;
            }
        }
    }

    /* Check 3: Host process contamination detection */
    if (payload->process_count > 0) {
        static const char *s_host_apps[] = {
            "Safari", "Brave", "Chrome", "Firefox", "WhatsApp", "Slack",
            "Antigravity", "Microsoft Word", "Word", "Adobe", "Finder",
            "Dock", "WindowServer", "launchd", "kernel_task"
        };
        for (int i = 0; i < payload->process_count; i++) {
            const char *pname = payload->processes[i].name;
            for (size_t a = 0; a < sizeof(s_host_apps)/sizeof(s_host_apps[a]); a++) {
                if (strstr(pname, s_host_apps[a]) != NULL) {
                    char tmp[160];
                    snprintf(tmp, sizeof(tmp),
                             "Host process contamination: detected host process '%s' in guest process list. ",
                             pname);
                    strncat(issues, tmp, sizeof(issues) - strlen(issues) - 1);
                    warnings++;
                    break;
                }
            }
            if (warnings >= 3) break;
        }
    }

    if (warn_buf && warn_sz > 0 && warnings > 0) {
        snprintf(warn_buf, warn_sz, "%s", issues);
    }
    return warnings;
}

/* ------------------------------------------------------------------ Virtio-Serial Socket Polling */

static void poll_guest_agent_socket(VmMonitorSlot *slot)
{
    if (slot->guest_sock_fd < 0) {
        /* Attempt to connect to the virtio-serial UNIX domain socket */
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0) {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, slot->sock_path, sizeof(addr.sun_path) - 1);

            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0 || errno == EINPROGRESS) {
                slot->guest_sock_fd = fd;
            } else {
                close(fd);
            }
        }
    }

    if (slot->guest_sock_fd >= 0) {
        struct pollfd pfd;
        pfd.fd = slot->guest_sock_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        if (poll(&pfd, 1, 50) > 0 && (pfd.revents & POLLIN)) {
            static char buf[MAX_STREAM_BUF];
            ssize_t n = read(slot->guest_sock_fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                GuestTelemetryPayload p;
                if (agent_protocol_deserialize(buf, &p) == 0) {
                    VmConfig cfg;
                    vm_config_init(&cfg);
                    VmState st = VM_STATE_CREATED;
                    int has_cfg = 0;
                    if (slot->mgr) {
                        VmHandle h;
                        if (vm_get_handle(slot->mgr, slot->vm_id, &h) == 0) {
                            cfg = h.cfg;
                            has_cfg = 1;
                        }
                    }
                    if (!has_cfg) {
                        vm_db_get(slot->vm_id, &cfg, &st);
                    }

                    char warn[256] = {0};
                    int warn_cnt = vm_monitor_validate_telemetry(&cfg, &p, warn, sizeof(warn));

                    pthread_mutex_lock(&slot->slot_lock);
                    slot->latest_guest = p;
                    slot->has_guest_data = 1;
                    slot->last_telemetry_rx_ms = get_time_ms();
                    if (warn_cnt > 0) {
                        slot->has_integrity_warning = 1;
                        snprintf(slot->integrity_warning, sizeof(slot->integrity_warning), "%s", warn);
                        fprintf(stderr, "[vm_monitor] WARNING for VM '%s': %s\n", slot->vm_id, warn);
                    } else {
                        slot->has_integrity_warning = 0;
                        slot->integrity_warning[0] = '\0';
                    }
                    pthread_mutex_unlock(&slot->slot_lock);
                }
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                /* Disconnected */
                close(slot->guest_sock_fd);
                slot->guest_sock_fd = -1;
            }
        }
    }
}

/* ------------------------------------------------------------------ Worker Thread */

static void *vm_monitor_worker(void *arg)
{
    VmMonitorSlot *slot = (VmMonitorSlot *)arg;
    if (!slot) return NULL;

    while (1) {
        pthread_mutex_lock(&slot->slot_lock);
        if (slot->stop_requested) {
            pthread_mutex_unlock(&slot->slot_lock);
            break;
        }
        pthread_mutex_unlock(&slot->slot_lock);

        /* 1. Poll guest agent socket if configured */
        poll_guest_agent_socket(slot);

        /* 2. Resolve QEMU PID and configuration */
        pid_t qpid = 0;
        VmConfig cfg;
        vm_config_init(&cfg);
        VmState st = VM_STATE_CREATED;

        if (slot->mgr) {
            VmHandle h;
            if (vm_get_handle(slot->mgr, slot->vm_id, &h) == 0) {
                qpid = h.pid;
                cfg  = h.cfg;
                st   = h.state;
            }
        }
        if (qpid == 0) {
            /* Try database lookup */
            vm_db_get(slot->vm_id, &cfg, &st);
        }

        /* Verify actual process liveness with kill(pid, 0) */
        int qemu_alive = (qpid > 0 && kill(qpid, 0) == 0);
        if (!qemu_alive && st == VM_STATE_RUNNING) {
            st = VM_STATE_STOPPED;
        }

        /* 3. Sample host system and host QEMU process */
        HostMetrics host_sys;
        memset(&host_sys, 0, sizeof(host_sys));
        host_monitor_sample(&host_sys);

        ProcessHostMetrics pm;
        memset(&pm, 0, sizeof(pm));
        if (qemu_alive) {
            host_monitor_sample_process(qpid, &pm);
        }

        /* 4. Synthesize current snapshot with freshness and lifecycle reconciliation */
        pthread_mutex_lock(&slot->slot_lock);

        uint64_t now_ms = get_time_ms();
        VmResourceMetrics *rm = &slot->latest_metrics;
        memset(rm, 0, sizeof(*rm));
        snprintf(rm->vm_id, sizeof(rm->vm_id), "%s", slot->vm_id);
        rm->timestamp_ms          = now_ms;
        rm->vm_state              = st;
        rm->qemu_pid              = qemu_alive ? qpid : 0;
        rm->vcpu_allocated        = cfg.cpu_count;
        rm->memory_mb_configured  = cfg.memory_mb;
        rm->uptime_seconds        = (qemu_alive && now_ms >= slot->start_time_ms) ?
                                    ((now_ms - slot->start_time_ms) / 1000ULL) : 0;

        /* Host system readings */
        rm->host_cpu_percent        = host_sys.cpu_total_percent;
        rm->host_memory_total_bytes = host_sys.memory_total_bytes;
        rm->host_memory_used_bytes  = host_sys.memory_used_bytes;

        /* Host observations of QEMU hypervisor */
        rm->qemu_cpu_percent      = qemu_alive ? pm.cpu_percent : -1.0;
        rm->qemu_memory_rss_bytes = qemu_alive ? pm.memory_rss_bytes : 0;

        /* Guest observations & freshness reconciliation */
        if (slot->has_guest_data) {
            uint64_t age_ms = (now_ms >= slot->last_telemetry_rx_ms) ?
                              (now_ms - slot->last_telemetry_rx_ms) : 0;
            rm->telemetry_age_ms            = age_ms;
            rm->last_telemetry_timestamp_ms = slot->last_telemetry_rx_ms;
            rm->has_integrity_warning       = slot->has_integrity_warning;
            snprintf(rm->integrity_warning, sizeof(rm->integrity_warning), "%s", slot->integrity_warning);

            const GuestTelemetryPayload *gp = &slot->latest_guest;
            rm->guest_cpu_percent            = gp->cpu.total_percent;
            rm->guest_memory_total_bytes     = gp->memory.total_bytes;
            rm->guest_memory_used_bytes      = gp->memory.used_bytes;
            rm->guest_memory_available_bytes = gp->memory.available_bytes;
            rm->disk_read_bytes              = gp->disk.read_bytes;
            rm->disk_write_bytes             = gp->disk.write_bytes;
            rm->network_rx_bytes             = gp->network.rx_bytes;
            rm->network_tx_bytes             = gp->network.tx_bytes;
            rm->guest_process_count          = gp->process_count;

            /* Freshness evaluation:
             * LIVE: QEMU is running AND telemetry arrived within 3 intervals + 2s timeout.
             * STALE: QEMU terminated OR telemetry is older than timeout. */
            if (!qemu_alive) {
                rm->freshness = TELEMETRY_STALE;
                rm->has_guest_telemetry = 0; /* Do not claim live guest telemetry when QEMU is dead */
                rm->metric_source = METRIC_SOURCE_NONE;
            } else if (age_ms > (uint64_t)(slot->interval_ms * 3 + 2000)) {
                rm->freshness = TELEMETRY_STALE;
                rm->has_guest_telemetry = 1;
                rm->metric_source = METRIC_SOURCE_HOST;
            } else {
                rm->freshness = TELEMETRY_LIVE;
                rm->has_guest_telemetry = 1;
                rm->metric_source = METRIC_SOURCE_COMBINED;
            }
        } else {
            rm->freshness                    = TELEMETRY_UNAVAILABLE;
            rm->telemetry_age_ms            = 0;
            rm->last_telemetry_timestamp_ms = 0;
            rm->has_guest_telemetry          = 0;
            rm->guest_cpu_percent            = -1.0;
            rm->guest_memory_total_bytes     = 0;
            rm->guest_memory_used_bytes      = 0;
            rm->guest_memory_available_bytes = 0;
            rm->disk_read_bytes              = 0;
            rm->disk_write_bytes             = 0;
            rm->network_rx_bytes             = 0;
            rm->network_tx_bytes             = 0;
            rm->guest_process_count          = 0;
            rm->metric_source                = qemu_alive ? METRIC_SOURCE_HOST : METRIC_SOURCE_NONE;
        }

        /* 5. Snapshot copy of processes for database transaction */
        GuestProcessMetric proc_copy[AGENT_MAX_PROCESSES];
        int proc_copy_count = 0;
        if (slot->has_guest_data && qemu_alive) {
            proc_copy_count = slot->latest_guest.process_count;
            if (proc_copy_count > AGENT_MAX_PROCESSES) proc_copy_count = AGENT_MAX_PROCESSES;
            memcpy(proc_copy, slot->latest_guest.processes,
                   sizeof(GuestProcessMetric) * (size_t)proc_copy_count);
        }

        pthread_mutex_unlock(&slot->slot_lock);

        db_save_sample(rm, proc_copy, proc_copy_count);

        /* 6. Sleep for configured interval */
        pthread_mutex_lock(&slot->slot_lock);
        if (slot->stop_requested) {
            pthread_mutex_unlock(&slot->slot_lock);
            break;
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += slot->interval_ms / 1000;
        ts.tv_nsec += (long)(slot->interval_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&slot->slot_cond, &slot->slot_lock, &ts);
        pthread_mutex_unlock(&slot->slot_lock);
    }

    if (slot->guest_sock_fd >= 0) {
        close(slot->guest_sock_fd);
        slot->guest_sock_fd = -1;
    }
    return NULL;
}

/* ------------------------------------------------------------------ Lifecycle */

int vm_monitor_init(const char *db_path)
{
    pthread_mutex_lock(&s_engine_lock);
    if (s_engine_initialized) {
        pthread_mutex_unlock(&s_engine_lock);
        return 0;
    }

    if (db_path && db_path[0] != '\0') {
        snprintf(s_mon_db_path, sizeof(s_mon_db_path), "%s", db_path);
    } else {
        snprintf(s_mon_db_path, sizeof(s_mon_db_path), "virtual_os.db");
    }

    host_monitor_init();

    for (int i = 0; i < MAX_MONITORED_VMS; i++) {
        memset(&s_slots[i], 0, sizeof(s_slots[i]));
        s_slots[i].guest_sock_fd = -1;
        pthread_mutex_init(&s_slots[i].slot_lock, NULL);
        pthread_cond_init(&s_slots[i].slot_cond, NULL);
    }

    s_engine_initialized = 1;
    pthread_mutex_unlock(&s_engine_lock);

    vm_monitor_db_ensure_schema();
    return 0;
}

void vm_monitor_shutdown(void)
{
    pthread_mutex_lock(&s_engine_lock);
    if (!s_engine_initialized) {
        pthread_mutex_unlock(&s_engine_lock);
        return;
    }

    for (int i = 0; i < MAX_MONITORED_VMS; i++) {
        if (s_slots[i].is_active) {
            pthread_mutex_lock(&s_slots[i].slot_lock);
            s_slots[i].stop_requested = 1;
            pthread_cond_broadcast(&s_slots[i].slot_cond);
            pthread_mutex_unlock(&s_slots[i].slot_lock);

            pthread_join(s_slots[i].worker_thread, NULL);

            if (s_slots[i].guest_sock_fd >= 0) {
                close(s_slots[i].guest_sock_fd);
                s_slots[i].guest_sock_fd = -1;
            }
            s_slots[i].is_active = 0;
        }
        pthread_mutex_destroy(&s_slots[i].slot_lock);
        pthread_cond_destroy(&s_slots[i].slot_cond);
    }

    if (s_mon_db) {
        sqlite3_close(s_mon_db);
        s_mon_db = NULL;
    }

    host_monitor_shutdown();
    s_engine_initialized = 0;
    pthread_mutex_unlock(&s_engine_lock);
}

int vm_monitor_start(const char *vm_id, int interval_ms, VmManager *mgr)
{
    if (!vm_id) return -1;
    if (interval_ms < 100) interval_ms = 1000;

    pthread_mutex_lock(&s_engine_lock);
    if (!s_engine_initialized) {
        pthread_mutex_unlock(&s_engine_lock);
        vm_monitor_init(NULL);
        pthread_mutex_lock(&s_engine_lock);
    }

    /* Check if already active */
    for (int i = 0; i < MAX_MONITORED_VMS; i++) {
        if (s_slots[i].is_active && strcmp(s_slots[i].vm_id, vm_id) == 0) {
            pthread_mutex_unlock(&s_engine_lock);
            return 0; /* Already running */
        }
    }

    /* Find free slot */
    int slot_idx = -1;
    for (int i = 0; i < MAX_MONITORED_VMS; i++) {
        if (!s_slots[i].is_active) {
            slot_idx = i;
            break;
        }
    }

    if (slot_idx < 0) {
        pthread_mutex_unlock(&s_engine_lock);
        return -1; /* Exceeded MAX_MONITORED_VMS */
    }

    VmMonitorSlot *slot = &s_slots[slot_idx];
    memset(slot->vm_id, 0, sizeof(slot->vm_id));
    snprintf(slot->vm_id, sizeof(slot->vm_id), "%s", vm_id);
    slot->interval_ms    = interval_ms;
    slot->mgr            = mgr;
    slot->stop_requested = 0;
    slot->start_time_ms  = get_time_ms();
    slot->has_guest_data = 0;
    slot->last_telemetry_rx_ms = 0;
    slot->has_integrity_warning = 0;
    slot->integrity_warning[0] = '\0';
    slot->guest_sock_fd  = -1;
    snprintf(slot->sock_path, sizeof(slot->sock_path), "/tmp/vos_agent_%s.sock", vm_id);

    memset(&slot->latest_metrics, 0, sizeof(slot->latest_metrics));
    snprintf(slot->latest_metrics.vm_id, sizeof(slot->latest_metrics.vm_id), "%s", vm_id);
    slot->latest_metrics.vm_state = VM_STATE_CREATED;
    slot->latest_metrics.freshness = TELEMETRY_UNAVAILABLE;

    slot->is_active = 1;

    if (pthread_create(&slot->worker_thread, NULL, vm_monitor_worker, slot) != 0) {
        slot->is_active = 0;
        pthread_mutex_unlock(&s_engine_lock);
        return -1;
    }

    pthread_mutex_unlock(&s_engine_lock);
    return 0;
}

int vm_monitor_stop(const char *vm_id)
{
    if (!vm_id) return -1;

    pthread_mutex_lock(&s_engine_lock);
    int slot_idx = -1;
    for (int i = 0; i < MAX_MONITORED_VMS; i++) {
        if (s_slots[i].is_active && strcmp(s_slots[i].vm_id, vm_id) == 0) {
            slot_idx = i;
            break;
        }
    }

    if (slot_idx < 0) {
        pthread_mutex_unlock(&s_engine_lock);
        return 0; /* Not active */
    }

    VmMonitorSlot *slot = &s_slots[slot_idx];
    pthread_mutex_lock(&slot->slot_lock);
    slot->stop_requested = 1;
    pthread_cond_broadcast(&slot->slot_cond);
    pthread_mutex_unlock(&slot->slot_lock);

    pthread_mutex_unlock(&s_engine_lock);

    pthread_join(slot->worker_thread, NULL);

    pthread_mutex_lock(&slot->slot_lock);
    slot->is_active = 0;
    pthread_mutex_unlock(&slot->slot_lock);
    return 0;
}

bool vm_monitor_is_active(const char *vm_id)
{
    if (!vm_id) return false;
    pthread_mutex_lock(&s_engine_lock);
    for (int i = 0; i < MAX_MONITORED_VMS; i++) {
        if (s_slots[i].is_active && strcmp(s_slots[i].vm_id, vm_id) == 0) {
            pthread_mutex_unlock(&s_engine_lock);
            return true;
        }
    }
    pthread_mutex_unlock(&s_engine_lock);
    return false;
}

/* ------------------------------------------------------------------ Queries */

int vm_monitor_get_latest(const char *vm_id, VmResourceMetrics *out)
{
    if (!vm_id || !out) return -1;

    /* Check in-memory active slots first */
    pthread_mutex_lock(&s_engine_lock);
    for (int i = 0; i < MAX_MONITORED_VMS; i++) {
        if (strcmp(s_slots[i].vm_id, vm_id) == 0 && (s_slots[i].is_active || s_slots[i].has_guest_data)) {
            pthread_mutex_lock(&s_slots[i].slot_lock);
            *out = s_slots[i].latest_metrics;
            /* Update age in real-time if telemetry exists */
            if (s_slots[i].last_telemetry_rx_ms > 0) {
                uint64_t now_ms = get_time_ms();
                out->telemetry_age_ms = (now_ms >= s_slots[i].last_telemetry_rx_ms) ?
                                        (now_ms - s_slots[i].last_telemetry_rx_ms) : 0;
            }
            pthread_mutex_unlock(&s_slots[i].slot_lock);
            pthread_mutex_unlock(&s_engine_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&s_engine_lock);

    /* Fallback to database */
    if (mon_db_open_if_needed() != 0) return -1;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT vm_id, timestamp_ms, metric_source, freshness_status, integrity_warning, qemu_pid,"
        " host_cpu_percent, host_memory_used, host_memory_total,"
        " qemu_cpu_percent, qemu_memory_rss, guest_cpu_percent,"
        " guest_memory_used, guest_memory_total, disk_read_bytes,"
        " disk_write_bytes, network_rx_bytes, network_tx_bytes"
        " FROM resource_samples WHERE vm_id=? ORDER BY id DESC LIMIT 1;";

    if (sqlite3_prepare_v2(s_mon_db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);

    int found = -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        memset(out, 0, sizeof(*out));
        snprintf(out->vm_id, sizeof(out->vm_id), "%s", (const char *)sqlite3_column_text(st, 0));
        out->timestamp_ms          = (uint64_t)sqlite3_column_int64(st, 1);
        const char *src_str        = (const char *)sqlite3_column_text(st, 2);
        const char *fresh_str      = (const char *)sqlite3_column_text(st, 3);
        const char *warn_str       = (const char *)sqlite3_column_text(st, 4);

        if (fresh_str) {
            if (strcmp(fresh_str, "LIVE") == 0) out->freshness = TELEMETRY_LIVE;
            else if (strcmp(fresh_str, "STALE") == 0) out->freshness = TELEMETRY_STALE;
            else out->freshness = TELEMETRY_UNAVAILABLE;
        }
        if (warn_str && warn_str[0] != '\0') {
            out->has_integrity_warning = 1;
            snprintf(out->integrity_warning, sizeof(out->integrity_warning), "%s", warn_str);
        }

        out->qemu_pid              = (pid_t)sqlite3_column_int(st, 5);
        out->host_cpu_percent      = sqlite3_column_double(st, 6);
        out->host_memory_used_bytes= (uint64_t)sqlite3_column_int64(st, 7);
        out->host_memory_total_bytes = (uint64_t)sqlite3_column_int64(st, 8);
        out->qemu_cpu_percent      = sqlite3_column_double(st, 9);
        out->qemu_memory_rss_bytes = (uint64_t)sqlite3_column_int64(st, 10);
        out->guest_cpu_percent     = sqlite3_column_double(st, 11);
        out->guest_memory_used_bytes  = (uint64_t)sqlite3_column_int64(st, 12);
        out->guest_memory_total_bytes = (uint64_t)sqlite3_column_int64(st, 13);
        out->disk_read_bytes       = (uint64_t)sqlite3_column_int64(st, 14);
        out->disk_write_bytes      = (uint64_t)sqlite3_column_int64(st, 15);
        out->network_rx_bytes      = (uint64_t)sqlite3_column_int64(st, 16);
        out->network_tx_bytes      = (uint64_t)sqlite3_column_int64(st, 17);

        if (src_str && strcmp(src_str, "combined") == 0) out->metric_source = METRIC_SOURCE_COMBINED;
        else if (src_str && strcmp(src_str, "guest") == 0) out->metric_source = METRIC_SOURCE_GUEST;
        else if (src_str && strcmp(src_str, "host") == 0) out->metric_source = METRIC_SOURCE_HOST;
        else out->metric_source = METRIC_SOURCE_NONE;

        out->has_guest_telemetry = (out->guest_cpu_percent >= 0.0);
        found = 0;
    }
    sqlite3_finalize(st);
    return found;
}

int vm_monitor_get_history(const char *vm_id, int max_samples,
                           VmResourceMetrics *out_samples, int *out_count)
{
    if (!vm_id || !out_samples || max_samples <= 0 || !out_count) return -1;
    *out_count = 0;

    if (mon_db_open_if_needed() != 0) return -1;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT vm_id, timestamp_ms, metric_source, freshness_status, integrity_warning, qemu_pid,"
        " host_cpu_percent, host_memory_used, host_memory_total,"
        " qemu_cpu_percent, qemu_memory_rss, guest_cpu_percent,"
        " guest_memory_used, guest_memory_total, disk_read_bytes,"
        " disk_write_bytes, network_rx_bytes, network_tx_bytes"
        " FROM resource_samples WHERE vm_id=? ORDER BY id DESC LIMIT ?;";

    if (sqlite3_prepare_v2(s_mon_db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, max_samples);

    int count = 0;
    while (sqlite3_step(st) == SQLITE_ROW && count < max_samples) {
        VmResourceMetrics *m = &out_samples[count];
        memset(m, 0, sizeof(*m));

        snprintf(m->vm_id, sizeof(m->vm_id), "%s", (const char *)sqlite3_column_text(st, 0));
        m->timestamp_ms          = (uint64_t)sqlite3_column_int64(st, 1);
        const char *src_str      = (const char *)sqlite3_column_text(st, 2);
        const char *fresh_str    = (const char *)sqlite3_column_text(st, 3);
        const char *warn_str     = (const char *)sqlite3_column_text(st, 4);

        if (fresh_str) {
            if (strcmp(fresh_str, "LIVE") == 0) m->freshness = TELEMETRY_LIVE;
            else if (strcmp(fresh_str, "STALE") == 0) m->freshness = TELEMETRY_STALE;
            else m->freshness = TELEMETRY_UNAVAILABLE;
        }
        if (warn_str && warn_str[0] != '\0') {
            m->has_integrity_warning = 1;
            snprintf(m->integrity_warning, sizeof(m->integrity_warning), "%s", warn_str);
        }

        m->qemu_pid              = (pid_t)sqlite3_column_int(st, 5);
        m->host_cpu_percent      = sqlite3_column_double(st, 6);
        m->host_memory_used_bytes= (uint64_t)sqlite3_column_int64(st, 7);
        m->host_memory_total_bytes = (uint64_t)sqlite3_column_int64(st, 8);
        m->qemu_cpu_percent      = sqlite3_column_double(st, 9);
        m->qemu_memory_rss_bytes = (uint64_t)sqlite3_column_int64(st, 10);
        m->guest_cpu_percent     = sqlite3_column_double(st, 11);
        m->guest_memory_used_bytes  = (uint64_t)sqlite3_column_int64(st, 12);
        m->guest_memory_total_bytes = (uint64_t)sqlite3_column_int64(st, 13);
        m->disk_read_bytes       = (uint64_t)sqlite3_column_int64(st, 14);
        m->disk_write_bytes      = (uint64_t)sqlite3_column_int64(st, 15);
        m->network_rx_bytes      = (uint64_t)sqlite3_column_int64(st, 16);
        m->network_tx_bytes      = (uint64_t)sqlite3_column_int64(st, 17);

        if (src_str && strcmp(src_str, "combined") == 0) m->metric_source = METRIC_SOURCE_COMBINED;
        else if (src_str && strcmp(src_str, "guest") == 0) m->metric_source = METRIC_SOURCE_GUEST;
        else if (src_str && strcmp(src_str, "host") == 0) m->metric_source = METRIC_SOURCE_HOST;
        else m->metric_source = METRIC_SOURCE_NONE;

        m->has_guest_telemetry = (m->guest_cpu_percent >= 0.0);
        count++;
    }
    sqlite3_finalize(st);
    *out_count = count;
    return 0;
}

int vm_monitor_get_processes(const char *vm_id, const ProcessFilter *filter,
                             GuestProcessMetric *out_procs, int max_procs,
                             int *out_count)
{
    if (!vm_id || !out_procs || max_procs <= 0 || !out_count) return -1;
    *out_count = 0;

    pthread_mutex_lock(&s_engine_lock);
    for (int i = 0; i < MAX_MONITORED_VMS; i++) {
        if (strcmp(s_slots[i].vm_id, vm_id) == 0 && s_slots[i].has_guest_data) {
            pthread_mutex_lock(&s_slots[i].slot_lock);
            const GuestTelemetryPayload *gp = &s_slots[i].latest_guest;
            if (filter) {
                process_filter_list(gp->processes, gp->process_count, filter,
                                    out_procs, max_procs, out_count);
            } else {
                int to_copy = (gp->process_count > max_procs) ? max_procs : gp->process_count;
                memcpy(out_procs, gp->processes, sizeof(GuestProcessMetric) * (size_t)to_copy);
                *out_count = to_copy;
            }
            pthread_mutex_unlock(&s_slots[i].slot_lock);
            pthread_mutex_unlock(&s_engine_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&s_engine_lock);

    return 0;
}

int vm_monitor_ingest_telemetry(const char *vm_id, const GuestTelemetryPayload *payload)
{
    if (!vm_id || !payload) return -1;

    pthread_mutex_lock(&s_engine_lock);
    if (!s_engine_initialized) {
        pthread_mutex_unlock(&s_engine_lock);
        vm_monitor_init(NULL);
        pthread_mutex_lock(&s_engine_lock);
    }

    int slot_idx = -1;
    for (int i = 0; i < MAX_MONITORED_VMS; i++) {
        if (strcmp(s_slots[i].vm_id, vm_id) == 0) {
            slot_idx = i;
            break;
        }
    }

    if (slot_idx < 0) {
        for (int i = 0; i < MAX_MONITORED_VMS; i++) {
            if (!s_slots[i].is_active && s_slots[i].vm_id[0] == '\0') {
                slot_idx = i;
                snprintf(s_slots[i].vm_id, sizeof(s_slots[i].vm_id), "%s", vm_id);
                break;
            }
        }
    }

    if (slot_idx < 0) {
        pthread_mutex_unlock(&s_engine_lock);
        return -1;
    }

    VmMonitorSlot *slot = &s_slots[slot_idx];

    /* Fetch config for validation */
    VmConfig cfg;
    vm_config_init(&cfg);
    VmState st = VM_STATE_CREATED;
    int has_cfg = 0;
    if (slot->mgr) {
        VmHandle h;
        if (vm_get_handle(slot->mgr, vm_id, &h) == 0) {
            cfg = h.cfg;
            st  = h.state;
            has_cfg = 1;
        }
    }
    if (!has_cfg) {
        vm_db_get(vm_id, &cfg, &st);
    }

    char warn[256] = {0};
    int warn_cnt = vm_monitor_validate_telemetry(&cfg, payload, warn, sizeof(warn));

    pthread_mutex_lock(&slot->slot_lock);
    slot->latest_guest = *payload;
    slot->has_guest_data = 1;
    slot->last_telemetry_rx_ms = get_time_ms();
    if (warn_cnt > 0) {
        slot->has_integrity_warning = 1;
        snprintf(slot->integrity_warning, sizeof(slot->integrity_warning), "%s", warn);
    } else {
        slot->has_integrity_warning = 0;
        slot->integrity_warning[0] = '\0';
    }

    /* Update latest_metrics */
    VmResourceMetrics *rm = &slot->latest_metrics;
    snprintf(rm->vm_id, sizeof(rm->vm_id), "%s", vm_id);
    rm->timestamp_ms = slot->last_telemetry_rx_ms;
    rm->vm_state = st;
    rm->vcpu_allocated = cfg.cpu_count;
    rm->memory_mb_configured = cfg.memory_mb;
    rm->has_guest_telemetry = 1;
    rm->freshness = TELEMETRY_LIVE;
    rm->telemetry_age_ms = 0;
    rm->last_telemetry_timestamp_ms = slot->last_telemetry_rx_ms;
    rm->has_integrity_warning = slot->has_integrity_warning;
    snprintf(rm->integrity_warning, sizeof(rm->integrity_warning), "%s", slot->integrity_warning);

    rm->guest_cpu_percent            = payload->cpu.total_percent;
    rm->guest_memory_total_bytes     = payload->memory.total_bytes;
    rm->guest_memory_used_bytes      = payload->memory.used_bytes;
    rm->guest_memory_available_bytes = payload->memory.available_bytes;
    rm->disk_read_bytes              = payload->disk.read_bytes;
    rm->disk_write_bytes             = payload->disk.write_bytes;
    rm->network_rx_bytes             = payload->network.rx_bytes;
    rm->network_tx_bytes             = payload->network.tx_bytes;
    rm->guest_process_count          = payload->process_count;

    /* Resolve QEMU PID and liveness */
    pid_t qpid = 0;
    if (slot->mgr) {
        VmHandle h;
        if (vm_get_handle(slot->mgr, vm_id, &h) == 0 && h.pid > 0) {
            qpid = h.pid;
        }
    }
    if (qpid <= 0 && mon_db_open_if_needed() == 0) {
        sqlite3_stmt *st_pid = NULL;
        const char *sql_pid = "SELECT pid FROM vms WHERE id=? AND state='running' LIMIT 1;";
        if (sqlite3_prepare_v2(s_mon_db, sql_pid, -1, &st_pid, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st_pid, 1, vm_id, -1, SQLITE_STATIC);
            if (sqlite3_step(st_pid) == SQLITE_ROW) {
                qpid = (pid_t)sqlite3_column_int(st_pid, 0);
            }
            sqlite3_finalize(st_pid);
        }
    }

    int qemu_alive = (qpid > 0 && kill(qpid, 0) == 0);

    /* Sample host system */
    HostMetrics host_sys;
    if (host_monitor_sample(&host_sys) == 0) {
        rm->host_cpu_percent        = host_sys.cpu_total_percent;
        rm->host_memory_total_bytes = host_sys.memory_total_bytes;
        rm->host_memory_used_bytes  = host_sys.memory_used_bytes;
    } else {
        rm->host_cpu_percent        = -1.0;
        rm->host_memory_total_bytes = 0;
        rm->host_memory_used_bytes  = 0;
    }

    /* Sample QEMU hypervisor process */
    if (qemu_alive) {
        ProcessHostMetrics pm;
        host_monitor_sample_process(qpid, &pm);
        rm->qemu_pid              = qpid;
        rm->qemu_cpu_percent      = pm.cpu_percent;
        rm->qemu_memory_rss_bytes = pm.memory_rss_bytes;
        rm->metric_source         = METRIC_SOURCE_COMBINED;
    } else {
        rm->qemu_pid              = 0;
        rm->qemu_cpu_percent      = -1.0;
        rm->qemu_memory_rss_bytes = 0;
        rm->metric_source         = METRIC_SOURCE_GUEST;
    }

    GuestProcessMetric proc_copy[AGENT_MAX_PROCESSES];
    int proc_cnt = payload->process_count;
    if (proc_cnt > AGENT_MAX_PROCESSES) proc_cnt = AGENT_MAX_PROCESSES;
    memcpy(proc_copy, payload->processes, sizeof(GuestProcessMetric) * (size_t)proc_cnt);

    pthread_mutex_unlock(&slot->slot_lock);
    pthread_mutex_unlock(&s_engine_lock);

    db_save_sample(rm, proc_copy, proc_cnt);
    return 0;
}

int vm_monitor_get_host_metrics(HostMetrics *out)
{
    return host_monitor_sample(out);
}

int vm_monitor_apply_limit(const char *vm_id, const AppResourceLimit *limit,
                           char *err_buf, size_t err_sz)
{
    if (!vm_id || !limit) return -1;

    char err_local[256] = {0};
    ResourceLimitStatus st_code = resource_limits_apply_cgroup_v2(NULL, limit, err_local, sizeof(err_local));
    const char *st_name = resource_limit_status_name(st_code);
    int db_rc = -1;

    if (mon_db_open_if_needed() == 0) {
        sqlite3_stmt *st = NULL;
        const char *sql =
            "INSERT INTO resource_limits("
            " vm_id, process_identifier, cpu_limit, memory_limit, enforcement_status, detail, updated_at"
            ") VALUES (?, ?, ?, ?, ?, ?, datetime('now','localtime'));";

        if (sqlite3_prepare_v2(s_mon_db, sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text  (st, 1, vm_id, -1, SQLITE_STATIC);
            sqlite3_bind_text  (st, 2, limit->process_name, -1, SQLITE_STATIC);
            sqlite3_bind_double(st, 3, limit->cpu_quota_percent);
            sqlite3_bind_int64 (st, 4, (sqlite3_int64)limit->memory_max_bytes);
            sqlite3_bind_text  (st, 5, st_name, -1, SQLITE_STATIC);
            sqlite3_bind_text  (st, 6, err_local[0] ? err_local : "Enforcement recorded", -1, SQLITE_STATIC);
            if (sqlite3_step(st) == SQLITE_DONE) {
                db_rc = 0;
            }
            sqlite3_finalize(st);
        }
    }

    if (err_buf && err_sz > 0) {
        snprintf(err_buf, err_sz, "%s: %s", st_name, err_local[0] ? err_local : "Saved to resource_limits catalog");
    }
    return (db_rc == 0) ? 0 : -1;
}

int vm_monitor_get_limit_status(const char *vm_id, const char *proc_name,
                                ResourceEnforcementRecord *out)
{
    if (!vm_id || !proc_name || !out) return -1;
    memset(out, 0, sizeof(*out));
    snprintf(out->vm_id, sizeof(out->vm_id), "%s", vm_id);
    snprintf(out->process_identifier, sizeof(out->process_identifier), "%s", proc_name);

    if (mon_db_open_if_needed() != 0) return -1;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT enforcement_status, detail FROM resource_limits WHERE vm_id=? AND process_identifier=?"
        " ORDER BY id DESC LIMIT 1;";

    if (sqlite3_prepare_v2(s_mon_db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, proc_name, -1, SQLITE_STATIC);

    int rc = -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char *st_txt  = (const char *)sqlite3_column_text(st, 0);
        const char *det_txt = (const char *)sqlite3_column_text(st, 1);
        if (st_txt) {
            if (strcmp(st_txt, "enforced") == 0) out->status = LIMIT_STATUS_ENFORCED;
            else if (strcmp(st_txt, "unsupported") == 0) out->status = LIMIT_STATUS_UNSUPPORTED;
            else if (strcmp(st_txt, "permission_denied") == 0) out->status = LIMIT_STATUS_PERMISSION_DENIED;
            else if (strcmp(st_txt, "protected_process") == 0) out->status = LIMIT_STATUS_PROTECTED_PROCESS;
            else out->status = LIMIT_STATUS_PENDING;
        }
        snprintf(out->detail, sizeof(out->detail), "%s", det_txt ? det_txt : "");
        rc = 0;
    }
    sqlite3_finalize(st);
    return rc;
}
