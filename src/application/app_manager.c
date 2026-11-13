/* app_manager.c – Phase 5 Application Management & Virtual Environment Integration
 *
 * Implements:
 *   - Application Registry (scoped per VM, persistent in SQLite)
 *   - Application Discovery (invoking guest agent RPC over virtio-serial)
 *   - Application Resource Profiles (cgroups v2 limits mapping)
 *   - Application Launcher with verified pre-execution cgroup handshake
 *   - Application Lifecycle Management (PID reuse protected, graceful/forced stop)
 *   - Monitoring samples collection and persistence for Flutter time-series charts
 */

#include "app_manager.h"
#include "monitoring/resource_manager.h"
#include "monitoring/agent_protocol.h"
#include "sqlite3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

/* Safe identifier validation (alphanumeric, dash, underscore, dot; no path traversals) */
static int app_is_safe_name(const char *name)
{
    if (!name || name[0] == '\0') return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) return 0;
    for (const char *p = name; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.')) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ Globals & Mutex */

static sqlite3 *s_app_db = NULL;
static char s_app_db_path[512] = "virtual_os.db";
static pthread_mutex_t s_app_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ Schema DDL */

static const char *s_app_schema =
    "PRAGMA foreign_keys=ON;"
    "PRAGMA journal_mode=WAL;"

    /* 1. Applications Registry Table */
    "CREATE TABLE IF NOT EXISTS applications ("
    "  id                 INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  app_id             TEXT    NOT NULL,"
    "  vm_id              TEXT    NOT NULL,"
    "  name               TEXT    NOT NULL,"
    "  executable_path    TEXT    NOT NULL,"
    "  description        TEXT    NOT NULL DEFAULT '',"
    "  category           TEXT    NOT NULL DEFAULT 'Utility',"
    "  version            TEXT    NOT NULL DEFAULT '',"
    "  enabled            INTEGER NOT NULL DEFAULT 1,"
    "  default_profile_id TEXT    NOT NULL DEFAULT 'default',"
    "  registered_at      INTEGER NOT NULL,"
    "  last_launched_at   INTEGER NOT NULL DEFAULT 0,"
    "  launch_count       INTEGER NOT NULL DEFAULT 0,"
    "  UNIQUE(vm_id, app_id)"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_apps_vm_id ON applications(vm_id);"
    "CREATE INDEX IF NOT EXISTS idx_apps_cat ON applications(category);"

    /* 2. Application Resource Profiles Table */
    "CREATE TABLE IF NOT EXISTS application_profiles ("
    "  id               INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  profile_id       TEXT    NOT NULL UNIQUE,"
    "  name             TEXT    NOT NULL,"
    "  description      TEXT    NOT NULL DEFAULT '',"
    "  cpu_cores        REAL    NOT NULL DEFAULT 0.0,"
    "  cpu_quota_us     INTEGER NOT NULL DEFAULT 0,"
    "  cpu_period_us    INTEGER NOT NULL DEFAULT 100000,"
    "  cpu_weight       INTEGER NOT NULL DEFAULT 100,"
    "  memory_max_bytes INTEGER NOT NULL DEFAULT 0,"
    "  pids_max         INTEGER NOT NULL DEFAULT -1,"
    "  io_read_bps      INTEGER NOT NULL DEFAULT 0,"
    "  io_write_bps     INTEGER NOT NULL DEFAULT 0,"
    "  created_at       INTEGER NOT NULL,"
    "  updated_at       INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_prof_id ON application_profiles(profile_id);"

    /* 3. Application Instances Table */
    "CREATE TABLE IF NOT EXISTS application_instances ("
    "  id                 INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  instance_id        TEXT    NOT NULL UNIQUE,"
    "  app_id             TEXT    NOT NULL,"
    "  vm_id              TEXT    NOT NULL,"
    "  guest_pid          INTEGER NOT NULL,"
    "  process_start_time INTEGER NOT NULL DEFAULT 0,"
    "  cgroup_path        TEXT    NOT NULL DEFAULT '',"
    "  state              TEXT    NOT NULL DEFAULT 'STARTING',"
    "  profile_id         TEXT    NOT NULL DEFAULT 'default',"
    "  started_at         INTEGER NOT NULL,"
    "  stopped_at         INTEGER NOT NULL DEFAULT 0,"
    "  exit_code          INTEGER NOT NULL DEFAULT -1,"
    "  failure_reason     TEXT    NOT NULL DEFAULT ''"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_inst_vm_app ON application_instances(vm_id, app_id);"
    "CREATE INDEX IF NOT EXISTS idx_inst_state ON application_instances(state);"

    /* 4. Application Events Table */
    "CREATE TABLE IF NOT EXISTS application_events ("
    "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  instance_id   TEXT    NOT NULL,"
    "  app_id        TEXT    NOT NULL,"
    "  vm_id         TEXT    NOT NULL,"
    "  event_type    TEXT    NOT NULL,"
    "  event_details TEXT    NOT NULL DEFAULT '',"
    "  created_at    INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_app_events_inst ON application_events(instance_id);"

    /* 5. Application Resource Samples Table (Time-Series for Flutter Charts) */
    "CREATE TABLE IF NOT EXISTS application_resource_samples ("
    "  id                   INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  instance_id          TEXT    NOT NULL,"
    "  app_id               TEXT    NOT NULL,"
    "  vm_id                TEXT    NOT NULL,"
    "  cpu_usage_pct        REAL    NOT NULL DEFAULT 0.0,"
    "  cpu_time_usec        INTEGER NOT NULL DEFAULT 0,"
    "  throttled_usec       INTEGER NOT NULL DEFAULT 0,"
    "  memory_current_bytes INTEGER NOT NULL DEFAULT 0,"
    "  memory_peak_bytes    INTEGER NOT NULL DEFAULT 0,"
    "  pids_current         INTEGER NOT NULL DEFAULT 0,"
    "  pids_max             INTEGER NOT NULL DEFAULT -1,"
    "  io_read_bytes        INTEGER NOT NULL DEFAULT 0,"
    "  io_write_bytes       INTEGER NOT NULL DEFAULT 0,"
    "  sample_timestamp     INTEGER NOT NULL,"
    "  freshness            TEXT    NOT NULL DEFAULT 'FRESH',"
    "  verification_state   TEXT    NOT NULL DEFAULT 'VERIFIED'"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_app_samples_ts ON application_resource_samples(instance_id, sample_timestamp);";

/* ------------------------------------------------------------------ String Utilities */

const char *app_result_str(AppResult res)
{
    switch (res) {
        case APP_SUCCESS:                  return "SUCCESS";
        case APP_ERR_INVALID_PARAM:        return "INVALID_PARAMETER";
        case APP_ERR_NOT_FOUND:            return "NOT_FOUND";
        case APP_ERR_ALREADY_EXISTS:       return "ALREADY_EXISTS";
        case APP_ERR_VM_NOT_FOUND:         return "VM_NOT_FOUND";
        case APP_ERR_VM_NOT_RUNNING:       return "VM_NOT_RUNNING";
        case APP_ERR_AGENT_COMM:           return "COMMUNICATION_FAILED";
        case APP_ERR_CGROUP_FAILED:        return "CGROUP_ENFORCEMENT_FAILED";
        case APP_ERR_LAUNCH_FAILED:        return "APPLICATION_LAUNCH_FAILED";
        case APP_ERR_STOP_FAILED:          return "APPLICATION_STOP_FAILED";
        case APP_ERR_PID_IDENTITY_CHANGED: return "PID_IDENTITY_CHANGED";
        case APP_ERR_DB:                   return "DATABASE_ERROR";
        case APP_ERR_UNSUPPORTED:          return "UNSUPPORTED_OPERATION";
        default:                           return "UNKNOWN_ERROR";
    }
}

const char *app_state_name(AppState state)
{
    switch (state) {
        case APP_STATE_REGISTERED: return "REGISTERED";
        case APP_STATE_STARTING:   return "STARTING";
        case APP_STATE_RUNNING:    return "RUNNING";
        case APP_STATE_STOPPING:   return "STOPPING";
        case APP_STATE_STOPPED:    return "STOPPED";
        case APP_STATE_FAILED:     return "FAILED";
        default:                   return "UNKNOWN";
    }
}

AppState app_state_from_name(const char *name)
{
    if (!name) return APP_STATE_UNKNOWN;
    if (strcmp(name, "REGISTERED") == 0) return APP_STATE_REGISTERED;
    if (strcmp(name, "STARTING") == 0)   return APP_STATE_STARTING;
    if (strcmp(name, "RUNNING") == 0)    return APP_STATE_RUNNING;
    if (strcmp(name, "STOPPING") == 0)   return APP_STATE_STOPPING;
    if (strcmp(name, "STOPPED") == 0)    return APP_STATE_STOPPED;
    if (strcmp(name, "FAILED") == 0)     return APP_STATE_FAILED;
    return APP_STATE_UNKNOWN;
}

/* ------------------------------------------------------------------ Schema Setup & Defaults */

static void seed_default_profiles(sqlite3 *db)
{
    const char *check_sql = "SELECT COUNT(*) FROM application_profiles;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, check_sql, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0) == 0) {
            uint64_t now = (uint64_t)time(NULL);
            const char *seed =
                "INSERT INTO application_profiles "
                "(profile_id, name, description, cpu_cores, cpu_quota_us, cpu_period_us, cpu_weight, memory_max_bytes, pids_max, io_read_bps, io_write_bps, created_at, updated_at) VALUES "
                "('default', 'Default Profile', 'Standard unconstrained profile', 0.0, 0, 100000, 100, 0, -1, 0, 0, ?, ?),"
                "('restricted', 'Restricted Sandbox', '50% CPU, 128MB RAM, 20 PIDs, 5MB/s I/O', 0.5, 50000, 100000, 100, 134217728, 20, 5242880, 5242880, ?, ?),"
                "('performance', 'Performance Profile', '1.5 vCPUs, 512MB RAM, 50 PIDs', 1.5, 150000, 100000, 200, 536870912, 50, 0, 0, ?, ?);";

            sqlite3_stmt *ins = NULL;
            if (sqlite3_prepare_v2(db, seed, -1, &ins, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(ins, 1, (sqlite3_int64)now);
                sqlite3_bind_int64(ins, 2, (sqlite3_int64)now);
                sqlite3_bind_int64(ins, 3, (sqlite3_int64)now);
                sqlite3_bind_int64(ins, 4, (sqlite3_int64)now);
                sqlite3_bind_int64(ins, 5, (sqlite3_int64)now);
                sqlite3_bind_int64(ins, 6, (sqlite3_int64)now);
                sqlite3_step(ins);
                sqlite3_finalize(ins);
            }
        }
        sqlite3_finalize(st);
    }
}

int app_manager_ensure_schema(sqlite3 *db)
{
    if (!db) return -1;
    char *err = NULL;
    int rc = sqlite3_exec(db, s_app_schema, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return -1;
    }
    seed_default_profiles(db);
    return 0;
}

AppResult app_manager_init(const char *db_path)
{
    pthread_mutex_lock(&s_app_mutex);
    if (db_path && db_path[0]) {
        snprintf(s_app_db_path, sizeof(s_app_db_path), "%s", db_path);
    }
    if (s_app_db) {
        sqlite3_close(s_app_db);
        s_app_db = NULL;
    }

    if (sqlite3_open(s_app_db_path, &s_app_db) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_busy_timeout(s_app_db, 2000);
    app_manager_ensure_schema(s_app_db);

    /* Also ensure resource manager subsystem is initialized with the same database */
    resource_manager_init(s_app_db_path);

    pthread_mutex_unlock(&s_app_mutex);
    return APP_SUCCESS;
}

void app_manager_shutdown(void)
{
    pthread_mutex_lock(&s_app_mutex);
    if (s_app_db) {
        sqlite3_close(s_app_db);
        s_app_db = NULL;
    }
    pthread_mutex_unlock(&s_app_mutex);
}

static sqlite3 *get_db(void)
{
    if (!s_app_db) {
        app_manager_init(NULL);
    }
    return s_app_db;
}

/* ------------------------------------------------------------------ Application Registry */

AppResult app_manager_register_app(const char *vm_id,
                                   const char *app_id,
                                   const char *name,
                                   const char *executable_path,
                                   const char *description,
                                   const char *category,
                                   const char *version,
                                   const char *default_profile_id,
                                   char *err_buf, size_t err_sz)
{
    if (err_buf && err_sz > 0) err_buf[0] = '\0';
    if (!vm_id || !vm_id[0] || !app_id || !app_id[0] || !name || !name[0] || !executable_path || !executable_path[0]) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Missing required parameters (vm_id, app_id, name, path)");
        return APP_ERR_INVALID_PARAM;
    }

    /* Validate identifier safety */
    if (!app_is_safe_name(app_id)) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Invalid application identifier '%s'", app_id);
        return APP_ERR_INVALID_PARAM;
    }

    /* Executable path must be absolute and contain no traversal */
    if (executable_path[0] != '/' || strstr(executable_path, "..")) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Executable path must be absolute and contain no traversal");
        return APP_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql =
        "INSERT INTO applications "
        "(app_id, vm_id, name, executable_path, description, category, version, enabled, default_profile_id, registered_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 1, ?, ?);";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "DB prepare error: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    uint64_t now = (uint64_t)time(NULL);
    sqlite3_bind_text(st, 1, app_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, vm_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, executable_path, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, description ? description : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 6, (category && category[0]) ? category : "Utility", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 7, version ? version : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 8, (default_profile_id && default_profile_id[0]) ? default_profile_id : "default", -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 9, (sqlite3_int64)now);

    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);

    if (rc == SQLITE_CONSTRAINT) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Application '%s' is already registered for VM '%s'", app_id, vm_id);
        return APP_ERR_ALREADY_EXISTS;
    } else if (rc != SQLITE_DONE) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "DB insert failed: %s", sqlite3_errmsg(db));
        return APP_ERR_DB;
    }

    return APP_SUCCESS;
}

AppResult app_manager_update_app(const char *vm_id,
                                 const char *app_id,
                                 const char *name,
                                 const char *description,
                                 const char *category,
                                 const char *version,
                                 const char *default_profile_id,
                                 char *err_buf, size_t err_sz)
{
    if (!vm_id || !app_id) return APP_ERR_INVALID_PARAM;

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql =
        "UPDATE applications SET name=?, description=?, category=?, version=?, default_profile_id=? "
        "WHERE vm_id=? AND app_id=?;";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_bind_text(st, 1, name ? name : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, description ? description : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, category ? category : "Utility", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, version ? version : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, default_profile_id ? default_profile_id : "default", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 6, vm_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 7, app_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);

    if (rc != SQLITE_DONE || changes == 0) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Application '%s' not found for VM '%s'", app_id, vm_id);
        return APP_ERR_NOT_FOUND;
    }
    return APP_SUCCESS;
}

AppResult app_manager_unregister_app(const char *vm_id,
                                     const char *app_id,
                                     char *err_buf, size_t err_sz)
{
    if (!vm_id || !app_id) return APP_ERR_INVALID_PARAM;

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql = "DELETE FROM applications WHERE vm_id=? AND app_id=?;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, app_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);

    if (rc != SQLITE_DONE || changes == 0) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Application '%s' not found for VM '%s'", app_id, vm_id);
        return APP_ERR_NOT_FOUND;
    }
    return APP_SUCCESS;
}

AppResult app_manager_get_app(const char *vm_id,
                              const char *app_id,
                              ApplicationEntry *out_app,
                              char *err_buf, size_t err_sz)
{
    if (!vm_id || !app_id || !out_app) return APP_ERR_INVALID_PARAM;
    memset(out_app, 0, sizeof(*out_app));

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql =
        "SELECT id, app_id, vm_id, name, executable_path, description, category, version, "
        "enabled, default_profile_id, registered_at, last_launched_at, launch_count "
        "FROM applications WHERE vm_id=? AND app_id=?;";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, app_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        out_app->db_id = sqlite3_column_int64(st, 0);
        snprintf(out_app->app_id, sizeof(out_app->app_id), "%s", (const char *)sqlite3_column_text(st, 1));
        snprintf(out_app->vm_id, sizeof(out_app->vm_id), "%s", (const char *)sqlite3_column_text(st, 2));
        snprintf(out_app->name, sizeof(out_app->name), "%s", (const char *)sqlite3_column_text(st, 3));
        snprintf(out_app->executable_path, sizeof(out_app->executable_path), "%s", (const char *)sqlite3_column_text(st, 4));
        snprintf(out_app->description, sizeof(out_app->description), "%s", (const char *)sqlite3_column_text(st, 5));
        snprintf(out_app->category, sizeof(out_app->category), "%s", (const char *)sqlite3_column_text(st, 6));
        snprintf(out_app->version, sizeof(out_app->version), "%s", (const char *)sqlite3_column_text(st, 7));
        out_app->enabled = sqlite3_column_int(st, 8);
        snprintf(out_app->default_profile_id, sizeof(out_app->default_profile_id), "%s", (const char *)sqlite3_column_text(st, 9));
        out_app->registered_at = (uint64_t)sqlite3_column_int64(st, 10);
        out_app->last_launched_at = (uint64_t)sqlite3_column_int64(st, 11);
        out_app->launch_count = sqlite3_column_int(st, 12);
        sqlite3_finalize(st);
        pthread_mutex_unlock(&s_app_mutex);
        return APP_SUCCESS;
    }

    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);
    if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Application '%s' not registered in VM '%s'", app_id, vm_id);
    return APP_ERR_NOT_FOUND;
}

AppResult app_manager_list_apps(const char *vm_id,
                                const char *category_filter,
                                ApplicationEntry *out_apps,
                                int max_apps,
                                int *out_count)
{
    if (!vm_id || !out_apps || max_apps <= 0 || !out_count) return APP_ERR_INVALID_PARAM;
    *out_count = 0;

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    char sql[512];
    if (category_filter && category_filter[0]) {
        snprintf(sql, sizeof(sql),
                 "SELECT id, app_id, vm_id, name, executable_path, description, category, version, "
                 "enabled, default_profile_id, registered_at, last_launched_at, launch_count "
                 "FROM applications WHERE vm_id=? AND category=? ORDER BY name ASC LIMIT %d;", max_apps);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT id, app_id, vm_id, name, executable_path, description, category, version, "
                 "enabled, default_profile_id, registered_at, last_launched_at, launch_count "
                 "FROM applications WHERE vm_id=? ORDER BY name ASC LIMIT %d;", max_apps);
    }

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
    if (category_filter && category_filter[0]) {
        sqlite3_bind_text(st, 2, category_filter, -1, SQLITE_STATIC);
    }

    int count = 0;
    while (sqlite3_step(st) == SQLITE_ROW && count < max_apps) {
        ApplicationEntry *app = &out_apps[count++];
        app->db_id = sqlite3_column_int64(st, 0);
        snprintf(app->app_id, sizeof(app->app_id), "%s", (const char *)sqlite3_column_text(st, 1));
        snprintf(app->vm_id, sizeof(app->vm_id), "%s", (const char *)sqlite3_column_text(st, 2));
        snprintf(app->name, sizeof(app->name), "%s", (const char *)sqlite3_column_text(st, 3));
        snprintf(app->executable_path, sizeof(app->executable_path), "%s", (const char *)sqlite3_column_text(st, 4));
        snprintf(app->description, sizeof(app->description), "%s", (const char *)sqlite3_column_text(st, 5));
        snprintf(app->category, sizeof(app->category), "%s", (const char *)sqlite3_column_text(st, 6));
        snprintf(app->version, sizeof(app->version), "%s", (const char *)sqlite3_column_text(st, 7));
        app->enabled = sqlite3_column_int(st, 8);
        snprintf(app->default_profile_id, sizeof(app->default_profile_id), "%s", (const char *)sqlite3_column_text(st, 9));
        app->registered_at = (uint64_t)sqlite3_column_int64(st, 10);
        app->last_launched_at = (uint64_t)sqlite3_column_int64(st, 11);
        app->launch_count = sqlite3_column_int(st, 12);
    }

    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);
    *out_count = count;
    return APP_SUCCESS;
}

AppResult app_manager_set_enabled(const char *vm_id,
                                  const char *app_id,
                                  int enabled,
                                  char *err_buf, size_t err_sz)
{
    if (!vm_id || !app_id) return APP_ERR_INVALID_PARAM;

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql = "UPDATE applications SET enabled=? WHERE vm_id=? AND app_id=?;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_bind_int(st, 1, enabled ? 1 : 0);
    sqlite3_bind_text(st, 2, vm_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, app_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);

    if (rc != SQLITE_DONE || changes == 0) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Application '%s' not found for VM '%s'", app_id, vm_id);
        return APP_ERR_NOT_FOUND;
    }
    return APP_SUCCESS;
}

/* ------------------------------------------------------------------ Application Discovery */

AppResult app_manager_discover_apps(const char *vm_id,
                                    AgentAppDiscoveryList *out_discovery,
                                    char *err_buf, size_t err_sz)
{
    if (err_buf && err_sz > 0) err_buf[0] = '\0';
    if (!vm_id || !out_discovery) return APP_ERR_INVALID_PARAM;
    memset(out_discovery, 0, sizeof(*out_discovery));

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "disc_%llu", (unsigned long long)time(NULL));
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_DISCOVER_APPS;

    AgentResourceResponse resp;
    memset(&resp, 0, sizeof(resp));

    ResourceResult r_res = resource_manager_rpc(vm_id, &req, &resp, 5000);
    if (r_res != RESOURCE_SUCCESS || resp.status_code != AGENT_STATUS_SUCCESS) {
        if (err_buf && err_sz > 0) {
            snprintf(err_buf, err_sz, "Discovery RPC failed: %s (%s)",
                     resp.error_message[0] ? resp.error_message : resource_result_str(r_res),
                     resp.status_msg);
        }
        return APP_ERR_AGENT_COMM;
    }

    *out_discovery = resp.discovery;
    return APP_SUCCESS;
}

/* ------------------------------------------------------------------ Application Profiles */

AppResult app_manager_create_profile(const char *profile_id,
                                     const char *name,
                                     const char *description,
                                     double cpu_cores,
                                     int cpu_weight,
                                     uint64_t memory_max_bytes,
                                     int64_t pids_max,
                                     uint64_t io_read_bps,
                                     uint64_t io_write_bps,
                                     char *err_buf, size_t err_sz)
{
    if (err_buf && err_sz > 0) err_buf[0] = '\0';
    if (!profile_id || !profile_id[0] || !name || !name[0]) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Missing profile_id or name");
        return APP_ERR_INVALID_PARAM;
    }

    if (!app_is_safe_name(profile_id)) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Invalid profile identifier '%s'", profile_id);
        return APP_ERR_INVALID_PARAM;
    }

    if (cpu_cores < 0.0 || (cpu_weight != 0 && (cpu_weight < 1 || cpu_weight > 10000)) || pids_max < -1) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Invalid profile resource limits (cores=%.2f, weight=%d, pids=%lld)", cpu_cores, cpu_weight, (long long)pids_max);
        return APP_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql =
        "INSERT INTO application_profiles "
        "(profile_id, name, description, cpu_cores, cpu_quota_us, cpu_period_us, cpu_weight, "
        "memory_max_bytes, pids_max, io_read_bps, io_write_bps, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, 100000, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    int64_t quota = (cpu_cores > 0.0) ? (int64_t)(cpu_cores * 100000.0) : 0;
    uint64_t now = (uint64_t)time(NULL);

    sqlite3_bind_text(st, 1, profile_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, description ? description : "", -1, SQLITE_STATIC);
    sqlite3_bind_double(st, 4, cpu_cores);
    sqlite3_bind_int64(st, 5, quota);
    sqlite3_bind_int(st, 6, cpu_weight > 0 ? cpu_weight : 100);
    sqlite3_bind_int64(st, 7, (sqlite3_int64)memory_max_bytes);
    sqlite3_bind_int64(st, 8, pids_max > 0 ? pids_max : -1);
    sqlite3_bind_int64(st, 9, (sqlite3_int64)io_read_bps);
    sqlite3_bind_int64(st, 10, (sqlite3_int64)io_write_bps);
    sqlite3_bind_int64(st, 11, (sqlite3_int64)now);
    sqlite3_bind_int64(st, 12, (sqlite3_int64)now);

    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);

    if (rc == SQLITE_CONSTRAINT) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Profile '%s' already exists", profile_id);
        return APP_ERR_ALREADY_EXISTS;
    } else if (rc != SQLITE_DONE) {
        return APP_ERR_DB;
    }
    return APP_SUCCESS;
}

AppResult app_manager_update_profile(const char *profile_id,
                                     const char *name,
                                     const char *description,
                                     double cpu_cores,
                                     int cpu_weight,
                                     uint64_t memory_max_bytes,
                                     int64_t pids_max,
                                     uint64_t io_read_bps,
                                     uint64_t io_write_bps,
                                     char *err_buf, size_t err_sz)
{
    if (!profile_id || !name) return APP_ERR_INVALID_PARAM;
    if (cpu_cores < 0.0 || (cpu_weight != 0 && (cpu_weight < 1 || cpu_weight > 10000)) || pids_max < -1) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Invalid profile resource limits (cores=%.2f, weight=%d, pids=%lld)", cpu_cores, cpu_weight, (long long)pids_max);
        return APP_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql =
        "UPDATE application_profiles SET "
        "name=?, description=?, cpu_cores=?, cpu_quota_us=?, cpu_weight=?, "
        "memory_max_bytes=?, pids_max=?, io_read_bps=?, io_write_bps=?, updated_at=? "
        "WHERE profile_id=?;";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    int64_t quota = (cpu_cores > 0.0) ? (int64_t)(cpu_cores * 100000.0) : 0;
    uint64_t now = (uint64_t)time(NULL);

    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, description ? description : "", -1, SQLITE_STATIC);
    sqlite3_bind_double(st, 3, cpu_cores);
    sqlite3_bind_int64(st, 4, quota);
    sqlite3_bind_int(st, 5, cpu_weight > 0 ? cpu_weight : 100);
    sqlite3_bind_int64(st, 6, (sqlite3_int64)memory_max_bytes);
    sqlite3_bind_int64(st, 7, pids_max > 0 ? pids_max : -1);
    sqlite3_bind_int64(st, 8, (sqlite3_int64)io_read_bps);
    sqlite3_bind_int64(st, 9, (sqlite3_int64)io_write_bps);
    sqlite3_bind_int64(st, 10, (sqlite3_int64)now);
    sqlite3_bind_text(st, 11, profile_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);

    if (rc != SQLITE_DONE || changes == 0) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Profile '%s' not found", profile_id);
        return APP_ERR_NOT_FOUND;
    }
    return APP_SUCCESS;
}

AppResult app_manager_delete_profile(const char *profile_id,
                                     char *err_buf, size_t err_sz)
{
    if (!profile_id) return APP_ERR_INVALID_PARAM;
    if (strcmp(profile_id, "default") == 0) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Cannot delete the protected 'default' profile");
        return APP_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    /* Guard: profile must not be in use by any registered application */
    const char *chk_sql = "SELECT COUNT(*) FROM applications WHERE default_profile_id=?;";
    sqlite3_stmt *st_chk = NULL;
    if (sqlite3_prepare_v2(db, chk_sql, -1, &st_chk, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st_chk, 1, profile_id, -1, SQLITE_STATIC);
        if (sqlite3_step(st_chk) == SQLITE_ROW) {
            int in_use = sqlite3_column_int(st_chk, 0);
            if (in_use > 0) {
                sqlite3_finalize(st_chk);
                pthread_mutex_unlock(&s_app_mutex);
                if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Profile '%s' is in use by %d registered applications", profile_id, in_use);
                return APP_ERR_INVALID_PARAM;
            }
        }
        sqlite3_finalize(st_chk);
    }

    const char *sql = "DELETE FROM application_profiles WHERE profile_id=?;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_bind_text(st, 1, profile_id, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);

    if (rc != SQLITE_DONE || changes == 0) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Profile '%s' not found", profile_id);
        return APP_ERR_NOT_FOUND;
    }
    return APP_SUCCESS;
}

AppResult app_manager_get_profile(const char *profile_id,
                                  ApplicationProfile *out_profile,
                                  char *err_buf, size_t err_sz)
{
    if (!profile_id || !out_profile) return APP_ERR_INVALID_PARAM;
    memset(out_profile, 0, sizeof(*out_profile));

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql =
        "SELECT id, profile_id, name, description, cpu_cores, cpu_quota_us, cpu_period_us, "
        "cpu_weight, memory_max_bytes, pids_max, io_read_bps, io_write_bps, created_at, updated_at "
        "FROM application_profiles WHERE profile_id=?;";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_bind_text(st, 1, profile_id, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        out_profile->db_id = sqlite3_column_int64(st, 0);
        snprintf(out_profile->profile_id, sizeof(out_profile->profile_id), "%s", (const char *)sqlite3_column_text(st, 1));
        snprintf(out_profile->name, sizeof(out_profile->name), "%s", (const char *)sqlite3_column_text(st, 2));
        snprintf(out_profile->description, sizeof(out_profile->description), "%s", (const char *)sqlite3_column_text(st, 3));
        out_profile->cpu_cores = sqlite3_column_double(st, 4);
        out_profile->cpu_quota_us = sqlite3_column_int64(st, 5);
        out_profile->cpu_period_us = sqlite3_column_int64(st, 6);
        out_profile->cpu_weight = sqlite3_column_int(st, 7);
        out_profile->memory_max_bytes = (uint64_t)sqlite3_column_int64(st, 8);
        out_profile->pids_max = sqlite3_column_int64(st, 9);
        out_profile->io_read_bps = (uint64_t)sqlite3_column_int64(st, 10);
        out_profile->io_write_bps = (uint64_t)sqlite3_column_int64(st, 11);
        out_profile->created_at = (uint64_t)sqlite3_column_int64(st, 12);
        out_profile->updated_at = (uint64_t)sqlite3_column_int64(st, 13);
        sqlite3_finalize(st);
        pthread_mutex_unlock(&s_app_mutex);
        return APP_SUCCESS;
    }

    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);
    if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Profile '%s' not found", profile_id);
    return APP_ERR_NOT_FOUND;
}

AppResult app_manager_list_profiles(ApplicationProfile *out_profiles,
                                    int max_profiles,
                                    int *out_count)
{
    if (!out_profiles || max_profiles <= 0 || !out_count) return APP_ERR_INVALID_PARAM;
    *out_count = 0;

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql =
        "SELECT id, profile_id, name, description, cpu_cores, cpu_quota_us, cpu_period_us, "
        "cpu_weight, memory_max_bytes, pids_max, io_read_bps, io_write_bps, created_at, updated_at "
        "FROM application_profiles ORDER BY name ASC;";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    int count = 0;
    while (sqlite3_step(st) == SQLITE_ROW && count < max_profiles) {
        ApplicationProfile *p = &out_profiles[count++];
        p->db_id = sqlite3_column_int64(st, 0);
        snprintf(p->profile_id, sizeof(p->profile_id), "%s", (const char *)sqlite3_column_text(st, 1));
        snprintf(p->name, sizeof(p->name), "%s", (const char *)sqlite3_column_text(st, 2));
        snprintf(p->description, sizeof(p->description), "%s", (const char *)sqlite3_column_text(st, 3));
        p->cpu_cores = sqlite3_column_double(st, 4);
        p->cpu_quota_us = sqlite3_column_int64(st, 5);
        p->cpu_period_us = sqlite3_column_int64(st, 6);
        p->cpu_weight = sqlite3_column_int(st, 7);
        p->memory_max_bytes = (uint64_t)sqlite3_column_int64(st, 8);
        p->pids_max = sqlite3_column_int64(st, 9);
        p->io_read_bps = (uint64_t)sqlite3_column_int64(st, 10);
        p->io_write_bps = (uint64_t)sqlite3_column_int64(st, 11);
        p->created_at = (uint64_t)sqlite3_column_int64(st, 12);
        p->updated_at = (uint64_t)sqlite3_column_int64(st, 13);
    }

    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);
    *out_count = count;
    return APP_SUCCESS;
}

AppResult app_manager_assign_profile(const char *vm_id,
                                     const char *app_id,
                                     const char *profile_id,
                                     char *err_buf, size_t err_sz)
{
    if (!vm_id || !app_id || !profile_id) return APP_ERR_INVALID_PARAM;

    /* Verify profile exists */
    ApplicationProfile prof;
    if (app_manager_get_profile(profile_id, &prof, err_buf, err_sz) != APP_SUCCESS) {
        return APP_ERR_NOT_FOUND;
    }

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql = "UPDATE applications SET default_profile_id=? WHERE vm_id=? AND app_id=?;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_bind_text(st, 1, profile_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, vm_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, app_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);

    if (rc != SQLITE_DONE || changes == 0) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Application '%s' not found for VM '%s'", app_id, vm_id);
        return APP_ERR_NOT_FOUND;
    }
    return APP_SUCCESS;
}

/* ------------------------------------------------------------------ Launcher & Lifecycle */

AppResult app_manager_log_event(const char *instance_id,
                                const char *app_id,
                                const char *vm_id,
                                const char *event_type,
                                const char *details)
{
    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql =
        "INSERT INTO application_events (instance_id, app_id, vm_id, event_type, event_details, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    uint64_t now = (uint64_t)time(NULL);
    sqlite3_bind_text(st, 1, instance_id ? instance_id : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, app_id ? app_id : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, vm_id ? vm_id : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, event_type ? event_type : "INFO", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, details ? details : "", -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 6, (sqlite3_int64)now);

    sqlite3_step(st);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);
    return APP_SUCCESS;
}

AppResult app_manager_launch_app(const char *vm_id,
                                 const char *app_id,
                                 const char *args_str,
                                 const char *profile_id_override,
                                 char *out_instance_id, size_t instance_id_sz,
                                 char *err_buf, size_t err_sz)
{
    if (err_buf && err_sz > 0) err_buf[0] = '\0';
    if (!vm_id || !app_id || !out_instance_id || instance_id_sz == 0) return APP_ERR_INVALID_PARAM;
    out_instance_id[0] = '\0';

    /* 1. Retrieve registered application */
    ApplicationEntry app;
    AppResult app_rc = app_manager_get_app(vm_id, app_id, &app, err_buf, err_sz);
    if (app_rc != APP_SUCCESS) {
        return app_rc;
    }
    if (!app.enabled) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Application '%s' is disabled", app_id);
        return APP_ERR_INVALID_PARAM;
    }

    /* 2. Select profile */
    const char *prof_id = (profile_id_override && profile_id_override[0]) ? profile_id_override : app.default_profile_id;
    ApplicationProfile prof;
    if (app_manager_get_profile(prof_id, &prof, err_buf, err_sz) != APP_SUCCESS) {
        if (app_manager_get_profile("default", &prof, err_buf, err_sz) != APP_SUCCESS) {
            return APP_ERR_NOT_FOUND;
        }
    }

    /* 3. Derive safe cgroup name: app_<app_id> */
    char cgroup_name[64];
    snprintf(cgroup_name, sizeof(cgroup_name), "app_%s", app_id);
    if (!app_is_safe_name(cgroup_name)) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Sanitized cgroup name '%s' is unsafe", cgroup_name);
        return APP_ERR_INVALID_PARAM;
    }

    /* 4. Enforce resource limits on cgroup BEFORE launching process */
    char r_err[256] = {0};
    ResourceResult r_res = resource_manager_create_group(vm_id, cgroup_name, app.name, r_err, sizeof(r_err));
    if (r_res != RESOURCE_SUCCESS) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Failed to create resource cgroup: %s", r_err);
        return APP_ERR_CGROUP_FAILED;
    }

    /* Apply CPU limit */
    if (prof.cpu_cores > 0.0) {
        int64_t period = prof.cpu_period_us > 0 ? prof.cpu_period_us : 100000;
        resource_manager_set_cpu_limit(vm_id, cgroup_name, prof.cpu_cores, period, r_err, sizeof(r_err));
    }
    /* Apply CPU weight */
    if (prof.cpu_weight > 0) {
        resource_manager_set_cpu_weight(vm_id, cgroup_name, prof.cpu_weight, r_err, sizeof(r_err));
    }
    /* Apply memory limit */
    if (prof.memory_max_bytes > 0) {
        resource_manager_set_memory_limit(vm_id, cgroup_name, prof.memory_max_bytes, r_err, sizeof(r_err));
    }
    /* Apply pids limit */
    if (prof.pids_max > 0) {
        resource_manager_set_process_limit(vm_id, cgroup_name, prof.pids_max, r_err, sizeof(r_err));
    }
    /* Apply disk I/O limit */
    if (prof.io_read_bps > 0 || prof.io_write_bps > 0) {
        resource_manager_set_io_limit(vm_id, cgroup_name, prof.io_read_bps, prof.io_write_bps, r_err, sizeof(r_err));
    }

    /* 5. Dispatch AGENT_OP_LAUNCH_APP RPC with cgroup launch handshake */
    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "launch_%llu", (unsigned long long)time(NULL));
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_LAUNCH_APP;
    snprintf(req.group_id, sizeof(req.group_id), "%s", cgroup_name);
    snprintf(req.app_id, sizeof(req.app_id), "%s", app_id);
    snprintf(req.app_path, sizeof(req.app_path), "%s", app.executable_path);
    if (args_str) {
        snprintf(req.app_args, sizeof(req.app_args), "%s", args_str);
    }

    AgentResourceResponse resp;
    memset(&resp, 0, sizeof(resp));

    ResourceResult r_rpc = resource_manager_rpc(vm_id, &req, &resp, 6000);
    if (r_rpc != RESOURCE_SUCCESS || resp.status_code != AGENT_STATUS_SUCCESS || resp.app_pid <= 1) {
        if (err_buf && err_sz > 0) {
            snprintf(err_buf, err_sz, "Application launch failed: %s (%s)",
                     resp.error_message[0] ? resp.error_message : resource_result_str(r_rpc),
                     resp.status_msg[0] ? resp.status_msg : "UNKNOWN");
        }
        return APP_ERR_LAUNCH_FAILED;
    }

    /* 6. Success! Generate instance ID and save in database */
    uint64_t now = (uint64_t)time(NULL);
    snprintf(out_instance_id, instance_id_sz, "inst_%s_%llu_%d", app_id, (unsigned long long)now, (int)resp.app_pid);

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (db) {
        const char *ins_sql =
            "INSERT INTO application_instances "
            "(instance_id, app_id, vm_id, guest_pid, process_start_time, cgroup_path, state, profile_id, started_at) "
            "VALUES (?, ?, ?, ?, ?, ?, 'RUNNING', ?, ?);";

        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, ins_sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, out_instance_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, app_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 3, vm_id, -1, SQLITE_STATIC);
            sqlite3_bind_int(st, 4, (int)resp.app_pid);
            sqlite3_bind_int64(st, 5, (sqlite3_int64)resp.app_start_time);
            sqlite3_bind_text(st, 6, cgroup_name, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 7, prof.profile_id, -1, SQLITE_STATIC);
            sqlite3_bind_int64(st, 8, (sqlite3_int64)now);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }

        /* Update application table launch statistics */
        const char *upd_sql =
            "UPDATE applications SET last_launched_at=?, launch_count=launch_count+1 WHERE vm_id=? AND app_id=?;";
        if (sqlite3_prepare_v2(db, upd_sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, (sqlite3_int64)now);
            sqlite3_bind_text(st, 2, vm_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 3, app_id, -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    pthread_mutex_unlock(&s_app_mutex);

    char event_detail[256];
    snprintf(event_detail, sizeof(event_detail), "PID %d launched in cgroup '%s' (verified=1)", (int)resp.app_pid, cgroup_name);
    app_manager_log_event(out_instance_id, app_id, vm_id, "LAUNCH", event_detail);

    return APP_SUCCESS;
}

AppResult app_manager_stop_app(const char *vm_id,
                               const char *instance_id,
                               int timeout_ms,
                               int force,
                               char *err_buf, size_t err_sz)
{
    if (err_buf && err_sz > 0) err_buf[0] = '\0';
    if (!vm_id || !instance_id) return APP_ERR_INVALID_PARAM;

    ApplicationInstance inst;
    if (app_manager_get_instance(instance_id, &inst, err_buf, err_sz) != APP_SUCCESS) {
        return APP_ERR_NOT_FOUND;
    }

    if (inst.state == APP_STATE_STOPPED) {
        return APP_SUCCESS; /* Already stopped */
    }

    /* System init protection */
    if (inst.guest_pid <= 1) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Refusing to terminate PID <= 1");
        return APP_ERR_INVALID_PARAM;
    }

    /* Build STOP RPC request */
    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    snprintf(req.request_id, sizeof(req.request_id), "stop_%llu", (unsigned long long)time(NULL));
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    req.operation = AGENT_OP_STOP_APP;
    snprintf(req.app_id, sizeof(req.app_id), "%s", inst.app_id);
    req.pid = inst.guest_pid;
    req.expected_start_time = inst.process_start_time;
    req.app_timeout_ms = timeout_ms > 0 ? timeout_ms : 3000;
    req.app_signal = force ? 9 : 15;

    AgentResourceResponse resp;
    memset(&resp, 0, sizeof(resp));

    ResourceResult r_rpc = resource_manager_rpc(vm_id, &req, &resp, 5000);
    if (r_rpc != RESOURCE_SUCCESS) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Stop RPC communication failed: %s", resource_result_str(r_rpc));
        return APP_ERR_AGENT_COMM;
    }

    if (resp.status_code == AGENT_STATUS_PROCESS_IDENTITY_CHANGED) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "PID %d was reused; stop aborted for safety", (int)inst.guest_pid);
        return APP_ERR_PID_IDENTITY_CHANGED;
    }

    /* Update instance state in database with exact exit status */
    uint64_t now = (uint64_t)time(NULL);
    char reason[128] = {0};
    int exit_code = resp.app_exit_code;
    if (exit_code >= 128) {
        snprintf(reason, sizeof(reason), "Terminated by signal %d", exit_code - 128);
    } else if (exit_code > 0) {
        snprintf(reason, sizeof(reason), "Exited with code %d", exit_code);
    } else if (exit_code == 0) {
        snprintf(reason, sizeof(reason), "Clean exit (code 0)");
    } else {
        snprintf(reason, sizeof(reason), "Stopped (unknown exit code)");
    }

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (db) {
        const char *upd_sql =
            "UPDATE application_instances SET state='STOPPED', stopped_at=?, exit_code=?, failure_reason=? WHERE instance_id=?;";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, upd_sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, (sqlite3_int64)now);
            sqlite3_bind_int(st, 2, exit_code);
            sqlite3_bind_text(st, 3, reason, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4, instance_id, -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    pthread_mutex_unlock(&s_app_mutex);

    char event_detail[128];
    snprintf(event_detail, sizeof(event_detail), "PID %d stopped (force=%d, exit_code=%d)",
             (int)inst.guest_pid, force, exit_code);
    app_manager_log_event(instance_id, inst.app_id, vm_id, "STOPPED", event_detail);

    return APP_SUCCESS;
}

AppResult app_manager_get_instance(const char *instance_id,
                                   ApplicationInstance *out_instance,
                                   char *err_buf, size_t err_sz)
{
    if (!instance_id || !out_instance) return APP_ERR_INVALID_PARAM;
    memset(out_instance, 0, sizeof(*out_instance));

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql =
        "SELECT id, instance_id, app_id, vm_id, guest_pid, process_start_time, cgroup_path, "
        "state, profile_id, started_at, stopped_at, exit_code, failure_reason "
        "FROM application_instances WHERE instance_id=?;";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_bind_text(st, 1, instance_id, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        out_instance->db_id = sqlite3_column_int64(st, 0);
        snprintf(out_instance->instance_id, sizeof(out_instance->instance_id), "%s", (const char *)sqlite3_column_text(st, 1));
        snprintf(out_instance->app_id, sizeof(out_instance->app_id), "%s", (const char *)sqlite3_column_text(st, 2));
        snprintf(out_instance->vm_id, sizeof(out_instance->vm_id), "%s", (const char *)sqlite3_column_text(st, 3));
        out_instance->guest_pid = (pid_t)sqlite3_column_int(st, 4);
        out_instance->process_start_time = (uint64_t)sqlite3_column_int64(st, 5);
        snprintf(out_instance->cgroup_path, sizeof(out_instance->cgroup_path), "%s", (const char *)sqlite3_column_text(st, 6));
        out_instance->state = app_state_from_name((const char *)sqlite3_column_text(st, 7));
        snprintf(out_instance->profile_id, sizeof(out_instance->profile_id), "%s", (const char *)sqlite3_column_text(st, 8));
        out_instance->started_at = (uint64_t)sqlite3_column_int64(st, 9);
        out_instance->stopped_at = (uint64_t)sqlite3_column_int64(st, 10);
        out_instance->exit_code = sqlite3_column_int(st, 11);
        snprintf(out_instance->failure_reason, sizeof(out_instance->failure_reason), "%s", (const char *)sqlite3_column_text(st, 12));

        sqlite3_finalize(st);
        pthread_mutex_unlock(&s_app_mutex);
        return APP_SUCCESS;
    }

    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);
    if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Instance '%s' not found", instance_id);
    return APP_ERR_NOT_FOUND;
}

AppResult app_manager_list_instances(const char *vm_id,
                                     AppState state_filter,
                                     ApplicationInstance *out_instances,
                                     int max_instances,
                                     int *out_count)
{
    if (!vm_id || !out_instances || max_instances <= 0 || !out_count) return APP_ERR_INVALID_PARAM;
    *out_count = 0;

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    char sql[512];
    if (state_filter != APP_STATE_UNKNOWN) {
        snprintf(sql, sizeof(sql),
                 "SELECT id, instance_id, app_id, vm_id, guest_pid, process_start_time, cgroup_path, "
                 "state, profile_id, started_at, stopped_at, exit_code, failure_reason "
                 "FROM application_instances WHERE vm_id=? AND state=? ORDER BY started_at DESC LIMIT %d;", max_instances);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT id, instance_id, app_id, vm_id, guest_pid, process_start_time, cgroup_path, "
                 "state, profile_id, started_at, stopped_at, exit_code, failure_reason "
                 "FROM application_instances WHERE vm_id=? ORDER BY started_at DESC LIMIT %d;", max_instances);
    }

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
    if (state_filter != APP_STATE_UNKNOWN) {
        sqlite3_bind_text(st, 2, app_state_name(state_filter), -1, SQLITE_STATIC);
    }

    int count = 0;
    while (sqlite3_step(st) == SQLITE_ROW && count < max_instances) {
        ApplicationInstance *inst = &out_instances[count++];
        inst->db_id = sqlite3_column_int64(st, 0);
        snprintf(inst->instance_id, sizeof(inst->instance_id), "%s", (const char *)sqlite3_column_text(st, 1));
        snprintf(inst->app_id, sizeof(inst->app_id), "%s", (const char *)sqlite3_column_text(st, 2));
        snprintf(inst->vm_id, sizeof(inst->vm_id), "%s", (const char *)sqlite3_column_text(st, 3));
        inst->guest_pid = (pid_t)sqlite3_column_int(st, 4);
        inst->process_start_time = (uint64_t)sqlite3_column_int64(st, 5);
        snprintf(inst->cgroup_path, sizeof(inst->cgroup_path), "%s", (const char *)sqlite3_column_text(st, 6));
        inst->state = app_state_from_name((const char *)sqlite3_column_text(st, 7));
        snprintf(inst->profile_id, sizeof(inst->profile_id), "%s", (const char *)sqlite3_column_text(st, 8));
        inst->started_at = (uint64_t)sqlite3_column_int64(st, 9);
        inst->stopped_at = (uint64_t)sqlite3_column_int64(st, 10);
        inst->exit_code = sqlite3_column_int(st, 11);
        snprintf(inst->failure_reason, sizeof(inst->failure_reason), "%s", (const char *)sqlite3_column_text(st, 12));
    }

    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);
    *out_count = count;
    return APP_SUCCESS;
}

AppResult app_manager_reconcile_instances(const char *vm_id, int *out_reconciled_count)
{
    if (!vm_id || !out_reconciled_count) return APP_ERR_INVALID_PARAM;
    *out_reconciled_count = 0;

    ApplicationInstance instances[64];
    int count = 0;
    if (app_manager_list_instances(vm_id, APP_STATE_RUNNING, instances, 64, &count) != APP_SUCCESS) {
        return APP_ERR_DB;
    }

    int reconciled = 0;
    for (int i = 0; i < count; i++) {
        ApplicationInstance *inst = &instances[i];

        /* Probe guest via AGENT_OP_GET_APP_METRICS */
        AgentResourceRequest req;
        memset(&req, 0, sizeof(req));
        req.version = AGENT_PROTOCOL_VERSION;
        req.operation = AGENT_OP_GET_APP_METRICS;
        snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
        snprintf(req.group_id, sizeof(req.group_id), "%s", inst->cgroup_path);
        req.pid = inst->guest_pid;
        req.expected_start_time = inst->process_start_time;

        AgentResourceResponse resp;
        memset(&resp, 0, sizeof(resp));

        ResourceResult r_rpc = resource_manager_rpc(vm_id, &req, &resp, 1500);
        if (r_rpc != RESOURCE_SUCCESS || resp.status_code == AGENT_STATUS_PROCESS_NOT_FOUND ||
            resp.status_code == AGENT_STATUS_PROCESS_IDENTITY_CHANGED) {
            /* Process no longer exists or VM is offline: mark as STOPPED */
            uint64_t now = (uint64_t)time(NULL);
            pthread_mutex_lock(&s_app_mutex);
            sqlite3 *db = get_db();
            if (db) {
                const char *upd =
                    "UPDATE application_instances SET state='STOPPED', stopped_at=?, failure_reason='Reconciled: process exited' "
                    "WHERE instance_id=?;";
                sqlite3_stmt *st = NULL;
                if (sqlite3_prepare_v2(db, upd, -1, &st, NULL) == SQLITE_OK) {
                    sqlite3_bind_int64(st, 1, (sqlite3_int64)now);
                    sqlite3_bind_text(st, 2, inst->instance_id, -1, SQLITE_STATIC);
                    sqlite3_step(st);
                    sqlite3_finalize(st);
                    reconciled++;
                }
            }
            pthread_mutex_unlock(&s_app_mutex);
            app_manager_log_event(inst->instance_id, inst->app_id, vm_id, "RECONCILED", "Process no longer running");
        }
    }

    *out_reconciled_count = reconciled;
    return APP_SUCCESS;
}

/* ------------------------------------------------------------------ Resource Monitoring & Sampling */

AppResult app_manager_collect_metrics(const char *vm_id,
                                      const char *instance_id,
                                      ApplicationResourceSample *out_sample,
                                      char *err_buf, size_t err_sz)
{
    if (err_buf && err_sz > 0) err_buf[0] = '\0';
    if (!vm_id || !instance_id || !out_sample) return APP_ERR_INVALID_PARAM;
    memset(out_sample, 0, sizeof(*out_sample));

    ApplicationInstance inst;
    if (app_manager_get_instance(instance_id, &inst, err_buf, err_sz) != APP_SUCCESS) {
        return APP_ERR_NOT_FOUND;
    }

    AgentResourceRequest req;
    memset(&req, 0, sizeof(req));
    req.version = AGENT_PROTOCOL_VERSION;
    req.operation = AGENT_OP_GET_APP_METRICS;
    snprintf(req.vm_id, sizeof(req.vm_id), "%s", vm_id);
    snprintf(req.group_id, sizeof(req.group_id), "%s", inst.cgroup_path);
    snprintf(req.app_id, sizeof(req.app_id), "%s", inst.app_id);
    req.pid = inst.guest_pid;
    req.expected_start_time = inst.process_start_time;

    AgentResourceResponse resp;
    memset(&resp, 0, sizeof(resp));

    ResourceResult r_rpc = resource_manager_rpc(vm_id, &req, &resp, 3000);
    uint64_t now = (uint64_t)time(NULL);

    snprintf(out_sample->instance_id, sizeof(out_sample->instance_id), "%s", instance_id);
    snprintf(out_sample->app_id, sizeof(out_sample->app_id), "%s", inst.app_id);
    snprintf(out_sample->vm_id, sizeof(out_sample->vm_id), "%s", vm_id);
    out_sample->sample_timestamp = now;

    if (r_rpc != RESOURCE_SUCCESS || resp.status_code != AGENT_STATUS_SUCCESS) {
        snprintf(out_sample->freshness, sizeof(out_sample->freshness), "UNAVAILABLE");
        snprintf(out_sample->verification_state, sizeof(out_sample->verification_state), "UNVERIFIED");
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Metrics RPC failed: %s", resource_result_str(r_rpc));
        return APP_ERR_AGENT_COMM;
    }

    snprintf(out_sample->freshness, sizeof(out_sample->freshness), "FRESH");
    snprintf(out_sample->verification_state, sizeof(out_sample->verification_state), "VERIFIED");

    out_sample->cpu_time_usec = resp.applied.cpu_usage_usec;
    out_sample->throttled_usec = resp.applied.cpu_throttled_usec;
    out_sample->memory_current_bytes = resp.applied.memory_current_bytes;
    out_sample->memory_peak_bytes = resp.applied.memory_peak_bytes;
    out_sample->pids_current = resp.applied.pids_current;
    out_sample->pids_max = resp.applied.pids_max;
    out_sample->io_read_bytes = resp.applied.io_read_bytes;
    out_sample->io_write_bytes = resp.applied.io_write_bytes;

    /* Compute approximate CPU utilization if cpu_time is available */
    if (inst.started_at > 0 && now > inst.started_at) {
        double elapsed_sec = (double)(now - inst.started_at);
        double cpu_sec = (double)out_sample->cpu_time_usec / 1000000.0;
        out_sample->cpu_usage_pct = (cpu_sec / elapsed_sec) * 100.0;
        if (out_sample->cpu_usage_pct > 100.0 * 2.0) out_sample->cpu_usage_pct = 200.0;
    }

    /* Persist sample in database */
    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (db) {
        const char *ins_sql =
            "INSERT INTO application_resource_samples "
            "(instance_id, app_id, vm_id, cpu_usage_pct, cpu_time_usec, throttled_usec, "
            "memory_current_bytes, memory_peak_bytes, pids_current, pids_max, io_read_bytes, io_write_bytes, "
            "sample_timestamp, freshness, verification_state) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, ins_sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, instance_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, inst.app_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 3, vm_id, -1, SQLITE_STATIC);
            sqlite3_bind_double(st, 4, out_sample->cpu_usage_pct);
            sqlite3_bind_int64(st, 5, (sqlite3_int64)out_sample->cpu_time_usec);
            sqlite3_bind_int64(st, 6, (sqlite3_int64)out_sample->throttled_usec);
            sqlite3_bind_int64(st, 7, (sqlite3_int64)out_sample->memory_current_bytes);
            sqlite3_bind_int64(st, 8, (sqlite3_int64)out_sample->memory_peak_bytes);
            sqlite3_bind_int64(st, 9, (sqlite3_int64)out_sample->pids_current);
            sqlite3_bind_int64(st, 10, (sqlite3_int64)out_sample->pids_max);
            sqlite3_bind_int64(st, 11, (sqlite3_int64)out_sample->io_read_bytes);
            sqlite3_bind_int64(st, 12, (sqlite3_int64)out_sample->io_write_bytes);
            sqlite3_bind_int64(st, 13, (sqlite3_int64)now);
            sqlite3_bind_text(st, 14, out_sample->freshness, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 15, out_sample->verification_state, -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    pthread_mutex_unlock(&s_app_mutex);

    return APP_SUCCESS;
}

AppResult app_manager_get_samples(const char *instance_id,
                                  ApplicationResourceSample *out_samples,
                                  int max_samples,
                                  int *out_count)
{
    if (!instance_id || !out_samples || max_samples <= 0 || !out_count) return APP_ERR_INVALID_PARAM;
    *out_count = 0;

    pthread_mutex_lock(&s_app_mutex);
    sqlite3 *db = get_db();
    if (!db) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    const char *sql =
        "SELECT id, instance_id, app_id, vm_id, cpu_usage_pct, cpu_time_usec, throttled_usec, "
        "memory_current_bytes, memory_peak_bytes, pids_current, pids_max, io_read_bytes, io_write_bytes, "
        "sample_timestamp, freshness, verification_state "
        "FROM application_resource_samples WHERE instance_id=? ORDER BY sample_timestamp DESC LIMIT %d;";

    char q[512];
    snprintf(q, sizeof(q), sql, max_samples);

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, q, -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&s_app_mutex);
        return APP_ERR_DB;
    }

    sqlite3_bind_text(st, 1, instance_id, -1, SQLITE_STATIC);

    int count = 0;
    while (sqlite3_step(st) == SQLITE_ROW && count < max_samples) {
        ApplicationResourceSample *s = &out_samples[count++];
        s->db_id = sqlite3_column_int64(st, 0);
        snprintf(s->instance_id, sizeof(s->instance_id), "%s", (const char *)sqlite3_column_text(st, 1));
        snprintf(s->app_id, sizeof(s->app_id), "%s", (const char *)sqlite3_column_text(st, 2));
        snprintf(s->vm_id, sizeof(s->vm_id), "%s", (const char *)sqlite3_column_text(st, 3));
        s->cpu_usage_pct = sqlite3_column_double(st, 4);
        s->cpu_time_usec = (uint64_t)sqlite3_column_int64(st, 5);
        s->throttled_usec = (uint64_t)sqlite3_column_int64(st, 6);
        s->memory_current_bytes = (uint64_t)sqlite3_column_int64(st, 7);
        s->memory_peak_bytes = (uint64_t)sqlite3_column_int64(st, 8);
        s->pids_current = sqlite3_column_int64(st, 9);
        s->pids_max = sqlite3_column_int64(st, 10);
        s->io_read_bytes = (uint64_t)sqlite3_column_int64(st, 11);
        s->io_write_bytes = (uint64_t)sqlite3_column_int64(st, 12);
        s->sample_timestamp = (uint64_t)sqlite3_column_int64(st, 13);
        snprintf(s->freshness, sizeof(s->freshness), "%s", (const char *)sqlite3_column_text(st, 14));
        snprintf(s->verification_state, sizeof(s->verification_state), "%s", (const char *)sqlite3_column_text(st, 15));
    }

    sqlite3_finalize(st);
    pthread_mutex_unlock(&s_app_mutex);
    *out_count = count;
    return APP_SUCCESS;
}
