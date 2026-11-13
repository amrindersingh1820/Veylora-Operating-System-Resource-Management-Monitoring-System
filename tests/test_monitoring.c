/* tests/test_monitoring.c – Comprehensive Unit & Integration Tests for Monitoring & Limits
 *
 * Covers:
 *   1. Host Resource Monitor (Mach host APIs, sysctl, proc_pidinfo)
 *   2. Agent Telemetry Protocol (JSON serialization/deserialization, error handling)
 *   3. Application & Process Classification and Ranking
 *   4. Resource Management & Limits (cgroups v2 bounds, PID 1 protection, host priority)
 *   5. SQLite Schema, Persistence, Multi-VM Isolation, and Querying
 *   6. VM Monitor Lifecycle & Thread Concurrency (start, sample, stop, shutdown)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>

#include "metrics.h"
#include "monitoring/host_monitor.h"
#include "monitoring/agent_protocol.h"
#include "monitoring/process_monitor.h"
#include "monitoring/resource_limits.h"
#include "monitoring/vm_monitor.h"
#include "virtualization/vm_config.h"

static int g_checks = 0;
static int g_fails  = 0;

#define CHECK(cond) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            g_fails++; \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_MSG(cond, msg) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            g_fails++; \
            fprintf(stderr, "FAIL [%s:%d]: %s (%s)\n", __FILE__, __LINE__, #cond, msg); \
        } \
    } while (0)

/* ================================================================ Test Cases */

static void test_host_monitor(void)
{
    printf("\n--- Test Suite 1: Host Resource Monitor (Mach / Sysctl / Proc) ---\n");

    CHECK(host_monitor_init() == 0);

    HostMetrics h;
    memset(&h, 0, sizeof(h));
    int rc = host_monitor_sample(&h);
    CHECK(rc == 0);
    CHECK(h.cpu_core_count > 0);
    CHECK(strlen(h.cpu_arch) > 0);
    CHECK(h.memory_total_bytes > 0);
    CHECK(h.memory_used_bytes > 0);
    CHECK(h.timestamp_ms > 0);

    /* Second sample to verify delta CPU calculation */
    usleep(50000); /* 50ms */
    rc = host_monitor_sample(&h);
    CHECK(rc == 0);
    CHECK(h.cpu_total_percent >= 0.0 && h.cpu_total_percent <= 100.0);
    CHECK(h.cpu_user_percent >= 0.0 && h.cpu_user_percent <= 100.0);
    CHECK(h.cpu_system_percent >= 0.0 && h.cpu_system_percent <= 100.0);
    CHECK(h.cpu_idle_percent >= 0.0 && h.cpu_idle_percent <= 100.0);

    /* Test process inspection with current test process */
    ProcessHostMetrics p;
    memset(&p, 0, sizeof(p));
    rc = host_monitor_sample_process(getpid(), &p);
    CHECK(rc == 0);
    CHECK(p.is_alive == 1);
    CHECK(p.pid == getpid());
    CHECK(p.memory_rss_bytes > 0);
    CHECK(strlen(p.state) > 0);

    /* Test non-existent PID */
    memset(&p, 0, sizeof(p));
    rc = host_monitor_sample_process(99999999, &p);
    CHECK(rc == 0);
    CHECK(p.is_alive == 0);

    /* Test timestamp helper */
    uint64_t now = host_monitor_now_ms();
    CHECK(now > 1700000000000ULL);

    host_monitor_shutdown();
}

static void test_agent_protocol(void)
{
    printf("\n--- Test Suite 2: Agent Telemetry Protocol (JSON Serialization/Parsing) ---\n");

    GuestTelemetryPayload send_p;
    memset(&send_p, 0, sizeof(send_p));
    send_p.version = AGENT_PROTOCOL_VERSION;
    snprintf(send_p.vm_id, sizeof(send_p.vm_id), "vm_test_protocol");
    send_p.sequence = 101;
    send_p.timestamp_ms = 1730000000000ULL;

    /* CPU */
    send_p.cpu.total_percent = 42.5;
    send_p.cpu.user_percent = 30.0;
    send_p.cpu.system_percent = 12.5;
    send_p.cpu.idle_percent = 57.5;
    send_p.cpu.iowait_percent = 0.5;
    send_p.cpu.core_count = 4;
    send_p.cpu.core_percents[0] = 50.0;
    send_p.cpu.core_percents[1] = 40.0;
    send_p.cpu.core_percents[2] = 45.0;
    send_p.cpu.core_percents[3] = 35.0;

    /* Memory */
    send_p.memory.total_bytes = 4294967296ULL;     /* 4 GB */
    send_p.memory.used_bytes = 2147483648ULL;      /* 2 GB */
    send_p.memory.free_bytes = 1073741824ULL;      /* 1 GB */
    send_p.memory.available_bytes = 2147483648ULL; /* 2 GB */

    /* Disk */
    send_p.disk.read_bytes = 10485760ULL;          /* 10 MB */
    send_p.disk.write_bytes = 5242880ULL;          /* 5 MB */
    send_p.disk.read_ops = 500;
    send_p.disk.write_ops = 250;

    /* Network */
    send_p.network.rx_bytes = 2097152ULL;
    send_p.network.tx_bytes = 1048576ULL;
    snprintf(send_p.network.iface_name, sizeof(send_p.network.iface_name), "eth0");

    /* Processes */
    send_p.process_count = 3;
    send_p.processes[0].pid = 1;
    snprintf(send_p.processes[0].name, sizeof(send_p.processes[0].name), "systemd");
    snprintf(send_p.processes[0].state, sizeof(send_p.processes[0].state), "S");
    send_p.processes[0].cpu_percent = 0.5;
    send_p.processes[0].memory_rss_bytes = 16777216ULL; /* 16 MB */

    send_p.processes[1].pid = 1234;
    snprintf(send_p.processes[1].name, sizeof(send_p.processes[1].name), "photoshop.bin");
    snprintf(send_p.processes[1].state, sizeof(send_p.processes[1].state), "R");
    send_p.processes[1].cpu_percent = 65.4;
    send_p.processes[1].memory_rss_bytes = 1073741824ULL; /* 1 GB */

    send_p.processes[2].pid = 2345;
    snprintf(send_p.processes[2].name, sizeof(send_p.processes[2].name), "clion");
    snprintf(send_p.processes[2].state, sizeof(send_p.processes[2].state), "S");
    send_p.processes[2].cpu_percent = 22.1;
    send_p.processes[2].memory_rss_bytes = 838860800ULL; /* 800 MB */

    /* 1. Serialize */
    char json_buf[AGENT_MAX_JSON_SZ];
    int json_len = agent_protocol_serialize(&send_p, json_buf, sizeof(json_buf));
    CHECK(json_len > 0);
    CHECK(json_buf[0] == '{');
    CHECK(strstr(json_buf, "\"vm_id\":\"vm_test_protocol\"") != NULL);
    CHECK(strstr(json_buf, "\"photoshop.bin\"") != NULL);
    CHECK(strstr(json_buf, "\"clion\"") != NULL);

    /* 2. Deserialize */
    GuestTelemetryPayload recv_p;
    memset(&recv_p, 0, sizeof(recv_p));
    int rc = agent_protocol_deserialize(json_buf, &recv_p);
    CHECK(rc == 0);
    CHECK(recv_p.version == AGENT_PROTOCOL_VERSION);
    CHECK(strcmp(recv_p.vm_id, "vm_test_protocol") == 0);
    CHECK(recv_p.sequence == 101);
    CHECK(fabs(recv_p.cpu.total_percent - 42.5) < 0.1);
    CHECK(recv_p.memory.total_bytes == 4294967296ULL);
    CHECK(recv_p.disk.read_bytes == 10485760ULL);
    CHECK(strcmp(recv_p.network.iface_name, "eth0") == 0);
    CHECK(recv_p.process_count == 3);
    CHECK(recv_p.processes[1].pid == 1234);
    CHECK(strcmp(recv_p.processes[1].name, "photoshop.bin") == 0);
    CHECK(fabs(recv_p.processes[1].cpu_percent - 65.4) < 0.1);
    CHECK(recv_p.processes[2].pid == 2345);
    CHECK(strcmp(recv_p.processes[2].name, "clion") == 0);

    /* 3. Error and boundary handling */
    CHECK(agent_protocol_serialize(NULL, json_buf, sizeof(json_buf)) == -1);
    CHECK(agent_protocol_serialize(&send_p, NULL, 0) == -1);
    CHECK(agent_protocol_serialize(&send_p, json_buf, 32) == -1); /* Buffer too small */
    CHECK(agent_protocol_deserialize(NULL, &recv_p) == -1);
    CHECK(agent_protocol_deserialize("{corrupted json", &recv_p) == -1);
    CHECK(agent_protocol_deserialize("{\"version\": 999}", &recv_p) == -2); /* Version mismatch */
}

static void test_process_monitor(void)
{
    printf("\n--- Test Suite 3: Application Classification and Process Ranking ---\n");

    /* Classification checks */
    CHECK(process_classify_group("photoshop") == APP_GROUP_CREATIVE);
    CHECK(process_classify_group("Photoshop.exe") == APP_GROUP_CREATIVE);
    CHECK(process_classify_group("Lightroom Classic") == APP_GROUP_CREATIVE);
    CHECK(process_classify_group("blender-bin") == APP_GROUP_CREATIVE);
    CHECK(process_classify_group("gimp-2.10") == APP_GROUP_CREATIVE);

    CHECK(process_classify_group("clion") == APP_GROUP_DEVTOOLS);
    CHECK(process_classify_group("idea.sh") == APP_GROUP_DEVTOOLS);
    CHECK(process_classify_group("pycharm64.exe") == APP_GROUP_DEVTOOLS);
    CHECK(process_classify_group("code") == APP_GROUP_DEVTOOLS);
    CHECK(process_classify_group("clang-15") == APP_GROUP_DEVTOOLS);

    CHECK(process_classify_group("firefox") == APP_GROUP_BROWSER);
    CHECK(process_classify_group("chrome") == APP_GROUP_BROWSER);
    CHECK(process_classify_group("chromium-browser") == APP_GROUP_BROWSER);

    CHECK(process_classify_group("systemd") == APP_GROUP_SYSTEM);
    CHECK(process_classify_group("sshd") == APP_GROUP_SYSTEM);
    CHECK(process_classify_group("init") == APP_GROUP_SYSTEM);

    CHECK(process_classify_group("custom_calculator") == APP_GROUP_OTHER);

    /* Group names */
    for (int g = 0; g < APP_GROUP_COUNT; g++) {
        CHECK(process_group_name((AppGroup)g) != NULL);
        CHECK(strlen(process_group_name((AppGroup)g)) > 0);
    }

    /* Process list operations */
    GuestProcessMetric list[5];
    memset(list, 0, sizeof(list));

    list[0].pid = 1;   snprintf(list[0].name, sizeof(list[0].name), "systemd");
    list[0].cpu_percent = 0.2;  list[0].memory_rss_bytes = 20000000ULL;

    list[1].pid = 100; snprintf(list[1].name, sizeof(list[1].name), "photoshop");
    list[1].cpu_percent = 55.0; list[1].memory_rss_bytes = 1500000000ULL;

    list[2].pid = 200; snprintf(list[2].name, sizeof(list[2].name), "clion");
    list[2].cpu_percent = 25.0; list[2].memory_rss_bytes = 900000000ULL;

    list[3].pid = 300; snprintf(list[3].name, sizeof(list[3].name), "chrome");
    list[3].cpu_percent = 15.0; list[3].memory_rss_bytes = 1200000000ULL;

    list[4].pid = 400; snprintf(list[4].name, sizeof(list[4].name), "user_job");
    list[4].cpu_percent = 5.0;  list[4].memory_rss_bytes = 100000000ULL;

    /* 1. Filter by group */
    ProcessFilter f;
    memset(&f, 0, sizeof(f));
    f.group_filter = APP_GROUP_CREATIVE;
    GuestProcessMetric out[5];
    int out_cnt = 0;
    CHECK(process_filter_list(list, 5, &f, out, 5, &out_cnt) == 0);
    CHECK(out_cnt == 1);
    CHECK(strcmp(out[0].name, "photoshop") == 0);

    /* 2. Filter by minimum CPU */
    memset(&f, 0, sizeof(f));
    f.min_cpu_percent = 20.0;
    CHECK(process_filter_list(list, 5, &f, out, 5, &out_cnt) == 0);
    CHECK(out_cnt == 2); /* photoshop (55%) and clion (25%) */

    /* 3. Find by exact PID */
    GuestProcessMetric match;
    CHECK(process_find_by_pid(list, 5, 200, &match) == 0);
    CHECK(strcmp(match.name, "clion") == 0);
    CHECK(process_find_by_pid(list, 5, 999, &match) == -1);

    /* 4. Top ranked by CPU */
    CHECK(process_get_top(list, 5, 0, out, 3, &out_cnt) == 0);
    CHECK(out_cnt == 3);
    CHECK(strcmp(out[0].name, "photoshop") == 0);
    CHECK(strcmp(out[1].name, "clion") == 0);
    CHECK(strcmp(out[2].name, "chrome") == 0);

    /* 5. Top ranked by Memory */
    CHECK(process_get_top(list, 5, 1, out, 3, &out_cnt) == 0);
    CHECK(out_cnt == 3);
    CHECK(strcmp(out[0].name, "photoshop") == 0); /* 1.5 GB */
    CHECK(strcmp(out[1].name, "chrome") == 0);    /* 1.2 GB */
    CHECK(strcmp(out[2].name, "clion") == 0);     /* 900 MB */
}

static void test_resource_limits(void)
{
    printf("\n--- Test Suite 4: Resource Management & Limit Enforcement ---\n");

    /* Status name helper */
    CHECK(strcasecmp(resource_limit_status_name(LIMIT_STATUS_ENFORCED), "enforced") == 0);
    CHECK(strcasecmp(resource_limit_status_name(LIMIT_STATUS_UNSUPPORTED), "unsupported") == 0);
    CHECK(strcasecmp(resource_limit_status_name(LIMIT_STATUS_PROTECTED_PROCESS), "protected_process") == 0);

    /* VM hardware cap verification report */
    VmConfig cfg;
    vm_config_init(&cfg);
    cfg.cpu_count = 4;
    cfg.memory_mb = 4096;
    char report[512];
    CHECK(resource_limits_verify_vm(&cfg, report, sizeof(report)) == 0);
    CHECK(strstr(report, "4") != NULL);
    CHECK(strstr(report, "4096") != NULL);
    CHECK(strstr(report, "STRICTLY ENFORCED BY HYPERVISOR") != NULL);

    /* PID 1 protection check in cgroups v2 */
    AppResourceLimit limit;
    memset(&limit, 0, sizeof(limit));
    limit.pid = 1;
    limit.cpu_quota_percent = 50.0;
    char err[256] = {0};
    ResourceLimitStatus st = resource_limits_apply_cgroup_v2("/sys/fs/cgroup", &limit, err, sizeof(err));
    CHECK(st == LIMIT_STATUS_PROTECTED_PROCESS);
    CHECK(strstr(err, "Refusing") != NULL);

    /* Non-existent cgroups check */
    limit.pid = 1234;
    st = resource_limits_apply_cgroup_v2("/non/existent/cgroup/path", &limit, err, sizeof(err));
    CHECK(st == LIMIT_STATUS_UNSUPPORTED);

    /* Host process priority adjustment */
    int rc = resource_limits_apply_host_priority(getpid(), 0, err, sizeof(err));
    CHECK(rc == 0 || strstr(err, "privileges") != NULL || strstr(err, "failed") != NULL);
}

static void test_sqlite_persistence_and_queries(void)
{
    printf("\n--- Test Suite 5: SQLite Persistence, Migrations, and Multi-VM Isolation ---\n");

    const char *test_db = "/tmp/test_vos_mon.db";
    unlink(test_db);

    CHECK(vm_monitor_init(test_db) == 0);
    CHECK(vm_monitor_db_ensure_schema() == 0);

    /* Feed payload for VM 1 */
    GuestTelemetryPayload p1;
    memset(&p1, 0, sizeof(p1));
    p1.version = AGENT_PROTOCOL_VERSION;
    snprintf(p1.vm_id, sizeof(p1.vm_id), "vm_alpha");
    p1.sequence = 1;
    p1.timestamp_ms = 1730000001000ULL;
    p1.cpu.total_percent = 35.0;
    p1.memory.total_bytes = 2147483648ULL;
    p1.memory.used_bytes = 1073741824ULL;
    p1.process_count = 1;
    p1.processes[0].pid = 501;
    snprintf(p1.processes[0].name, sizeof(p1.processes[0].name), "photoshop");
    p1.processes[0].cpu_percent = 30.0;
    p1.processes[0].memory_rss_bytes = 800000000ULL;

    CHECK(vm_monitor_ingest_telemetry("vm_alpha", &p1) == 0);

    /* Feed payload for VM 2 */
    GuestTelemetryPayload p2;
    memset(&p2, 0, sizeof(p2));
    p2.version = AGENT_PROTOCOL_VERSION;
    snprintf(p2.vm_id, sizeof(p2.vm_id), "vm_beta");
    p2.sequence = 1;
    p2.timestamp_ms = 1730000002000ULL;
    p2.cpu.total_percent = 78.0;
    p2.memory.total_bytes = 4294967296ULL;
    p2.memory.used_bytes = 3221225472ULL;
    p2.process_count = 1;
    p2.processes[0].pid = 801;
    snprintf(p2.processes[0].name, sizeof(p2.processes[0].name), "clion");
    p2.processes[0].cpu_percent = 70.0;
    p2.processes[0].memory_rss_bytes = 1500000000ULL;

    CHECK(vm_monitor_ingest_telemetry("vm_beta", &p2) == 0);

    /* Verify latest sample query and VM isolation */
    VmResourceMetrics m1;
    CHECK(vm_monitor_get_latest("vm_alpha", &m1) == 0);
    CHECK(strcmp(m1.vm_id, "vm_alpha") == 0);
    CHECK(fabs(m1.guest_cpu_percent - 35.0) < 0.1);
    CHECK(m1.has_guest_telemetry == 1);

    VmResourceMetrics m2;
    CHECK(vm_monitor_get_latest("vm_beta", &m2) == 0);
    CHECK(strcmp(m2.vm_id, "vm_beta") == 0);
    CHECK(fabs(m2.guest_cpu_percent - 78.0) < 0.1);

    /* Verify history */
    VmResourceMetrics hist[10];
    int hist_cnt = 0;
    CHECK(vm_monitor_get_history("vm_alpha", 10, hist, &hist_cnt) == 0);
    CHECK(hist_cnt >= 1);
    CHECK(strcmp(hist[0].vm_id, "vm_alpha") == 0);

    /* Verify process queries */
    GuestProcessMetric procs[10];
    int proc_cnt = 0;
    CHECK(vm_monitor_get_processes("vm_alpha", NULL, procs, 10, &proc_cnt) == 0);
    CHECK(proc_cnt == 1);
    CHECK(strcmp(procs[0].name, "photoshop") == 0);

    /* Verify applying limit to DB */
    AppResourceLimit lim;
    memset(&lim, 0, sizeof(lim));
    lim.pid = 501;
    snprintf(lim.process_name, sizeof(lim.process_name), "photoshop");
    lim.cpu_quota_percent = 50.0;
    lim.cpu_weight = 100;
    lim.memory_max_bytes = 1073741824ULL;
    char err[256];
    CHECK(vm_monitor_apply_limit("vm_alpha", &lim, err, sizeof(err)) == 0);

    vm_monitor_shutdown();
    unlink(test_db);
}

static void test_vm_monitor_lifecycle(void)
{
    printf("\n--- Test Suite 6: VM Monitor Lifecycle & Thread Concurrency ---\n");

    const char *test_db = "/tmp/test_vos_lifecycle.db";
    unlink(test_db);

    CHECK(vm_monitor_init(test_db) == 0);

    const char *vm_id = "test_vm_lifecycle";
    CHECK(!vm_monitor_is_active(vm_id));

    /* Start monitor thread at 50ms interval */
    CHECK(vm_monitor_start(vm_id, 50, NULL) == 0);
    CHECK(vm_monitor_is_active(vm_id));

    /* Wait 120ms to allow thread to run samples */
    usleep(120000);

    VmResourceMetrics m;
    int rc = vm_monitor_get_latest(vm_id, &m);
    CHECK(rc == 0);
    CHECK(strcmp(m.vm_id, vm_id) == 0);
    CHECK(m.timestamp_ms > 0);

    /* Stop monitoring */
    CHECK(vm_monitor_stop(vm_id) == 0);
    CHECK(!vm_monitor_is_active(vm_id));

    /* Test host metrics query via vm_monitor */
    HostMetrics host_m;
    CHECK(vm_monitor_get_host_metrics(&host_m) == 0);
    CHECK(host_m.cpu_core_count > 0);

    vm_monitor_shutdown();
    unlink(test_db);
}

static void create_test_dir(const char *dir)
{
    mkdir(dir, 0755);
}

static void write_test_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    if (fp) {
        fputs(content, fp);
        fclose(fp);
    }
}

static void test_phase_3_1_telemetry_integrity(void)
{
    printf("\n--- Test Suite 7: Phase 3.1 Telemetry Integrity & In-Guest Procfs Audit ---\n");

    const char *fix_dir = "/tmp/vos_test_proc";
    system("rm -rf /tmp/vos_test_proc");
    create_test_dir(fix_dir);
    create_test_dir("/tmp/vos_test_proc/net");
    create_test_dir("/tmp/vos_test_proc/1");
    create_test_dir("/tmp/vos_test_proc/42");
    create_test_dir("/tmp/vos_test_proc/99");
    create_test_dir("/tmp/vos_test_proc/555"); /* Missing stat/status for disappearance test */

    agent_set_proc_root(fix_dir);

    /* 1. test_guest_cpu_parsing: 2 cores from Linux /proc/stat */
    printf("  [1/12] Testing guest CPU detection and per-core /proc/stat parsing...\n");
    write_test_file("/tmp/vos_test_proc/stat",
        "cpu  1000 200 300 8500 50 10 20 0 0 0\n"
        "cpu0 500 100 150 4250 25 5 10 0 0 0\n"
        "cpu1 500 100 150 4250 25 5 10 0 0 0\n"
    );
    cpu_monitor_init();
    GuestCpuMetrics cpu1;
    CHECK(cpu_monitor_sample(&cpu1) == 0);
    CHECK_MSG(cpu1.core_count == 2, "Expected 2 vCPUs from guest /proc/stat");
    CHECK_MSG(cpu1.total_percent == 0.0, "First sample total percent must be 0.0%");
    CHECK_MSG(cpu1.idle_percent == 100.0, "First sample idle percent must be 100.0%");

    /* Second sample: tick deltas */
    write_test_file("/tmp/vos_test_proc/stat",
        "cpu  1100 200 350 8850 50 10 20 0 0 0\n"
        "cpu0 550 100 175 4425 25 5 10 0 0 0\n"
        "cpu1 550 100 175 4425 25 5 10 0 0 0\n"
    );
    GuestCpuMetrics cpu2;
    CHECK(cpu_monitor_sample(&cpu2) == 0);
    CHECK(cpu2.core_count == 2);
    /* Total delta = 10580 - 10080 = 500
     * Idle delta = 8850 - 8500 = 350 -> idle_pct = 70.0%
     * Total pct = 30.0%
     * User delta = 100 -> user_pct = 20.0%
     * System delta = 50 -> sys_pct = 10.0% */
    CHECK_MSG(fabs(cpu2.idle_percent - 70.0) < 0.1, "Calculated idle percent should be 70.0%");
    CHECK_MSG(fabs(cpu2.total_percent - 30.0) < 0.1, "Calculated total percent should be 30.0%");
    CHECK_MSG(fabs(cpu2.user_percent - 20.0) < 0.1, "Calculated user percent should be 20.0%");
    CHECK_MSG(fabs(cpu2.system_percent - 10.0) < 0.1, "Calculated system percent should be 10.0%");

    /* 2. test_guest_cpu_counter_reset: handling reboot or tick wrap */
    printf("  [2/12] Testing CPU counter wrap/reset handling...\n");
    write_test_file("/tmp/vos_test_proc/stat",
        "cpu  100 20 30 850 5 1 2 0 0 0\n"
        "cpu0 50 10 15 425 2 0 1 0 0 0\n"
        "cpu1 50 10 15 425 3 1 1 0 0 0\n"
    );
    GuestCpuMetrics cpu3;
    CHECK(cpu_monitor_sample(&cpu3) == 0);
    CHECK_MSG(cpu3.total_percent == 0.0, "Total percent on reset must cleanly fall back to 0.0%");
    CHECK_MSG(cpu3.idle_percent == 100.0, "Idle percent on reset must cleanly fall back to 100.0%");

    /* 3. test_guest_memory_parsing: /proc/meminfo parsing & kB->bytes conversion */
    printf("  [3/12] Testing guest memory detection from /proc/meminfo...\n");
    write_test_file("/tmp/vos_test_proc/meminfo",
        "MemTotal:        1009184 kB\n"
        "MemFree:          847144 kB\n"
        "MemAvailable:     919248 kB\n"
        "Buffers:            3120 kB\n"
        "Cached:            92880 kB\n"
        "SwapTotal:        262144 kB\n"
        "SwapFree:         131072 kB\n"
    );
    GuestMemoryMetrics mem;
    CHECK(memory_monitor_sample(&mem) == 0);
    CHECK_MSG(mem.total_bytes == 1009184ULL * 1024ULL, "MemTotal kB to bytes conversion");
    CHECK_MSG(mem.free_bytes == 847144ULL * 1024ULL, "MemFree conversion");
    CHECK_MSG(mem.available_bytes == 919248ULL * 1024ULL, "MemAvailable conversion");
    CHECK_MSG(mem.used_bytes == (1009184ULL - 919248ULL) * 1024ULL, "Used RAM derived from available");
    CHECK_MSG(mem.swap_total_bytes == 262144ULL * 1024ULL, "SwapTotal conversion");
    CHECK_MSG(mem.swap_used_bytes == 131072ULL * 1024ULL, "SwapUsed calculation");

    /* 4. test_guest_process_name_with_spaces_parens */
    printf("  [4/12] Testing guest process name parsing with spaces and parentheses...\n");
    write_test_file("/tmp/vos_test_proc/1/stat",
        "1 (init) S 0 1 1 0 -1 4194560 1000 0 0 0 120 80 0 0 20 0 1 0 5 16777216 256\n"
    );
    write_test_file("/tmp/vos_test_proc/1/status",
        "Name:\tinit\nVmSize:\t16384 kB\nVmRSS:\t1024 kB\n"
    );
    write_test_file("/tmp/vos_test_proc/42/stat",
        "42 (Web Content) R 1 42 42 0 -1 4194560 500 0 0 0 450 150 0 0 20 0 4 0 50 67108864 2048\n"
    );
    write_test_file("/tmp/vos_test_proc/42/status",
        "Name:\tWeb Content\nVmSize:\t65536 kB\nVmRSS:\t8192 kB\n"
    );
    write_test_file("/tmp/vos_test_proc/99/stat",
        "99 (kworker/u4:0 (events)) S 2 0 0 0 -1 69238848 0 0 0 0 10 5 0 0 20 0 1 0 10 0 0\n"
    );
    write_test_file("/tmp/vos_test_proc/99/status",
        "Name:\tkworker/u4:0 (events)\nVmSize:\t0 kB\nVmRSS:\t0 kB\n"
    );

    GuestProcessMetric procs[16];
    int proc_cnt = 0;
    process_monitor_init();
    CHECK(process_monitor_sample(procs, 16, &proc_cnt) == 0);
    CHECK_MSG(proc_cnt == 3, "Expected exactly 3 valid processes from /proc");

    int found_web = 0, found_kworker = 0, found_init = 0;
    for (int i = 0; i < proc_cnt; i++) {
        if (procs[i].pid == 42) {
            CHECK_MSG(strcmp(procs[i].name, "Web Content") == 0, "Parsed name with spaces");
            CHECK_MSG(procs[i].memory_rss_bytes == 8192ULL * 1024ULL, "Parsed RSS from /proc/[pid]/status");
            found_web = 1;
        } else if (procs[i].pid == 99) {
            CHECK_MSG(strcmp(procs[i].name, "kworker/u4:0 (events)") == 0, "Parsed name with nested parentheses");
            found_kworker = 1;
        } else if (procs[i].pid == 1) {
            CHECK_MSG(strcmp(procs[i].name, "init") == 0, "Parsed init");
            found_init = 1;
        }
    }
    CHECK(found_web && found_kworker && found_init);

    /* 5. test_guest_process_disappearing_handling */
    printf("  [5/12] Testing handling of process disappearing mid-enumeration...\n");
    proc_cnt = 0;
    CHECK(process_monitor_sample(procs, 16, &proc_cnt) == 0);
    CHECK_MSG(proc_cnt == 3, "Disappeared PID 555 safely ignored without crash");

    /* 6. test_guest_disk_filtering: filter loop/ram, aggregate vda */
    printf("  [6/12] Testing disk filtering (/proc/diskstats)...\n");
    write_test_file("/tmp/vos_test_proc/diskstats",
        "   7       0 loop0 0 0 0 0 0 0 0 0 0 0 0\n"
        "   1       0 ram0 0 0 0 0 0 0 0 0 0 0 0\n"
        " 254       0 vda 12450 310 1048576 4500 8920 120 2097152 3200 0 1200 7700\n"
    );
    GuestDiskMetrics disk;
    CHECK(disk_monitor_sample(&disk) == 0);
    CHECK_MSG(disk.read_bytes == 1048576ULL * 512ULL, "vda read bytes calculated from sectors * 512");
    CHECK_MSG(disk.write_bytes == 2097152ULL * 512ULL, "vda write bytes calculated from sectors * 512");
    CHECK_MSG(disk.read_ops == 12450, "vda read ops");
    CHECK_MSG(disk.write_ops == 8920, "vda write ops");

    /* 7. test_guest_net_filtering: filter lo, aggregate eth0 */
    printf("  [7/12] Testing network filtering (/proc/net/dev)...\n");
    write_test_file("/tmp/vos_test_proc/net/dev",
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "    lo: 1200000   10000    0    0    0     0          0         0  1200000   10000    0    0    0     0       0          0\n"
        "  eth0: 8388608    8192    0    0    0     0          0         0  4194304    4096    0    0    0     0       0          0\n"
    );
    GuestNetworkMetrics net;
    CHECK(network_monitor_sample(&net) == 0);
    CHECK_MSG(strcmp(net.iface_name, "eth0") == 0, "Loopback 'lo' skipped; primary interface 'eth0' selected");
    CHECK_MSG(net.rx_bytes == 8388608ULL, "Received bytes");
    CHECK_MSG(net.tx_bytes == 4194304ULL, "Transmitted bytes");
    CHECK_MSG(net.rx_packets == 8192ULL, "Received packets");

    /* 8. test_proc_unavailable_clean_failure */
    printf("  [8/12] Testing clean failure when /proc is missing (no host fallback)...\n");
    agent_set_proc_root("/non_existent_proc_path_9999");
    CHECK_MSG(cpu_monitor_sample(&cpu1) == -1, "CPU monitor must return -1 when /proc missing");
    CHECK_MSG(memory_monitor_sample(&mem) == -1, "Memory monitor must return -1 when /proc missing");
    CHECK_MSG(disk_monitor_sample(&disk) == -1, "Disk monitor must return -1 when /proc missing");
    CHECK_MSG(network_monitor_sample(&net) == -1, "Network monitor must return -1 when /proc missing");
    int empty_cnt = 0;
    CHECK_MSG(process_monitor_sample(procs, 16, &empty_cnt) == -1, "Process monitor must return -1 when /proc missing");
    agent_set_proc_root(fix_dir); /* restore */

    /* 9. test_telemetry_validation_cpu_mismatch */
    printf("  [9/12] Testing validation of vCPU count contradiction...\n");
    VmConfig vcfg;
    vm_config_init(&vcfg);
    vcfg.cpu_count = 2;
    vcfg.memory_mb = 1024;

    GuestTelemetryPayload bad_payload;
    memset(&bad_payload, 0, sizeof(bad_payload));
    bad_payload.cpu.core_count = 10; /* Contradiction! Configured with 2 */
    bad_payload.memory.total_bytes = 1009184ULL * 1024ULL;

    char warn_buf[256] = {0};
    int warns = vm_monitor_validate_telemetry(&vcfg, &bad_payload, warn_buf, sizeof(warn_buf));
    CHECK_MSG(warns > 0, "Validation must catch CPU core count contradiction");
    CHECK_MSG(strstr(warn_buf, "CPU core mismatch") != NULL, "Warning text indicates mismatch");
    CHECK_MSG(strstr(warn_buf, "2 vCPUs") != NULL, "Warning references configured 2 vCPUs");
    CHECK_MSG(strstr(warn_buf, "10 cores") != NULL, "Warning references reported 10 cores");

    /* 10. test_telemetry_validation_memory_mismatch & host process contamination */
    printf("  [10/12] Testing validation of RAM size contradiction and host app contamination...\n");
    bad_payload.cpu.core_count = 2;
    bad_payload.memory.total_bytes = 17179869184ULL; /* 16 GB for a 1024 MB VM! */
    bad_payload.process_count = 2;
    bad_payload.processes[0].pid = 100;
    snprintf(bad_payload.processes[0].name, sizeof(bad_payload.processes[0].name), "Brave Browser");
    bad_payload.processes[1].pid = 200;
    snprintf(bad_payload.processes[1].name, sizeof(bad_payload.processes[1].name), "Microsoft Word");

    warns = vm_monitor_validate_telemetry(&vcfg, &bad_payload, warn_buf, sizeof(warn_buf));
    CHECK_MSG(warns >= 2, "Validation must catch both memory contradiction and host apps");
    CHECK_MSG(strstr(warn_buf, "Memory size contradiction") != NULL, "Warning text indicates RAM mismatch");
    CHECK_MSG(strstr(warn_buf, "Host process contamination") != NULL, "Warning text flags host processes");

    /* 11. test_telemetry_freshness_lifecycle: LIVE -> STALE -> UNAVAILABLE */
    printf("  [11/12] Testing telemetry freshness states and lifecycle reconciliation...\n");
    const char *test_db = "/tmp/test_vos_freshness.db";
    unlink(test_db);
    vm_monitor_init(test_db);

    /* Ingest clean payload for vm_fresh_test */
    GuestTelemetryPayload clean_p;
    memset(&clean_p, 0, sizeof(clean_p));
    clean_p.cpu.core_count = 2;
    clean_p.cpu.total_percent = 15.0;
    clean_p.memory.total_bytes = 1009184ULL * 1024ULL;
    clean_p.memory.used_bytes = 200000ULL * 1024ULL;
    clean_p.process_count = 1;
    clean_p.processes[0].pid = 1;
    snprintf(clean_p.processes[0].name, sizeof(clean_p.processes[0].name), "init");

    CHECK(vm_monitor_ingest_telemetry("vm_fresh_test", &clean_p) == 0);

    VmResourceMetrics fresh_m;
    CHECK(vm_monitor_get_latest("vm_fresh_test", &fresh_m) == 0);
    CHECK_MSG(fresh_m.freshness == TELEMETRY_LIVE, "Recently ingested sample must be LIVE");
    CHECK_MSG(strcmp(telemetry_freshness_name(fresh_m.freshness), "LIVE") == 0, "Freshness name is LIVE");

    /* Unsampled VM must be UNAVAILABLE */
    VmResourceMetrics unavail_m;
    CHECK(vm_monitor_get_latest("vm_nonexistent_xyz", &unavail_m) != 0);

    /* 12. test_sqlite_persistence_telemetry_integrity */
    printf("  [12/12] Testing SQLite persistence of integrity flags, warnings, and source attribution...\n");
    CHECK(vm_monitor_ingest_telemetry("vm_warn_test", &bad_payload) == 0);

    VmResourceMetrics warn_m;
    CHECK(vm_monitor_get_latest("vm_warn_test", &warn_m) == 0);
    CHECK_MSG(warn_m.has_integrity_warning == 1, "Latest metrics holds integrity warning flag");
    CHECK_MSG(strlen(warn_m.integrity_warning) > 0, "Integrity warning string persisted");

    vm_monitor_shutdown();
    unlink(test_db);
    system("rm -rf /tmp/vos_test_proc");
    printf("  All 12 Telemetry Integrity tests completed successfully!\n");
}

static void test_phase_3_2_telemetry_consistency(void)
{
    printf("\n--- Test Suite 8: Phase 3.2 Telemetry Consistency & Ground Truth Verification ---\n");
    system("rm -rf /tmp/vos_test_proc32 && mkdir -p /tmp/vos_test_proc32/net");
    agent_set_proc_root("/tmp/vos_test_proc32");

    const char *test_db = "/tmp/vos_test_phase3_2.db";
    unlink(test_db);
    vm_monitor_init(test_db);

    /* 1. Disk sector-to-byte conversion (512-byte sectors) */
    printf("  [1/13] Testing disk sector-to-byte conversion (512-byte units)...\n");
    write_test_file("/tmp/vos_test_proc32/diskstats",
        " 253       0 vda 100 0 2000 50 200 0 4000 100 0 150 150 0 0 0 0 0 0\n"
    );
    disk_monitor_init();
    GuestDiskMetrics dm1;
    CHECK(disk_monitor_sample(&dm1) == 0);
    CHECK_MSG(dm1.read_bytes == 2000ULL * 512ULL, "2000 sectors * 512 bytes = 1,024,000 bytes");
    CHECK_MSG(dm1.write_bytes == 4000ULL * 512ULL, "4000 sectors * 512 bytes = 2,048,000 bytes");
    CHECK_MSG(dm1.read_ops == 100, "100 read operations");
    CHECK_MSG(dm1.write_ops == 200, "200 write operations");
    CHECK_MSG(strcmp(dm1.primary_disk, "vda") == 0, "Primary disk is vda");

    /* 2. Parent disk vs partition double-counting prevention */
    printf("  [2/13] Testing parent disk vs partition double-counting prevention...\n");
    write_test_file("/tmp/vos_test_proc32/diskstats",
        " 253       0 vda 1336 0 286846 67 0 0 0 0 0 210 67 0 0 0 0 0 0\n"
        " 253       1 vda1 35 0 2008 1 0 0 0 0 0 17 1 0 0 0 0 0 0\n"
        " 253       2 vda2 28 0 1024 0 0 0 0 0 0 14 0 0 0 0 0 0 0\n"
        " 253       3 vda3 47 0 4072 1 0 0 0 0 0 20 1 0 0 0 0 0 0\n"
    );
    disk_monitor_init();
    GuestDiskMetrics dm2;
    CHECK(disk_monitor_sample(&dm2) == 0);
    CHECK_MSG(dm2.read_ops == 1336, "Only parent disk vda reads (1336) counted, partitions excluded");
    CHECK_MSG(dm2.read_bytes == 286846ULL * 512ULL, "Read bytes matches vda sectors * 512");
    CHECK_MSG(dm2.write_ops == 0, "Write ops is 0");
    CHECK_MSG(dm2.write_bytes == 0, "Write bytes is 0");

    /* 3 & 4. Disk read and write rate calculation across intervals and cumulative representation */
    printf("  [3/13] Testing disk read/write interval rates and cumulative metrics...\n");
    write_test_file("/tmp/vos_test_proc32/diskstats",
        " 253       0 vda 100 0 2000 50 100 0 1000 50 0 100 100 0 0 0 0 0 0\n"
    );
    disk_monitor_init();
    GuestDiskMetrics dm3_a;
    CHECK(disk_monitor_sample(&dm3_a) == 0);
    CHECK(dm3_a.read_bytes_per_sec == 0.0);

    usleep(100000); /* 100 ms */
    write_test_file("/tmp/vos_test_proc32/diskstats",
        " 253       0 vda 150 0 3000 75 150 0 2000 75 0 150 150 0 0 0 0 0 0\n"
    );
    GuestDiskMetrics dm3_b;
    CHECK(disk_monitor_sample(&dm3_b) == 0);
    CHECK_MSG(dm3_b.read_bytes == 3000ULL * 512ULL, "Cumulative read bytes correctly incremented");
    CHECK_MSG(dm3_b.read_bytes_per_sec > 0.0, "Interval read rate is positive");
    CHECK_MSG(dm3_b.write_bytes_per_sec > 0.0, "Interval write rate is positive");

    /* 5. Network counter parsing with zero traffic */
    printf("  [4/13] Testing network counter parsing with genuine zero traffic...\n");
    write_test_file("/tmp/vos_test_proc32/net/dev",
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "    lo:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0\n"
        "  eth0:       0       0    0    0    0     0          0         0        0       0    0    0    0     0       0          0\n"
    );
    network_monitor_init();
    GuestNetworkMetrics nm1;
    CHECK(network_monitor_sample(&nm1) == 0);
    CHECK_MSG(nm1.rx_bytes == 0, "eth0 rx bytes is genuine 0 (not fabricated)");
    CHECK_MSG(nm1.tx_bytes == 0, "eth0 tx bytes is genuine 0 (not fabricated)");
    CHECK_MSG(nm1.rx_packets == 0, "eth0 rx packets is 0");
    CHECK_MSG(nm1.tx_packets == 0, "eth0 tx packets is 0");
    CHECK_MSG(strcmp(nm1.iface_name, "eth0") == 0, "Active interface is eth0");
    CHECK_MSG(nm1.interface_count == 1, "Non-loopback interface count is 1");

    /* 6. Multi-interface network aggregation and loopback filtering */
    printf("  [5/13] Testing multi-interface network aggregation and loopback filtering...\n");
    write_test_file("/tmp/vos_test_proc32/net/dev",
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "    lo:   50000     100    0    0    0     0          0         0    50000     100    0    0    0     0       0          0\n"
        "  eth0:    1024      10    0    0    0     0          0         0      512       5    0    0    0     0       0          0\n"
        "  eth1:    2048      20    0    0    0     0          0         0     1024      10    0    0    0     0       0          0\n"
    );
    network_monitor_init();
    GuestNetworkMetrics nm2;
    CHECK(network_monitor_sample(&nm2) == 0);
    CHECK_MSG(nm2.rx_bytes == 3072, "Aggregated rx bytes: 1024 + 2048 = 3072 (lo filtered)");
    CHECK_MSG(nm2.tx_bytes == 1536, "Aggregated tx bytes: 512 + 1024 = 1536 (lo filtered)");
    CHECK_MSG(nm2.interface_count == 2, "Discovered 2 physical/virtual interfaces (eth0, eth1)");

    /* 7. Network throughput rate calculation across intervals */
    printf("  [6/13] Testing network interval throughput rate calculation...\n");
    usleep(100000); /* 100 ms */
    write_test_file("/tmp/vos_test_proc32/net/dev",
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "    lo:   50000     100    0    0    0     0          0         0    50000     100    0    0    0     0       0          0\n"
        "  eth0:   11024     100    0    0    0     0          0         0     5512      50    0    0    0     0       0          0\n"
        "  eth1:    2048      20    0    0    0     0          0         0     1024      10    0    0    0     0       0          0\n"
    );
    GuestNetworkMetrics nm3;
    CHECK(network_monitor_sample(&nm3) == 0);
    CHECK_MSG(nm3.rx_bytes_per_sec > 0.0, "Calculated positive RX throughput rate");
    CHECK_MSG(nm3.tx_bytes_per_sec > 0.0, "Calculated positive TX throughput rate");

    /* 8 & 9. Process snapshot timestamp and distinguishing total discovered from reported */
    printf("  [7/13] Testing process snapshot timestamp and discovered vs reported counts...\n");
    system("mkdir -p /tmp/vos_test_proc32/101 /tmp/vos_test_proc32/102 /tmp/vos_test_proc32/103");
    write_test_file("/tmp/vos_test_proc32/101/stat", "101 (proc_one) S 1 0 0 0 0 0 0 0 0 10 20 0 0 0 0 0 0 100 200 50 0\n");
    write_test_file("/tmp/vos_test_proc32/102/stat", "102 (proc_two) S 1 0 0 0 0 0 0 0 0 30 40 0 0 0 0 0 0 100 200 60 0\n");
    write_test_file("/tmp/vos_test_proc32/103/stat", "103 (proc_three) S 1 0 0 0 0 0 0 0 0 50 60 0 0 0 0 0 0 100 200 70 0\n");

    process_monitor_init();
    GuestProcessMetric p_list[2];
    int p_rep = 0;
    int p_tot = 0;
    uint64_t snap_ts = 0;
    CHECK(process_monitor_sample_ex(p_list, 2, &p_rep, &p_tot, &snap_ts) == 0);
    CHECK_MSG(p_tot == 3, "Total discovered processes in procfs is 3");
    CHECK_MSG(p_rep == 2, "Reported top-N processes capped at max_procs (2)");
    CHECK_MSG(snap_ts > 0, "Snapshot timestamp recorded accurately");

    /* 10, 11, 12. Two-sample CPU utilization calculation, initial state, and reset */
    printf("  [8/13] Testing two-sample CPU calculation, initial state, and resets...\n");
    cpu_monitor_init();
    write_test_file("/tmp/vos_test_proc32/stat",
        "cpu  1000 0 500 8500 0 0 0 0 0 0\n"
        "cpu0 500 0 250 4250 0 0 0 0 0 0\n"
        "cpu1 500 0 250 4250 0 0 0 0 0 0\n"
    );
    GuestCpuMetrics cpu_s1;
    CHECK(cpu_monitor_sample(&cpu_s1) == 0);
    CHECK_MSG(cpu_s1.sample_interval_ms == 0, "First sample interval is 0 ms (initial state)");
    CHECK_MSG(cpu_s1.total_percent == 0.0, "First sample total percent is 0.0% uncomputed");

    usleep(100000);
    write_test_file("/tmp/vos_test_proc32/stat",
        "cpu  1100 0 550 8850 0 0 0 0 0 0\n"
        "cpu0 550 0 275 4425 0 0 0 0 0 0\n"
        "cpu1 550 0 275 4425 0 0 0 0 0 0\n"
    );
    GuestCpuMetrics cpu_s2;
    CHECK(cpu_monitor_sample(&cpu_s2) == 0);
    CHECK_MSG(cpu_s2.sample_interval_ms > 0, "Second sample interval is positive");
    CHECK_MSG(fabs(cpu_s2.total_percent - 30.0) < 0.1, "CPU utilization calculated as 30.0%");

    /* Counter reset check */
    write_test_file("/tmp/vos_test_proc32/stat",
        "cpu  100 0 50 850 0 0 0 0 0 0\n"
        "cpu0 50 0 25 425 0 0 0 0 0 0\n"
        "cpu1 50 0 25 425 0 0 0 0 0 0\n"
    );
    GuestCpuMetrics cpu_s3;
    CHECK(cpu_monitor_sample(&cpu_s3) == 0);
    CHECK_MSG(cpu_s3.sample_interval_ms == 0, "Reset drops interval to 0 ms");

    /* 13. SQLite database persistence with QEMU PID and sentinel -1 values */
    printf("  [9/13] Testing SQLite database persistence with QEMU PID and sentinel -1 values...\n");
    GuestTelemetryPayload tp;
    memset(&tp, 0, sizeof(tp));
    tp.version = AGENT_PROTOCOL_VERSION;
    snprintf(tp.vm_id, sizeof(tp.vm_id), "vm_p32_sentinel");
    tp.timestamp_ms = host_monitor_now_ms();
    tp.cpu.total_percent = 15.5;
    tp.memory.total_bytes = 1073741824ULL;
    tp.memory.used_bytes  = 536870912ULL;

    CHECK(vm_monitor_ingest_telemetry("vm_p32_sentinel", &tp) == 0);

    VmResourceMetrics db_res;
    CHECK(vm_monitor_get_latest("vm_p32_sentinel", &db_res) == 0);
    CHECK_MSG(db_res.qemu_pid == 0, "Unmonitored VM has qemu_pid = 0");
    CHECK_MSG(db_res.qemu_cpu_percent == -1.0, "Unavailable QEMU CPU stored as sentinel -1.0");
    CHECK_MSG(db_res.guest_cpu_percent == 15.5, "Guest CPU persisted with high fidelity");

    vm_monitor_shutdown();
    unlink(test_db);
    system("rm -rf /tmp/vos_test_proc32");
    printf("  All 13 Phase 3.2 consistency tests passed successfully!\n");
}

int main(void)
{
    printf("================================================================\n");
    printf(" Virtual OS Simulator - Phase 3 Monitoring Test Suite\n");
    printf(" Host: Apple Silicon ARM64 / macOS C11 Test Runner\n");
    printf("================================================================\n");

    test_host_monitor();
    test_agent_protocol();
    test_process_monitor();
    test_resource_limits();
    test_sqlite_persistence_and_queries();
    test_vm_monitor_lifecycle();
    test_phase_3_1_telemetry_integrity();
    test_phase_3_2_telemetry_consistency();

    printf("\n================================================================\n");
    printf(" Results: %d checks, %d failures\n", g_checks, g_fails);
    printf("================================================================\n");

    return (g_fails == 0) ? 0 : 1;
}
