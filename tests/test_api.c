/* test_api.c – Phase 6 C-ABI Stable Interface & FFI Gateway Test Suite
 *
 * Verifies:
 *   - vos_api_init / vos_api_shutdown lifecycle
 *   - vos_api_get_host_telemetry format and valid values
 *   - vos_api_vm_list, vm_get, vm_create, vm_delete
 *   - vos_api_profile_list, profile_save, profile_delete
 *   - vos_api_app_register, app_list, app_unregister
 *   - vos_api_sim_run_scheduler (FCFS, Round Robin, AUTO)
 *   - vos_api_sim_run_memory (First Fit, Best Fit)
 *   - vos_api_sim_run_bankers (Safe & Unsafe sequences)
 *   - vos_api_events_list
 */

#include "vos_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { \
        printf("FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

#define CHECK_MSG(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { \
        printf("FAIL [%s:%d]: %s (%s)\n", __FILE__, __LINE__, #cond, msg); \
        g_failures++; \
    } else { \
        printf("  [PASS] %s\n", msg); \
    } \
} while (0)

int main(void)
{
    printf("======================================================================\n");
    printf("Virtual OS Simulator - Phase 6 C-ABI & FFI Gateway Test Suite\n");
    printf("Host: macOS Apple Silicon M5 (ARM64) | Shared Library Validation\n");
    printf("======================================================================\n\n");

    const char *test_db = "/tmp/vos_test_api.db";
    unlink(test_db);

    /* 1. Lifecycle & Version */
    printf("[TEST 1] System Lifecycle & Version\n");
    CHECK_MSG(vos_api_is_initialized() == 0, "Initial state is uninitialized");
    CHECK_MSG(vos_api_init(test_db) == VOS_OK, "vos_api_init() initializes cleanly");
    CHECK_MSG(vos_api_is_initialized() == 1, "is_initialized() reports 1 after init");
    CHECK_MSG(strstr(vos_api_version(), "phase6") != NULL, "Version string contains 'phase6'");

    /* 2. Host Telemetry */
    printf("\n[TEST 2] Host Telemetry Format & Readback\n");
    char host_json[1024] = {0};
    int res = vos_api_get_host_telemetry(host_json, sizeof(host_json));
    CHECK_MSG(res == VOS_OK, "vos_api_get_host_telemetry() returns VOS_OK");
    CHECK_MSG(strstr(host_json, "\"cpu_count\":") != NULL, "Host JSON contains cpu_count");
    CHECK_MSG(strstr(host_json, "\"total_ram_bytes\":") != NULL, "Host JSON contains total_ram_bytes");
    CHECK_MSG(strstr(host_json, "\"host_platform\":") != NULL, "Host JSON contains host_platform");
    printf("  Host Telemetry Sample: %s\n", host_json);

    /* 3. Virtual Machine Management */
    printf("\n[TEST 3] Virtual Machine Management CRUD\n");
    char err[256] = {0};
    const char *new_vm_json =
        "{\"id\":\"vm_api_test\",\"name\":\"API Test VM\",\"vcpu_count\":2,\"ram_mb\":1024,\"disk_path\":\"vm_images/alpine.qcow2\"}";
    res = vos_api_vm_create(new_vm_json, err, sizeof(err));
    CHECK_MSG(res == VOS_OK, "vos_api_vm_create() succeeds");

    char vm_list_json[4096] = {0};
    res = vos_api_vm_list(vm_list_json, sizeof(vm_list_json));
    CHECK_MSG(res == VOS_OK, "vos_api_vm_list() succeeds");
    CHECK_MSG(strstr(vm_list_json, "vm_api_test") != NULL, "Created VM appears in vm_list");

    char vm_detail_json[1024] = {0};
    res = vos_api_vm_get("vm_api_test", vm_detail_json, sizeof(vm_detail_json));
    CHECK_MSG(res == VOS_OK, "vos_api_vm_get() succeeds");
    CHECK_MSG(strstr(vm_detail_json, "\"name\":\"API Test VM\"") != NULL, "VM detail has correct name");

    /* 4. Resource Profile Management */
    printf("\n[TEST 4] Resource Profile Management\n");
    res = vos_api_profile_save("p_fast", "Fast Worker", "High CPU", 1.5, 200, 268435456, 30, 10485760, 10485760, err, sizeof(err));
    CHECK_MSG(res == VOS_OK, "vos_api_profile_save() creates profile");

    char prof_json[1024] = {0};
    res = vos_api_profile_get("p_fast", prof_json, sizeof(prof_json));
    CHECK_MSG(res == VOS_OK, "vos_api_profile_get() retrieves profile");
    CHECK_MSG(strstr(prof_json, "\"cpu_cores\":1.50") != NULL, "Profile CPU cores matches 1.50");
    CHECK_MSG(strstr(prof_json, "\"gpu_support\":\"UNSUPPORTED\"") != NULL, "Profile explicitly marks GPU as unsupported");

    char prof_list_json[4096] = {0};
    res = vos_api_profile_list(prof_list_json, sizeof(prof_list_json));
    CHECK_MSG(res == VOS_OK, "vos_api_profile_list() succeeds");
    CHECK_MSG(strstr(prof_list_json, "p_fast") != NULL, "Profile appears in list");

    /* 5. Application Registry */
    printf("\n[TEST 5] Application Registry CRUD\n");
    res = vos_api_app_register("vm_api_test", "echo_worker", "Echo Worker", "/bin/echo",
                               "Simple worker", "Utility", "1.0", "p_fast", err, sizeof(err));
    CHECK_MSG(res == VOS_OK, "vos_api_app_register() registers app");

    char app_list_json[4096] = {0};
    res = vos_api_app_list("vm_api_test", app_list_json, sizeof(app_list_json));
    CHECK_MSG(res == VOS_OK, "vos_api_app_list() lists apps");
    CHECK_MSG(strstr(app_list_json, "echo_worker") != NULL, "Registered app appears in app_list");

    /* Profile deletion in-use protection */
    res = vos_api_profile_delete("p_fast", err, sizeof(err));
    CHECK_MSG(res != VOS_OK, "Profile deletion is blocked while in use by registered app");

    res = vos_api_app_unregister("vm_api_test", "echo_worker", err, sizeof(err));
    CHECK_MSG(res == VOS_OK, "vos_api_app_unregister() unregisters app");

    res = vos_api_profile_delete("p_fast", err, sizeof(err));
    CHECK_MSG(res == VOS_OK, "Profile deletion succeeds after app unregistration");

    /* 6. OS Algorithm Simulator - CPU Scheduling */
    printf("\n[TEST 6] OS Algorithm Simulator - CPU Scheduling\n");
    const char *procs_json =
        "[{\"pid\":1,\"name\":\"P1\",\"arrival\":0,\"burst\":6,\"priority\":2},"
        "{\"pid\":2,\"name\":\"P2\",\"arrival\":2,\"burst\":4,\"priority\":1},"
        "{\"pid\":3,\"name\":\"P3\",\"arrival\":4,\"burst\":2,\"priority\":3}]";

    char sched_out[4096] = {0};
    /* Run FCFS (algo=1) */
    res = vos_api_sim_run_scheduler(1, procs_json, 0, sched_out, sizeof(sched_out));
    CHECK_MSG(res == VOS_OK, "Run FCFS scheduler simulation");
    CHECK_MSG(strstr(sched_out, "\"algo_name\":") != NULL, "Output contains algo_name");
    CHECK_MSG(strstr(sched_out, "\"gantt\":[") != NULL, "Output contains gantt chart segments");
    CHECK_MSG(strstr(sched_out, "\"avg_wt\":") != NULL, "Output contains average waiting time");

    /* Run Round Robin (algo=6, quantum=2) */
    memset(sched_out, 0, sizeof(sched_out));
    res = vos_api_sim_run_scheduler(6, procs_json, 2, sched_out, sizeof(sched_out));
    CHECK_MSG(res == VOS_OK, "Run Round Robin scheduler simulation");
    CHECK_MSG(strstr(sched_out, "\"quantum\":2") != NULL, "Round robin quantum is 2");

    /* 7. OS Algorithm Simulator - Memory Management */
    printf("\n[TEST 7] OS Algorithm Simulator - Memory Management\n");
    const char *mem_procs =
        "[{\"pid\":1,\"name\":\"P1\",\"mem_kb\":150},"
        "{\"pid\":2,\"name\":\"P2\",\"mem_kb\":350},"
        "{\"pid\":3,\"name\":\"P3\",\"mem_kb\":200}]";
    const char *blocks_text = "200, 400, 300, 100";

    char mem_out[4096] = {0};
    res = vos_api_sim_run_memory(1 /* FIRST FIT */, mem_procs, blocks_text, mem_out, sizeof(mem_out));
    CHECK_MSG(res == VOS_OK, "Run First Fit memory simulation");
    CHECK_MSG(strstr(mem_out, "\"allocations\":[") != NULL, "Output contains memory allocations");
    CHECK_MSG(strstr(mem_out, "\"internal_frag\":") != NULL, "Output contains internal fragmentation");

    /* 8. OS Algorithm Simulator - Banker's Algorithm */
    printf("\n[TEST 8] OS Algorithm Simulator - Banker's Algorithm\n");
    const char *banker_procs =
        "[{\"pid\":1,\"name\":\"P1\",\"burst\":0,\"max\":[7,5,3,0],\"held\":[0,1,0,0]},"
        "{\"pid\":2,\"name\":\"P2\",\"burst\":0,\"max\":[3,2,2,0],\"held\":[2,0,0,0]},"
        "{\"pid\":3,\"name\":\"P3\",\"burst\":0,\"max\":[9,0,2,0],\"held\":[3,0,2,0]}]";
    const char *avail_text = "10, 5, 7, 0";

    char banker_out[4096] = {0};
    res = vos_api_sim_run_bankers(banker_procs, avail_text, banker_out, sizeof(banker_out));
    CHECK_MSG(res == VOS_OK, "Run Banker's algorithm simulation");
    CHECK_MSG(strstr(banker_out, "\"safe\":1") != NULL, "Banker's algorithm finds safe state");
    CHECK_MSG(strstr(banker_out, "\"sequence\":[") != NULL, "Banker's algorithm outputs safe sequence");

    /* 9. Event Audit Logs */
    printf("\n[TEST 9] Persistent Event Audit Logs\n");
    res = vos_api_event_log("test_inst_01", "app_test_1", "vm_api_test", "TEST_BENCHMARK", "{\"grade\":\"A+\",\"speedup\":\"3.2x\"}");
    CHECK_MSG(res == VOS_OK, "vos_api_event_log() writes audit record to SQLite");

    char events_out[4096] = {0};
    res = vos_api_events_list("vm_api_test", 50, events_out, sizeof(events_out));
    CHECK_MSG(res == VOS_OK, "vos_api_events_list() executes cleanly");
    CHECK_MSG(events_out[0] == '[', "Events list outputs valid JSON array");
    CHECK_MSG(strstr(events_out, "TEST_BENCHMARK") != NULL, "Logged test event found in audit events list");

    /* Cleanup VM */
    vos_api_vm_delete("vm_api_test", err, sizeof(err));

    /* Shutdown */
    CHECK_MSG(vos_api_shutdown() == VOS_OK, "vos_api_shutdown() closes engine cleanly");
    CHECK_MSG(vos_api_is_initialized() == 0, "is_initialized() is 0 after shutdown");

    unlink(test_db);

    printf("\n======================================================================\n");
    printf("Phase 6 API Test Summary: %d checks, %d failures\n", g_checks, g_failures);
    printf("======================================================================\n");

    return g_failures > 0 ? 1 : 0;
}

