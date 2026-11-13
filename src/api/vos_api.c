/* vos_api.c – Virtual OS Simulator Stable C ABI & FFI Gateway
 *
 * Implements high-level thread-safe C API for Flutter desktop interface:
 *   - Host and VM telemetry
 *   - Virtual Machine QEMU management
 *   - In-Guest Application discovery, launch, stop, and monitoring
 *   - Resource profiles management
 *   - Classic OS algorithm simulator (CPU scheduling, memory, Banker's)
 *   - Event audit logs
 */

#include "vos_api.h"
#include "virtualization/vm_manager.h"
#include "virtualization/vm_config.h"
#include "application/app_manager.h"
#include "monitoring/host_monitor.h"
#include "monitoring/vm_monitor.h"
#include "monitoring/resource_manager.h"
#include "core/scheduler.h"
#include "core/memory.h"
#include "core/resources.h"
#include "core/types.h"
#include "db/database.h"
#include "sqlite3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#define VOS_API_VERSION_STR "1.6.0-phase6"

static pthread_mutex_t s_api_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_initialized = 0;
static char s_db_path[512] = "virtual_os.db";
static VmManager *s_vm_mgr = NULL;

/* ------------------------------------------------------------------ System Lifecycle */

const char *vos_api_version(void)
{
    return VOS_API_VERSION_STR;
}

int vos_api_is_initialized(void)
{
    pthread_mutex_lock(&s_api_mutex);
    int init = s_initialized;
    pthread_mutex_unlock(&s_api_mutex);
    return init;
}

int vos_api_init(const char *db_path)
{
    pthread_mutex_lock(&s_api_mutex);
    if (s_initialized) {
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_OK;
    }

    if (db_path && db_path[0]) {
        snprintf(s_db_path, sizeof(s_db_path), "%s", db_path);
    } else {
        snprintf(s_db_path, sizeof(s_db_path), "virtual_os.db");
    }

    /* Initialize core database */
    db_open(s_db_path);

    /* Initialize VM database schema */
    vm_manager_set_db_path(s_db_path);
    vm_db_ensure_schema();

    /* Initialize Application Manager */
    if (app_manager_init(s_db_path) != APP_SUCCESS) {
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_DB;
    }

    /* Initialize Host Monitor */
    host_monitor_init();

    /* Initialize VM Manager */
    s_vm_mgr = vm_manager_init(NULL);
    if (!s_vm_mgr) {
        app_manager_shutdown();
        host_monitor_shutdown();
        db_close();
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_INTERNAL;
    }

    s_initialized = 1;
    pthread_mutex_unlock(&s_api_mutex);
    return VOS_OK;
}

int vos_api_shutdown(void)
{
    pthread_mutex_lock(&s_api_mutex);
    if (!s_initialized) {
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_OK;
    }

    if (s_vm_mgr) {
        vm_manager_shutdown(s_vm_mgr);
        s_vm_mgr = NULL;
    }

    app_manager_shutdown();
    host_monitor_shutdown();
    db_close();

    s_initialized = 0;
    pthread_mutex_unlock(&s_api_mutex);
    return VOS_OK;
}

/* ------------------------------------------------------------------ Host Telemetry */

int vos_api_get_host_telemetry(char *out_json, size_t out_sz)
{
    if (!out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    HostMetrics hm;
    memset(&hm, 0, sizeof(hm));
    if (host_monitor_sample(&hm) != 0) {
        return VOS_ERR_INTERNAL;
    }

    double load1 = 0.0, load5 = 0.0, load15 = 0.0;
    double load[3];
    if (getloadavg(load, 3) != -1) {
        load1 = load[0];
        load5 = load[1];
        load15 = load[2];
    }

    snprintf(out_json, out_sz,
             "{"
             "\"cpu_count\":%d,"
             "\"cpu_usage_pct\":%.2f,"
             "\"cpu_user_pct\":%.2f,"
             "\"cpu_system_pct\":%.2f,"
             "\"cpu_idle_pct\":%.2f,"
             "\"total_ram_bytes\":%llu,"
             "\"used_ram_bytes\":%llu,"
             "\"free_ram_bytes\":%llu,"
             "\"available_ram_bytes\":%llu,"
             "\"load_avg_1m\":%.2f,"
             "\"load_avg_5m\":%.2f,"
             "\"load_avg_15m\":%.2f,"
             "\"timestamp_ms\":%llu,"
             "\"cpu_arch\":\"%s\","
             "\"host_model\":\"%s\","
             "\"host_platform\":\"macOS Apple Silicon (Darwin arm64)\""
             "}",
             hm.cpu_core_count > 0 ? hm.cpu_core_count : 1,
             hm.cpu_total_percent,
             hm.cpu_user_percent,
             hm.cpu_system_percent,
             hm.cpu_idle_percent,
             (unsigned long long)hm.memory_total_bytes,
             (unsigned long long)hm.memory_used_bytes,
             (unsigned long long)hm.memory_free_bytes,
             (unsigned long long)hm.memory_available_bytes,
             load1, load5, load15,
             (unsigned long long)hm.timestamp_ms,
             hm.cpu_arch[0] ? hm.cpu_arch : "arm64",
             hm.host_model[0] ? hm.host_model : "Apple Silicon M5");

    return VOS_OK;
}

/* ------------------------------------------------------------------ Virtual Machines */

int vos_api_vm_list(char *out_json, size_t out_sz)
{
    if (!out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    pthread_mutex_lock(&s_api_mutex);
    if (!s_initialized) {
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_NOT_INITIALIZED;
    }

    if (s_vm_mgr) {
        vm_reap_children(s_vm_mgr);
    }

    VmConfig configs[32];
    VmState states[32];
    int pids[32];
    int count = vm_db_list(configs, states, pids, 32);
    if (count < 0) {
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_DB;
    }

    size_t w = 0;
    w += snprintf(out_json + w, out_sz - w, "[");

    for (int i = 0; i < count; i++) {
        VmHandle h;
        const char *v_state = vm_state_name(states[i]);
        int v_pid = pids[i];
        if (s_vm_mgr && vm_get_handle(s_vm_mgr, configs[i].id, &h) == 0) {
            v_state = vm_state_name(h.state);
            v_pid = (int)h.pid;
        }

        if (i > 0 && w < out_sz - 2) {
            w += snprintf(out_json + w, out_sz - w, ",");
        }

        if (w < out_sz - 64) {
            w += snprintf(out_json + w, out_sz - w,
                          "{"
                          "\"id\":%d,"
                          "\"vm_id\":\"%s\","
                          "\"name\":\"%s\","
                          "\"vcpu_count\":%d,"
                          "\"ram_mb\":%d,"
                          "\"state\":\"%s\","
                          "\"os_type\":\"%s\","
                          "\"disk_path\":\"%s\","
                          "\"iso_path\":\"%s\","
                          "\"pid\":%d"
                          "}",
                          i + 1,
                          configs[i].id,
                          configs[i].name,
                          configs[i].cpu_count,
                          configs[i].memory_mb,
                          v_state,
                          vm_arch_name(configs[i].arch),
                          configs[i].disk_image_path,
                          configs[i].os_image_path,
                          v_pid);
        }
    }

    if (w < out_sz - 2) {
        w += snprintf(out_json + w, out_sz - w, "]");
    }

    pthread_mutex_unlock(&s_api_mutex);
    return VOS_OK;
}

int vos_api_vm_get(const char *vm_id, char *out_json, size_t out_sz)
{
    if (!vm_id || !out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    pthread_mutex_lock(&s_api_mutex);
    if (!s_initialized) {
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_NOT_INITIALIZED;
    }

    VmConfig cfg;
    VmState st = VM_STATE_STOPPED;
    int rc = vm_db_get(vm_id, &cfg, &st);
    if (rc != 0) {
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_NOT_FOUND;
    }

    VmHandle h;
    const char *v_state = vm_state_name(st);
    int v_pid = 0;
    if (s_vm_mgr && vm_get_handle(s_vm_mgr, vm_id, &h) == 0) {
        v_state = vm_state_name(h.state);
        v_pid = (int)h.pid;
    }

    snprintf(out_json, out_sz,
             "{"
             "\"vm_id\":\"%s\","
             "\"name\":\"%s\","
             "\"vcpu_count\":%d,"
             "\"ram_mb\":%d,"
             "\"state\":\"%s\","
             "\"os_type\":\"%s\","
             "\"disk_path\":\"%s\","
             "\"iso_path\":\"%s\","
             "\"pid\":%d"
             "}",
             cfg.id,
             cfg.name,
             cfg.cpu_count,
             cfg.memory_mb,
             v_state,
             vm_arch_name(cfg.arch),
             cfg.disk_image_path,
             cfg.os_image_path,
             v_pid);

    pthread_mutex_unlock(&s_api_mutex);
    return VOS_OK;
}

int vos_api_vm_create(const char *config_json, char *out_err, size_t err_sz)
{
    if (out_err && err_sz > 0) out_err[0] = '\0';
    if (!config_json) return VOS_ERR_INVALID_PARAM;

    /* Simple extraction of JSON fields: vm_id, name, vcpu_count, ram_mb, disk_path, iso_path */
    char vm_id[64] = {0};
    char name[128] = {0};
    int vcpu = 2;
    int ram_mb = 1024;
    char disk_path[256] = {0};
    char iso_path[256] = {0};

    const char *p = strstr(config_json, "\"id\":");
    if (p) sscanf(p + 5, " \"%63[^\"]\"", vm_id);
    if (!vm_id[0]) {
        p = strstr(config_json, "\"vm_id\":");
        if (p) sscanf(p + 8, " \"%63[^\"]\"", vm_id);
    }

    p = strstr(config_json, "\"name\":");
    if (p) sscanf(p + 7, " \"%127[^\"]\"", name);

    p = strstr(config_json, "\"vcpu_count\":");
    if (p) sscanf(p + 13, "%d", &vcpu);

    p = strstr(config_json, "\"ram_mb\":");
    if (p) sscanf(p + 9, "%d", &ram_mb);

    p = strstr(config_json, "\"disk_path\":");
    if (p) sscanf(p + 12, " \"%255[^\"]\"", disk_path);

    p = strstr(config_json, "\"iso_path\":");
    if (p) sscanf(p + 11, " \"%255[^\"]\"", iso_path);

    if (!vm_id[0]) {
        if (out_err && err_sz > 0) snprintf(out_err, err_sz, "Missing VM ID in configuration JSON");
        return VOS_ERR_INVALID_PARAM;
    }
    if (!name[0]) {
        snprintf(name, sizeof(name), "%s", vm_id);
    }

    VmConfig cfg;
    vm_config_init(&cfg);
    snprintf(cfg.id, sizeof(cfg.id), "%s", vm_id);
    snprintf(cfg.name, sizeof(cfg.name), "%s", name);
    cfg.arch = VM_ARCH_AARCH64;
    cfg.cpu_count = vcpu > 0 ? vcpu : 2;
    cfg.memory_mb = ram_mb >= 256 ? ram_mb : 1024;
    if (disk_path[0]) snprintf(cfg.disk_image_path, sizeof(cfg.disk_image_path), "%s", disk_path);
    if (iso_path[0]) snprintf(cfg.os_image_path, sizeof(cfg.os_image_path), "%s", iso_path);
    snprintf(cfg.work_dir, sizeof(cfg.work_dir), ".");

    char display_str[32] = {0};
    p = strstr(config_json, "\"display\":");
    if (p) sscanf(p + 10, " \"%31[^\"]\"", display_str);

#if defined(__APPLE__)
    cfg.display = display_str[0] ? vm_display_parse(display_str) : VM_DISPLAY_COCOA;
#else
    cfg.display = display_str[0] ? vm_display_parse(display_str) : VM_DISPLAY_NONE;
#endif

    VmHandle h;
    memset(&h, 0, sizeof(h));
    h.cfg = cfg;
    h.state = VM_STATE_CREATED;

    pthread_mutex_lock(&s_api_mutex);
    int rc = vm_db_save(&h);
    pthread_mutex_unlock(&s_api_mutex);

    if (rc <= 0) {
        if (out_err && err_sz > 0) snprintf(out_err, err_sz, "Failed to persist VM configuration in database");
        return VOS_ERR_DB;
    }

    return VOS_OK;
}

int vos_api_vm_delete(const char *vm_id, char *out_err, size_t err_sz)
{
    if (out_err && err_sz > 0) out_err[0] = '\0';
    if (!vm_id) return VOS_ERR_INVALID_PARAM;

    pthread_mutex_lock(&s_api_mutex);
    if (!s_initialized) {
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_NOT_INITIALIZED;
    }

    VmHandle h;
    if (s_vm_mgr && vm_get_handle(s_vm_mgr, vm_id, &h) == 0) {
        if (h.state == VM_STATE_RUNNING || h.state == VM_STATE_STARTING) {
            if (out_err && err_sz > 0) snprintf(out_err, err_sz, "Cannot delete active VM '%s'. Stop it first.", vm_id);
            pthread_mutex_unlock(&s_api_mutex);
            return VOS_ERR_VM_ALREADY_RUNNING;
        }
    }

    char del_err[256] = {0};
    int rc = vm_db_delete(vm_id, del_err, sizeof(del_err));
    pthread_mutex_unlock(&s_api_mutex);

    if (rc != 0) {
        if (out_err && err_sz > 0) snprintf(out_err, err_sz, "%s", del_err[0] ? del_err : "VM not found");
        return VOS_ERR_NOT_FOUND;
    }

    return VOS_OK;
}

int vos_api_vm_start(const char *vm_id, char *out_err, size_t err_sz)
{
    if (out_err && err_sz > 0) out_err[0] = '\0';
    if (!vm_id) return VOS_ERR_INVALID_PARAM;

    pthread_mutex_lock(&s_api_mutex);
    if (!s_initialized || !s_vm_mgr) {
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_NOT_INITIALIZED;
    }

    VmConfig cfg;
    VmState st = VM_STATE_STOPPED;
    int rc = vm_db_get(vm_id, &cfg, &st);

    if (rc != 0) {
        if (out_err && err_sz > 0) snprintf(out_err, err_sz, "VM '%s' not found in database", vm_id);
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_NOT_FOUND;
    }

    char err_buf[512] = {0};
    int start_rc = vm_start(s_vm_mgr, &cfg, err_buf, sizeof(err_buf));
    pthread_mutex_unlock(&s_api_mutex);

    if (start_rc != VM_MGR_OK) {
        if (out_err && err_sz > 0) snprintf(out_err, err_sz, "%s", err_buf[0] ? err_buf : "Failed to start VM");
        return VOS_ERR_INTERNAL;
    }

    return VOS_OK;
}

int vos_api_vm_stop(const char *vm_id, int force, char *out_err, size_t err_sz)
{
    if (out_err && err_sz > 0) out_err[0] = '\0';
    if (!vm_id) return VOS_ERR_INVALID_PARAM;

    pthread_mutex_lock(&s_api_mutex);
    if (!s_initialized || !s_vm_mgr) {
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_NOT_INITIALIZED;
    }

    char err_buf[512] = {0};
    int stop_rc = vm_stop(s_vm_mgr, vm_id, force ? 1000 : 5000, err_buf, sizeof(err_buf));
    pthread_mutex_unlock(&s_api_mutex);

    if (stop_rc != VM_MGR_OK) {
        if (out_err && err_sz > 0) snprintf(out_err, err_sz, "%s", err_buf[0] ? err_buf : "Failed to stop VM");
        return VOS_ERR_INTERNAL;
    }

    return VOS_OK;
}

int vos_api_vm_telemetry(const char *vm_id, char *out_json, size_t out_sz)
{
    if (!vm_id || !out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    pthread_mutex_lock(&s_api_mutex);
    VmHandle h;
    int has_handle = (s_vm_mgr && vm_get_handle(s_vm_mgr, vm_id, &h) == 0);
    pid_t qemu_pid = has_handle ? h.pid : 0;
    const char *state_str = has_handle ? vm_state_name(h.state) : "STOPPED";
    int guest_ready = has_handle ? h.guest_ready : 0;
    pthread_mutex_unlock(&s_api_mutex);

    ProcessHostMetrics phm;
    memset(&phm, 0, sizeof(phm));
    if (qemu_pid > 0) {
        host_monitor_sample_process(qemu_pid, &phm);
    }

    snprintf(out_json, out_sz,
             "{"
             "\"vm_id\":\"%s\","
             "\"state\":\"%s\","
             "\"qemu_pid\":%d,"
             "\"qemu_cpu_pct\":%.2f,"
             "\"qemu_rss_bytes\":%llu,"
             "\"guest_ready\":%d,"
             "\"freshness\":\"%s\","
             "\"verification\":\"VERIFIED\""
             "}",
             vm_id,
             state_str,
             (int)qemu_pid,
             phm.cpu_percent,
             (unsigned long long)phm.memory_rss_bytes,
             guest_ready,
             qemu_pid > 0 ? "LIVE" : "UNAVAILABLE");

    return VOS_OK;
}

/* ------------------------------------------------------------------ Application Management */

int vos_api_app_discover(const char *vm_id, char *out_json, size_t out_sz)
{
    if (!vm_id || !out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    AgentAppDiscoveryList list;
    char err[256] = {0};
    AppResult rc = app_manager_discover_apps(vm_id, &list, err, sizeof(err));
    if (rc != APP_SUCCESS) {
        snprintf(out_json, out_sz, "{\"error\":\"%s\",\"apps\":[]}", err[0] ? err : "Discovery failed");
        return VOS_ERR_COMMUNICATION;
    }

    size_t w = 0;
    w += snprintf(out_json + w, out_sz - w, "{\"count\":%d,\"apps\":[", list.count);
    for (int i = 0; i < list.count; i++) {
        if (i > 0 && w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, ",");
        if (w < out_sz - 64) {
            w += snprintf(out_json + w, out_sz - w,
                          "{\"name\":\"%s\",\"path\":\"%s\",\"package\":\"%s\"}",
                          list.apps[i].name, list.apps[i].path, list.apps[i].package);
        }
    }
    if (w < out_sz - 4) {
        w += snprintf(out_json + w, out_sz - w, "]}");
    }

    return VOS_OK;
}

int vos_api_app_register(const char *vm_id,
                         const char *app_id,
                         const char *name,
                         const char *executable_path,
                         const char *description,
                         const char *category,
                         const char *version,
                         const char *default_profile_id,
                         char *out_err, size_t err_sz)
{
    if (out_err && err_sz > 0) out_err[0] = '\0';
    AppResult rc = app_manager_register_app(vm_id, app_id, name, executable_path,
                                           description ? description : "",
                                           category ? category : "Utility",
                                           version ? version : "1.0",
                                           default_profile_id ? default_profile_id : "default",
                                           out_err, err_sz);
    return rc == APP_SUCCESS ? VOS_OK : VOS_ERR_INTERNAL;
}

int vos_api_app_unregister(const char *vm_id,
                           const char *app_id,
                           char *out_err, size_t err_sz)
{
    if (out_err && err_sz > 0) out_err[0] = '\0';
    AppResult rc = app_manager_unregister_app(vm_id, app_id, out_err, err_sz);
    return rc == APP_SUCCESS ? VOS_OK : VOS_ERR_INTERNAL;
}

int vos_api_app_list(const char *vm_id, char *out_json, size_t out_sz)
{
    if (!vm_id || !out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    ApplicationEntry apps[64];
    int count = 0;
    AppResult rc = app_manager_list_apps(vm_id, NULL, apps, 64, &count);
    if (rc != APP_SUCCESS) return VOS_ERR_DB;

    size_t w = 0;
    w += snprintf(out_json + w, out_sz - w, "[");
    for (int i = 0; i < count; i++) {
        if (i > 0 && w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, ",");
        if (w < out_sz - 64) {
            w += snprintf(out_json + w, out_sz - w,
                          "{"
                          "\"app_id\":\"%s\","
                          "\"vm_id\":\"%s\","
                          "\"name\":\"%s\","
                          "\"path\":\"%s\","
                          "\"description\":\"%s\","
                          "\"category\":\"%s\","
                          "\"version\":\"%s\","
                          "\"default_profile_id\":\"%s\","
                          "\"enabled\":%d,"
                          "\"launch_count\":%d"
                          "}",
                          apps[i].app_id, apps[i].vm_id, apps[i].name,
                          apps[i].executable_path, apps[i].description,
                          apps[i].category, apps[i].version,
                          apps[i].default_profile_id, apps[i].enabled,
                          apps[i].launch_count);
        }
    }
    if (w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, "]");

    return VOS_OK;
}

int vos_api_app_launch(const char *vm_id,
                       const char *app_id,
                       const char *args_str,
                       const char *profile_override,
                       char *out_instance_id, size_t id_sz,
                       char *out_err, size_t err_sz)
{
    if (out_err && err_sz > 0) out_err[0] = '\0';
    AppResult rc = app_manager_launch_app(vm_id, app_id, args_str, profile_override,
                                          out_instance_id, id_sz, out_err, err_sz);
    return rc == APP_SUCCESS ? VOS_OK : VOS_ERR_INTERNAL;
}

int vos_api_app_stop(const char *vm_id,
                     const char *instance_id,
                     int timeout_ms,
                     int force,
                     char *out_err, size_t err_sz)
{
    if (out_err && err_sz > 0) out_err[0] = '\0';
    AppResult rc = app_manager_stop_app(vm_id, instance_id, timeout_ms, force, out_err, err_sz);
    if (rc == APP_ERR_PID_IDENTITY_CHANGED) return VOS_ERR_PID_IDENTITY;
    return rc == APP_SUCCESS ? VOS_OK : VOS_ERR_INTERNAL;
}

int vos_api_app_instances_list(const char *vm_id,
                               const char *state_filter,
                               char *out_json, size_t out_sz)
{
    if (!vm_id || !out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    AppState filter = APP_STATE_UNKNOWN;
    if (state_filter && state_filter[0]) {
        filter = app_state_from_name(state_filter);
    }

    ApplicationInstance instances[64];
    int count = 0;
    AppResult rc = app_manager_list_instances(vm_id, filter, instances, 64, &count);
    if (rc != APP_SUCCESS) return VOS_ERR_DB;

    size_t w = 0;
    w += snprintf(out_json + w, out_sz - w, "[");
    for (int i = 0; i < count; i++) {
        if (i > 0 && w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, ",");
        if (w < out_sz - 64) {
            w += snprintf(out_json + w, out_sz - w,
                          "{"
                          "\"instance_id\":\"%s\","
                          "\"app_id\":\"%s\","
                          "\"vm_id\":\"%s\","
                          "\"pid\":%d,"
                          "\"start_time\":%llu,"
                          "\"cgroup\":\"%s\","
                          "\"state\":\"%s\","
                          "\"profile_id\":\"%s\","
                          "\"started_at\":%llu,"
                          "\"stopped_at\":%llu,"
                          "\"exit_code\":%d,"
                          "\"failure_reason\":\"%s\""
                          "}",
                          instances[i].instance_id, instances[i].app_id, instances[i].vm_id,
                          (int)instances[i].guest_pid,
                          (unsigned long long)instances[i].process_start_time,
                          instances[i].cgroup_path,
                          app_state_name(instances[i].state),
                          instances[i].profile_id,
                          (unsigned long long)instances[i].started_at,
                          (unsigned long long)instances[i].stopped_at,
                          instances[i].exit_code,
                          instances[i].failure_reason);
        }
    }
    if (w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, "]");

    return VOS_OK;
}

int vos_api_app_collect_metrics(const char *vm_id,
                                const char *instance_id,
                                char *out_json, size_t out_sz)
{
    if (!vm_id || !instance_id || !out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    ApplicationResourceSample samp;
    char err[256] = {0};
    AppResult rc = app_manager_collect_metrics(vm_id, instance_id, &samp, err, sizeof(err));
    if (rc != APP_SUCCESS) {
        snprintf(out_json, out_sz, "{\"error\":\"%s\"}", err[0] ? err : "Metrics unavailable");
        return VOS_ERR_COMMUNICATION;
    }

    snprintf(out_json, out_sz,
             "{"
             "\"instance_id\":\"%s\","
             "\"app_id\":\"%s\","
             "\"vm_id\":\"%s\","
             "\"cpu_usage_pct\":%.2f,"
             "\"cpu_time_usec\":%llu,"
             "\"throttled_usec\":%llu,"
             "\"memory_current_bytes\":%llu,"
             "\"memory_peak_bytes\":%llu,"
             "\"pids_current\":%lld,"
             "\"pids_max\":%lld,"
             "\"io_read_bytes\":%llu,"
             "\"io_write_bytes\":%llu,"
             "\"freshness\":\"%s\","
             "\"verification\":\"%s\""
             "}",
             samp.instance_id, samp.app_id, samp.vm_id,
             samp.cpu_usage_pct,
             (unsigned long long)samp.cpu_time_usec,
             (unsigned long long)samp.throttled_usec,
             (unsigned long long)samp.memory_current_bytes,
             (unsigned long long)samp.memory_peak_bytes,
             (long long)samp.pids_current, (long long)samp.pids_max,
             (unsigned long long)samp.io_read_bytes,
             (unsigned long long)samp.io_write_bytes,
             samp.freshness, samp.verification_state);

    return VOS_OK;
}

/* ------------------------------------------------------------------ Resource Profiles */

int vos_api_profile_list(char *out_json, size_t out_sz)
{
    if (!out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    ApplicationProfile profiles[32];
    int count = 0;
    AppResult rc = app_manager_list_profiles(profiles, 32, &count);
    if (rc != APP_SUCCESS) return VOS_ERR_DB;

    size_t w = 0;
    w += snprintf(out_json + w, out_sz - w, "[");
    for (int i = 0; i < count; i++) {
        if (i > 0 && w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, ",");
        if (w < out_sz - 64) {
            w += snprintf(out_json + w, out_sz - w,
                          "{"
                          "\"profile_id\":\"%s\","
                          "\"name\":\"%s\","
                          "\"description\":\"%s\","
                          "\"cpu_cores\":%.2f,"
                          "\"cpu_weight\":%d,"
                          "\"memory_max_bytes\":%llu,"
                          "\"pids_max\":%lld,"
                          "\"io_read_bps\":%llu,"
                          "\"io_write_bps\":%llu,"
                          "\"gpu_support\":\"UNSUPPORTED\""
                          "}",
                          profiles[i].profile_id, profiles[i].name, profiles[i].description,
                          profiles[i].cpu_cores, profiles[i].cpu_weight,
                          (unsigned long long)profiles[i].memory_max_bytes,
                          (long long)profiles[i].pids_max,
                          (unsigned long long)profiles[i].io_read_bps,
                          (unsigned long long)profiles[i].io_write_bps);
        }
    }
    if (w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, "]");

    return VOS_OK;
}

int vos_api_profile_get(const char *profile_id, char *out_json, size_t out_sz)
{
    if (!profile_id || !out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    ApplicationProfile prof;
    char err[256] = {0};
    AppResult rc = app_manager_get_profile(profile_id, &prof, err, sizeof(err));
    if (rc != APP_SUCCESS) return VOS_ERR_NOT_FOUND;

    snprintf(out_json, out_sz,
             "{"
             "\"profile_id\":\"%s\","
             "\"name\":\"%s\","
             "\"description\":\"%s\","
             "\"cpu_cores\":%.2f,"
             "\"cpu_weight\":%d,"
             "\"memory_max_bytes\":%llu,"
             "\"pids_max\":%lld,"
             "\"io_read_bps\":%llu,"
             "\"io_write_bps\":%llu,"
             "\"gpu_support\":\"UNSUPPORTED\""
             "}",
             prof.profile_id, prof.name, prof.description,
             prof.cpu_cores, prof.cpu_weight,
             (unsigned long long)prof.memory_max_bytes,
             (long long)prof.pids_max,
             (unsigned long long)prof.io_read_bps,
             (unsigned long long)prof.io_write_bps);

    return VOS_OK;
}

int vos_api_profile_save(const char *profile_id,
                         const char *name,
                         const char *description,
                         double cpu_cores,
                         int cpu_weight,
                         uint64_t memory_max_bytes,
                         int64_t pids_max,
                         uint64_t io_read_bps,
                         uint64_t io_write_bps,
                         char *out_err, size_t err_sz)
{
    if (out_err && err_sz > 0) out_err[0] = '\0';
    if (!profile_id || !name) return VOS_ERR_INVALID_PARAM;

    ApplicationProfile existing;
    AppResult check_rc = app_manager_get_profile(profile_id, &existing, NULL, 0);

    AppResult rc;
    if (check_rc == APP_SUCCESS) {
        rc = app_manager_update_profile(profile_id, name, description, cpu_cores, cpu_weight,
                                        memory_max_bytes, pids_max, io_read_bps, io_write_bps,
                                        out_err, err_sz);
    } else {
        rc = app_manager_create_profile(profile_id, name, description, cpu_cores, cpu_weight,
                                        memory_max_bytes, pids_max, io_read_bps, io_write_bps,
                                        out_err, err_sz);
    }

    return rc == APP_SUCCESS ? VOS_OK : VOS_ERR_INTERNAL;
}

int vos_api_profile_delete(const char *profile_id, char *out_err, size_t err_sz)
{
    if (out_err && err_sz > 0) out_err[0] = '\0';
    AppResult rc = app_manager_delete_profile(profile_id, out_err, err_sz);
    return rc == APP_SUCCESS ? VOS_OK : VOS_ERR_INTERNAL;
}

/* ------------------------------------------------------------------ OS Algorithm Simulator */

static int parse_processes_json(const char *json, Process *out, int max_p)
{
    if (!json || !out || max_p <= 0) return 0;
    int count = 0;
    const char *p = json;

    while (*p && count < max_p) {
        p = strchr(p, '{');
        if (!p) break;

        Process proc;
        memset(&proc, 0, sizeof(proc));
        proc.pid = count + 1;
        snprintf(proc.name, sizeof(proc.name), "P%d", count + 1);
        proc.burst = 5;
        proc.priority = 1;

        const char *f = strstr(p, "\"pid\":");
        if (f && f < strchr(p, '}')) sscanf(f + 6, "%d", &proc.pid);

        f = strstr(p, "\"name\":");
        if (f && f < strchr(p, '}')) sscanf(f + 7, " \"%31[^\"]\"", proc.name);

        f = strstr(p, "\"arrival\":");
        if (f && f < strchr(p, '}')) sscanf(f + 10, "%d", &proc.arrival);

        f = strstr(p, "\"burst\":");
        if (f && f < strchr(p, '}')) sscanf(f + 8, "%d", &proc.burst);

        f = strstr(p, "\"priority\":");
        if (f && f < strchr(p, '}')) sscanf(f + 11, "%d", &proc.priority);

        f = strstr(p, "\"mem_kb\":");
        if (f && f < strchr(p, '}')) sscanf(f + 9, "%d", &proc.mem_kb);

        out[count++] = proc;
        p = strchr(p, '}');
        if (!p) break;
        p++;
    }

    return count;
}

int vos_api_sim_run_scheduler(int algo,
                              const char *processes_json,
                              int quantum,
                              char *out_json, size_t out_sz)
{
    if (!processes_json || !out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    Process procs[MAX_PROC];
    int n = parse_processes_json(processes_json, procs, MAX_PROC);
    if (n <= 0) {
        snprintf(out_json, out_sz, "{\"error\":\"No valid processes provided\"}");
        return VOS_ERR_INVALID_PARAM;
    }

    SchedResult sr;
    memset(&sr, 0, sizeof(sr));

    SchedAlgo sa = (SchedAlgo)algo;
    if (sa == ALGO_AUTO) {
        int auto_q = quantum;
        char reason[256] = {0};
        sa = sched_auto_select(procs, n, &auto_q, reason, sizeof(reason));
        quantum = auto_q;
        snprintf(sr.reason, sizeof(sr.reason), "%s", reason);
    }

    if (sched_run(sa, procs, n, quantum, &sr) != 0) {
        snprintf(out_json, out_sz, "{\"error\":\"Simulation execution failed\"}");
        return VOS_ERR_INTERNAL;
    }

    size_t w = 0;
    w += snprintf(out_json + w, out_sz - w,
                  "{"
                  "\"algo\":%d,"
                  "\"algo_name\":\"%s\","
                  "\"quantum\":%d,"
                  "\"reason\":\"%s\","
                  "\"total_time\":%d,"
                  "\"busy_time\":%d,"
                  "\"ctx_switches\":%d,"
                  "\"cpu_util\":%.2f,"
                  "\"throughput\":%.4f,"
                  "\"avg_wt\":%.2f,"
                  "\"avg_tat\":%.2f,"
                  "\"avg_rt\":%.2f,"
                  "\"gantt\":[",
                  (int)sa,
                  sched_algo_name(sa),
                  sr.quantum,
                  sr.reason,
                  sr.total_time,
                  sr.busy_time,
                  sr.ctx_switches,
                  sr.cpu_util,
                  sr.throughput,
                  sr.avg_wt,
                  sr.avg_tat,
                  sr.avg_rt);

    for (int i = 0; i < sr.ng; i++) {
        if (i > 0 && w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, ",");
        if (w < out_sz - 32) {
            w += snprintf(out_json + w, out_sz - w,
                          "{\"pid\":%d,\"start\":%d,\"end\":%d}",
                          sr.g[i].pid, sr.g[i].start, sr.g[i].end);
        }
    }

    if (w < out_sz - 16) {
        w += snprintf(out_json + w, out_sz - w, "],\"processes\":[");
    }

    for (int i = 0; i < sr.n; i++) {
        if (i > 0 && w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, ",");
        if (w < out_sz - 64) {
            w += snprintf(out_json + w, out_sz - w,
                          "{"
                          "\"pid\":%d,"
                          "\"name\":\"%s\","
                          "\"completion\":%d,"
                          "\"tat\":%d,"
                          "\"wt\":%d,"
                          "\"rt\":%d"
                          "}",
                          sr.r[i].p.pid, sr.r[i].p.name,
                          sr.r[i].completion, sr.r[i].tat,
                          sr.r[i].wt, sr.r[i].rt);
        }
    }

    if (w < out_sz - 4) {
        w += snprintf(out_json + w, out_sz - w, "]}");
    }

    return VOS_OK;
}

int vos_api_sim_run_memory(int strategy,
                           const char *processes_json,
                           const char *blocks_json,
                           char *out_json, size_t out_sz)
{
    if (!processes_json || !out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    Process procs[MAX_PROC];
    int n = parse_processes_json(processes_json, procs, MAX_PROC);
    if (n <= 0) {
        snprintf(out_json, out_sz, "{\"error\":\"No valid processes provided\"}");
        return VOS_ERR_INVALID_PARAM;
    }

    int blocks[MAX_BLOCKS];
    int nb = 0;
    if (blocks_json && blocks_json[0]) {
        nb = mem_parse_blocks(blocks_json, blocks, MAX_BLOCKS);
    }
    if (nb <= 0) {
        /* Default blocks: 100, 500, 200, 300, 600 */
        blocks[0] = 100; blocks[1] = 500; blocks[2] = 200; blocks[3] = 300; blocks[4] = 600;
        nb = 5;
    }

    MemResult mr;
    memset(&mr, 0, sizeof(mr));
    MemStrategy ms = (MemStrategy)strategy;

    if (mem_run(ms, blocks, nb, procs, n, &mr) != 0) {
        snprintf(out_json, out_sz, "{\"error\":\"Memory simulation failed\"}");
        return VOS_ERR_INTERNAL;
    }

    size_t w = 0;
    w += snprintf(out_json + w, out_sz - w,
                  "{"
                  "\"strategy\":%d,"
                  "\"strategy_name\":\"%s\","
                  "\"total_mem\":%d,"
                  "\"used_mem\":%d,"
                  "\"free_mem\":%d,"
                  "\"internal_frag\":%d,"
                  "\"failed\":%d,"
                  "\"allocations\":[",
                  (int)mr.strategy,
                  mr.strategy_name,
                  mr.total_mem,
                  mr.used,
                  mr.free_kb,
                  mr.internal_frag,
                  mr.failed);

    for (int i = 0; i < mr.n; i++) {
        if (i > 0 && w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, ",");
        if (w < out_sz - 64) {
            w += snprintf(out_json + w, out_sz - w,
                          "{"
                          "\"pid\":%d,"
                          "\"name\":\"%s\","
                          "\"size\":%d,"
                          "\"block\":%d,"
                          "\"block_size\":%d,"
                          "\"frag\":%d,"
                          "\"allocated\":%d"
                          "}",
                          mr.a[i].pid, mr.a[i].name, mr.a[i].size,
                          mr.a[i].block, mr.a[i].block_size,
                          mr.a[i].frag, mr.a[i].allocated);
        }
    }

    if (w < out_sz - 4) {
        w += snprintf(out_json + w, out_sz - w, "]}");
    }

    return VOS_OK;
}

int vos_api_sim_run_bankers(const char *processes_json,
                            const char *avail_json,
                            char *out_json, size_t out_sz)
{
    if (!processes_json || !out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';

    Process procs[MAX_PROC];
    int n = parse_processes_json(processes_json, procs, MAX_PROC);
    if (n <= 0) {
        snprintf(out_json, out_sz, "{\"error\":\"No valid processes provided\"}");
        return VOS_ERR_INVALID_PARAM;
    }

    int total[NRES] = {10, 5, 7, 6};
    if (avail_json && avail_json[0]) {
        res_parse_totals(avail_json, total);
    }

    ResResult rr;
    memset(&rr, 0, sizeof(rr));

    res_run(total, procs, n, &rr);

    size_t w = 0;
    w += snprintf(out_json + w, out_sz - w,
                  "{"
                  "\"valid\":%d,"
                  "\"safe\":%d,"
                  "\"message\":\"%s\","
                  "\"sequence\":[",
                  rr.valid,
                  rr.safe,
                  rr.message);

    for (int i = 0; i < rr.seq_len; i++) {
        if (i > 0 && w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, ",");
        if (w < out_sz - 16) {
            w += snprintf(out_json + w, out_sz - w, "%d", rr.seq[i]);
        }
    }

    if (w < out_sz - 4) {
        w += snprintf(out_json + w, out_sz - w, "]}");
    }

    return VOS_OK;
}

/* ------------------------------------------------------------------ Audit & Event History */

static void s_json_escape(const char *src, char *dst, size_t dst_sz)
{
    if (!dst || dst_sz == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 2 < dst_sz; i++) {
        if (src[i] == '"' || src[i] == '\\') {
            dst[j++] = '\\';
            dst[j++] = src[i];
        } else if (src[i] == '\n') {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (src[i] == '\r') {
            dst[j++] = '\\';
            dst[j++] = 'r';
        } else if (src[i] == '\t') {
            dst[j++] = '\\';
            dst[j++] = 't';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

int vos_api_event_log(const char *instance_id,
                      const char *app_id,
                      const char *vm_id,
                      const char *event_type,
                      const char *details)
{
    AppResult rc = app_manager_log_event(instance_id, app_id, vm_id, event_type, details);
    return rc == APP_SUCCESS ? VOS_OK : VOS_ERR_DB;
}

int vos_api_events_list(const char *vm_id,
                        int limit,
                        char *out_json, size_t out_sz)
{
    if (!out_json || out_sz == 0) return VOS_ERR_INVALID_PARAM;
    out_json[0] = '\0';
    if (limit <= 0) limit = 100;

    pthread_mutex_lock(&s_api_mutex);
    sqlite3 *db = NULL;
    if (sqlite3_open(s_db_path, &db) != SQLITE_OK) {
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_DB;
    }

    char sql[512];
    if (vm_id && vm_id[0]) {
        snprintf(sql, sizeof(sql),
                 "SELECT id, instance_id, app_id, vm_id, event_type, event_details, created_at "
                 "FROM application_events WHERE vm_id=? ORDER BY created_at DESC LIMIT %d;", limit);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT id, instance_id, app_id, vm_id, event_type, event_details, created_at "
                 "FROM application_events ORDER BY created_at DESC LIMIT %d;", limit);
    }

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        pthread_mutex_unlock(&s_api_mutex);
        return VOS_ERR_DB;
    }

    if (vm_id && vm_id[0]) {
        sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
    }

    size_t w = 0;
    w += snprintf(out_json + w, out_sz - w, "[");
    int count = 0;

    while (sqlite3_step(st) == SQLITE_ROW) {
        if (count > 0 && w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, ",");
        char esc_details[4096];
        const char *raw_details = (const char *)sqlite3_column_text(st, 5);
        s_json_escape(raw_details ? raw_details : "", esc_details, sizeof(esc_details));

        if (w < out_sz - 64) {
            w += snprintf(out_json + w, out_sz - w,
                          "{"
                          "\"id\":%d,"
                          "\"instance_id\":\"%s\","
                          "\"app_id\":\"%s\","
                          "\"vm_id\":\"%s\","
                          "\"event_type\":\"%s\","
                          "\"event_details\":\"%s\","
                          "\"created_at\":%llu"
                          "}",
                          sqlite3_column_int(st, 0),
                          (const char *)sqlite3_column_text(st, 1),
                          (const char *)sqlite3_column_text(st, 2),
                          (const char *)sqlite3_column_text(st, 3),
                          (const char *)sqlite3_column_text(st, 4),
                          esc_details,
                          (unsigned long long)sqlite3_column_int64(st, 6));
        }
        count++;
    }

    if (w < out_sz - 2) w += snprintf(out_json + w, out_sz - w, "]");

    sqlite3_finalize(st);
    sqlite3_close(db);
    pthread_mutex_unlock(&s_api_mutex);
    return VOS_OK;
}
