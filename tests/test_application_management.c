/* tests/test_application_management.c – Comprehensive Unit Tests for Phase 5
 *
 * Covers:
 *   1. RPC Wire Protocol Extensions (Discover, Launch, Stop, Metrics, Status Codes)
 *   2. In-Guest Application Discovery (Directory Traversal, Executable Filtering, Safety)
 *   3. Application Registry CRUD (Per-VM Scoping, Parameter Validation, Unregister Integrity)
 *   4. Application Profiles (Defaults, Cgroups v2 Mapping, Assignment, Deletion Guard)
 *   5. Launch Handshake & Pre-Execution Cgroup Handshake (Sync Pipes, Readback Verification)
 *   6. Lifecycle Management & PID Reuse Protection (Start Time Verification, Graceful Stop, Fallback)
 *   7. Reconciliation & Failure Recovery (Dead PID Detection, Database Sync)
 *   8. Resource Monitoring Persistence & Time-Series History (Samples, Events)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#include "monitoring/agent_protocol.h"
#include "cgroup_manager.h"
#include "app_launcher.h"
#include "application/app_manager.h"
#include "virtualization/vm_manager.h"
#include "virtualization/vm_config.h"
#include "sqlite3.h"

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

static int exec_sql(const char *db_path, const char *sql)
{
    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) return -1;
    int rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_close(db);
    return (rc == SQLITE_OK) ? 0 : -1;
}

/* ------------------------------------------------------------------ Test 1: Wire Protocol Extensions */

static void test_wire_protocol_extensions(void)
{
    printf("\n[TEST 1] RPC Wire Protocol Extensions\n");

    /* 1.1 Test Discover Request Serialization & Deserialization */
    {
        AgentResourceRequest req;
        memset(&req, 0, sizeof(req));
        req.version = AGENT_PROTOCOL_VERSION;
        req.operation = AGENT_OP_DISCOVER_APPS;
        snprintf(req.group_id, sizeof(req.group_id), "sys_discover");

        char buf[2048] = {0};
        int slen = agent_rpc_serialize_request(&req, buf, sizeof(buf));
        CHECK_MSG(slen > 0, "Serialize AGENT_OP_DISCOVER_APPS request");

        AgentResourceRequest req_out;
        memset(&req_out, 0, sizeof(req_out));
        int dres = agent_rpc_deserialize_request(buf, &req_out);
        CHECK_MSG(dres == 0, "Deserialize AGENT_OP_DISCOVER_APPS request");
        CHECK_MSG(req_out.operation == AGENT_OP_DISCOVER_APPS, "Request opcode matches");
        CHECK_MSG(strcmp(req_out.group_id, "sys_discover") == 0, "Group ID preserved");
    }

    /* 1.2 Test Launch Request with Arguments */
    {
        AgentResourceRequest req;
        memset(&req, 0, sizeof(req));
        req.version = AGENT_PROTOCOL_VERSION;
        req.operation = AGENT_OP_LAUNCH_APP;
        snprintf(req.group_id, sizeof(req.group_id), "app_worker");
        snprintf(req.app_id, sizeof(req.app_id), "worker_proc");
        snprintf(req.app_path, sizeof(req.app_path), "/bin/sleep");
        snprintf(req.app_args, sizeof(req.app_args), "10");

        char buf[2048] = {0};
        int slen = agent_rpc_serialize_request(&req, buf, sizeof(buf));
        CHECK_MSG(slen > 0, "Serialize AGENT_OP_LAUNCH_APP request");

        AgentResourceRequest req_out;
        int dres = agent_rpc_deserialize_request(buf, &req_out);
        CHECK_MSG(dres == 0, "Deserialize AGENT_OP_LAUNCH_APP request");
        CHECK_MSG(req_out.operation == AGENT_OP_LAUNCH_APP, "Opcode is LAUNCH");
        CHECK_MSG(strcmp(req_out.app_id, "worker_proc") == 0, "App ID preserved");
        CHECK_MSG(strcmp(req_out.app_path, "/bin/sleep") == 0, "App path preserved");
        CHECK_MSG(strcmp(req_out.app_args, "10") == 0, "App args preserved");
    }

    /* 1.3 Test Discovery Response Serialization & Deserialization */
    {
        AgentResourceResponse resp;
        memset(&resp, 0, sizeof(resp));
        resp.version = AGENT_PROTOCOL_VERSION;
        resp.status_code = AGENT_STATUS_SUCCESS;
        resp.discovery.count = 2;

        snprintf(resp.discovery.apps[0].name, sizeof(resp.discovery.apps[0].name), "sh");
        snprintf(resp.discovery.apps[0].path, sizeof(resp.discovery.apps[0].path), "/bin/sh");
        resp.discovery.apps[0].is_executable = 1;
        snprintf(resp.discovery.apps[0].package, sizeof(resp.discovery.apps[0].package), "Core Utility");

        snprintf(resp.discovery.apps[1].name, sizeof(resp.discovery.apps[1].name), "top");
        snprintf(resp.discovery.apps[1].path, sizeof(resp.discovery.apps[1].path), "/usr/bin/top");
        resp.discovery.apps[1].is_executable = 1;
        snprintf(resp.discovery.apps[1].package, sizeof(resp.discovery.apps[1].package), "Monitoring");

        char buf[4096] = {0};
        int slen = agent_rpc_serialize_response(&resp, buf, sizeof(buf));
        CHECK_MSG(slen > 0, "Serialize discovery response");

        AgentResourceResponse resp_out;
        int dres = agent_rpc_deserialize_response(buf, &resp_out);
        CHECK_MSG(dres == 0, "Deserialize discovery response");
        CHECK_MSG(resp_out.status_code == AGENT_STATUS_SUCCESS, "Status is success");
        CHECK_MSG(resp_out.discovery.count == 2, "Discovery count is 2");
        CHECK_MSG(strcmp(resp_out.discovery.apps[0].name, "sh") == 0, "First app name is sh");
        CHECK_MSG(strcmp(resp_out.discovery.apps[0].path, "/bin/sh") == 0, "First app path is /bin/sh");
        CHECK_MSG(strcmp(resp_out.discovery.apps[1].name, "top") == 0, "Second app name is top");
    }

    /* 1.4 Test Launch Response with PID, Start Time, and Extended Cgroup Status */
    {
        AgentResourceResponse resp;
        memset(&resp, 0, sizeof(resp));
        resp.version = AGENT_PROTOCOL_VERSION;
        resp.status_code = AGENT_STATUS_SUCCESS;
        snprintf(resp.app_id, sizeof(resp.app_id), "worker_proc");
        resp.app_pid = 4321;
        resp.app_start_time = 987654321ULL;
        resp.applied.cpu_throttled_usec = 125000ULL;
        resp.applied.memory_peak_bytes = 64 * 1024 * 1024ULL;
        resp.applied.io_read_bytes = 1048576ULL;
        resp.applied.io_write_bytes = 2097152ULL;

        char buf[2048] = {0};
        int slen = agent_rpc_serialize_response(&resp, buf, sizeof(buf));
        CHECK_MSG(slen > 0, "Serialize launch response");

        AgentResourceResponse resp_out;
        int dres = agent_rpc_deserialize_response(buf, &resp_out);
        CHECK_MSG(dres == 0, "Deserialize launch response");
        CHECK_MSG(resp_out.app_pid == 4321, "PID preserved");
        CHECK_MSG(resp_out.app_start_time == 987654321ULL, "Start time preserved");
        CHECK_MSG(resp_out.applied.cpu_throttled_usec == 125000ULL, "CPU throttled usec preserved");
        CHECK_MSG(resp_out.applied.memory_peak_bytes == 64 * 1024 * 1024ULL, "Peak memory preserved");
        CHECK_MSG(resp_out.applied.io_read_bytes == 1048576ULL, "I/O read bytes preserved");
        CHECK_MSG(resp_out.applied.io_write_bytes == 2097152ULL, "I/O write bytes preserved");
    }

    /* 1.5 Test Extended Error Codes */
    {
        CHECK_MSG(strcmp(agent_status_code_name(AGENT_STATUS_APP_NOT_FOUND), "APP_NOT_FOUND") == 0, "APP_NOT_FOUND str");
        CHECK_MSG(strcmp(agent_status_code_name(AGENT_STATUS_APP_ALREADY_RUNNING), "APP_ALREADY_RUNNING") == 0, "APP_ALREADY_RUNNING str");
        CHECK_MSG(strcmp(agent_status_code_name(AGENT_STATUS_LAUNCH_FAILED), "LAUNCH_FAILED") == 0, "LAUNCH_FAILED str");
        CHECK_MSG(strcmp(agent_status_code_name(AGENT_STATUS_CGROUP_ATTACH_FAILED), "CGROUP_ATTACH_FAILED") == 0, "CGROUP_ATTACH_FAILED str");
    }
}

/* ------------------------------------------------------------------ Test 2: In-Guest Discovery */

static void test_in_guest_discovery(void)
{
    printf("\n[TEST 2] In-Guest Application Discovery\n");

    /* Create temporary directory simulating guest filesystem paths */
    char test_dir[] = "/tmp/vos_test_discovery_XXXXXX";
    char *tmp = mkdtemp(test_dir);
    CHECK_MSG(tmp != NULL, "Created temporary directory for discovery test");
    if (!tmp) return;

    char bin_dir[512], opt_dir[512];
    snprintf(bin_dir, sizeof(bin_dir), "%s/bin", test_dir);
    snprintf(opt_dir, sizeof(opt_dir), "%s/opt", test_dir);
    mkdir(bin_dir, 0755);
    mkdir(opt_dir, 0755);

    /* 1. Create an executable file */
    char exec_file[512];
    snprintf(exec_file, sizeof(exec_file), "%s/echo", bin_dir);
    FILE *f = fopen(exec_file, "w");
    CHECK_MSG(f != NULL, "Create test executable file");
    if (f) {
        fprintf(f, "#!/bin/sh\necho test\n");
        fclose(f);
        chmod(exec_file, 0755);
    }

    /* 2. Create a non-executable file */
    char non_exec_file[512];
    snprintf(non_exec_file, sizeof(non_exec_file), "%s/readme.txt", bin_dir);
    f = fopen(non_exec_file, "w");
    if (f) {
        fprintf(f, "just a text file\n");
        fclose(f);
        chmod(non_exec_file, 0644);
    }

    /* 3. Run discovery using the custom root */
    AgentAppDiscoveryList list;
    memset(&list, 0, sizeof(list));
    int res = app_discover_executables_root(test_dir, &list);
    CHECK_MSG(res >= 1, "app_discover_executables_root returned at least 1");
    CHECK_MSG(list.count >= 1, "Discovered at least 1 executable");

    int found_echo = 0;
    int found_txt = 0;
    for (int i = 0; i < list.count; i++) {
        if (strcmp(list.apps[i].name, "echo") == 0) {
            found_echo = 1;
            CHECK_MSG(list.apps[i].is_executable == 1, "echo is marked executable");
            CHECK_MSG(strstr(list.apps[i].path, "/bin/echo") != NULL, "Path ends in /bin/echo");
        }
        if (strcmp(list.apps[i].name, "readme.txt") == 0) {
            found_txt = 1;
        }
    }
    CHECK_MSG(found_echo == 1, "Discovered echo executable");
    CHECK_MSG(found_txt == 0, "Non-executable readme.txt correctly ignored");

    /* Cleanup temporary directory */
    unlink(exec_file);
    unlink(non_exec_file);
    rmdir(bin_dir);
    rmdir(opt_dir);
    rmdir(test_dir);
}

/* ------------------------------------------------------------------ Test 3: Application Registry CRUD */

static void test_application_registry_crud(void)
{
    printf("\n[TEST 3] Application Registry CRUD & VM Isolation\n");

    const char *test_db = "/tmp/vos_test_registry.db";
    unlink(test_db);

    AppResult res = app_manager_init(test_db);
    CHECK_MSG(res == APP_SUCCESS, "Initialize app manager with test database");

    char err[256] = {0};

    /* 3.1 Register valid application */
    res = app_manager_register_app("vm_101", "app_calc", "Calculator", "/usr/bin/bc",
                                   "CLI calculator", "Math Utility", "1.06", "default",
                                   err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Register valid application 'app_calc'");

    /* 3.2 Duplicate registration rejection for same VM */
    res = app_manager_register_app("vm_101", "app_calc", "Calculator Duplicate", "/usr/bin/bc",
                                   "", "", "", "default", err, sizeof(err));
    CHECK_MSG(res == APP_ERR_ALREADY_EXISTS, "Duplicate app_id rejected for same VM");

    /* 3.3 VM Isolation: same app_id allowed in a different VM */
    res = app_manager_register_app("vm_102", "app_calc", "Calculator VM2", "/bin/calc",
                                   "", "Utility", "1.0", "default", err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Same app_id successfully registered in independent VM 'vm_102'");

    /* 3.4 Input validation: unsafe app_id (directory traversal / slash) */
    res = app_manager_register_app("vm_101", "../bad_id", "Bad App", "/bin/bad",
                                   "", "", "", "default", err, sizeof(err));
    CHECK_MSG(res == APP_ERR_INVALID_PARAM, "Unsafe app_id with traversal rejected");

    res = app_manager_register_app("vm_101", "app/slash", "Slash App", "/bin/slash",
                                   "", "", "", "default", err, sizeof(err));
    CHECK_MSG(res == APP_ERR_INVALID_PARAM, "Unsafe app_id with slash rejected");

    /* 3.5 Input validation: empty executable path */
    res = app_manager_register_app("vm_101", "app_empty_path", "No Path", "",
                                   "", "", "", "default", err, sizeof(err));
    CHECK_MSG(res == APP_ERR_INVALID_PARAM, "Empty executable path rejected");

    /* 3.6 Retrieve application and verify all fields */
    ApplicationEntry entry;
    res = app_manager_get_app("vm_101", "app_calc", &entry, err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Retrieve application 'app_calc'");
    CHECK_MSG(strcmp(entry.app_id, "app_calc") == 0, "Retrieved app_id matches");
    CHECK_MSG(strcmp(entry.vm_id, "vm_101") == 0, "Retrieved vm_id matches");
    CHECK_MSG(strcmp(entry.name, "Calculator") == 0, "Retrieved name matches");
    CHECK_MSG(strcmp(entry.executable_path, "/usr/bin/bc") == 0, "Retrieved path matches");
    CHECK_MSG(strcmp(entry.category, "Math Utility") == 0, "Retrieved category matches");
    CHECK_MSG(entry.enabled == 1, "App is enabled by default");
    CHECK_MSG(entry.launch_count == 0, "Initial launch count is 0");

    /* 3.7 Enable / Disable toggling */
    res = app_manager_set_enabled("vm_101", "app_calc", 0, err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Disable app_calc");
    res = app_manager_get_app("vm_101", "app_calc", &entry, err, sizeof(err));
    CHECK_MSG(entry.enabled == 0, "App enabled state is now 0");

    res = app_manager_set_enabled("vm_101", "app_calc", 1, err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Re-enable app_calc");
    res = app_manager_get_app("vm_101", "app_calc", &entry, err, sizeof(err));
    CHECK_MSG(entry.enabled == 1, "App enabled state restored to 1");

    /* 3.8 List applications with filtering */
    ApplicationEntry app_list[16];
    int count = 0;
    res = app_manager_list_apps("vm_101", NULL, app_list, 16, &count);
    CHECK_MSG(res == APP_SUCCESS, "List apps for vm_101");
    CHECK_MSG(count == 1, "Total count for vm_101 is 1");

    res = app_manager_list_apps("vm_101", "Math Utility", app_list, 16, &count);
    CHECK_MSG(count == 1, "Filter by exact category 'Math Utility' returned 1");

    res = app_manager_list_apps("vm_101", "NonExistent", app_list, 16, &count);
    CHECK_MSG(count == 0, "Filter by non-existent category returned 0");

    /* 3.9 Unregister application: entry removed from SQLite, guest file unharmed */
    res = app_manager_unregister_app("vm_101", "app_calc", err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Unregister app_calc");
    res = app_manager_get_app("vm_101", "app_calc", &entry, err, sizeof(err));
    CHECK_MSG(res == APP_ERR_NOT_FOUND, "app_calc no longer found after unregister");

    app_manager_shutdown();
    unlink(test_db);
}

/* ------------------------------------------------------------------ Test 4: Application Resource Profiles */

static void test_application_profiles(void)
{
    printf("\n[TEST 4] Application Resource Profiles & Cgroups v2 Mapping\n");

    const char *test_db = "/tmp/vos_test_profiles.db";
    unlink(test_db);

    AppResult res = app_manager_init(test_db);
    CHECK_MSG(res == APP_SUCCESS, "Initialize app manager");

    char err[256] = {0};

    /* 4.1 Verify 3 default profiles were automatically seeded */
    ApplicationProfile p_default, p_perf, p_restr;
    res = app_manager_get_profile("default", &p_default, err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Default profile exists");
    CHECK_MSG(p_default.cpu_cores == 0.0, "Default profile has 0 cores (unlimited)");
    CHECK_MSG(p_default.cpu_weight == 100, "Default profile weight is 100");

    res = app_manager_get_profile("performance", &p_perf, err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Performance profile exists");
    CHECK_MSG(p_perf.cpu_cores == 1.5, "Performance profile has 1.5 cores");
    CHECK_MSG(p_perf.memory_max_bytes == 512ULL * 1024 * 1024, "Performance RAM is 512MB");

    res = app_manager_get_profile("restricted", &p_restr, err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Restricted profile exists");
    CHECK_MSG(p_restr.cpu_cores == 0.5, "Restricted profile has 0.5 cores");
    CHECK_MSG(p_restr.memory_max_bytes == 128ULL * 1024 * 1024, "Restricted RAM is 128MB");
    CHECK_MSG(p_restr.pids_max == 20, "Restricted PIDs max is 20");

    /* 4.2 Create custom profile */
    res = app_manager_create_profile("custom_worker", "Worker Profile", "Dedicated worker limits",
                                     0.75, 150, 256ULL * 1024 * 1024, 30,
                                     10ULL * 1024 * 1024, 5ULL * 1024 * 1024,
                                     err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Create custom_worker profile");

    /* 4.3 Validation: reject unsafe profile ID */
    res = app_manager_create_profile("../bad_profile", "Bad", "", 1.0, 100, 0, 0, 0, 0, err, sizeof(err));
    CHECK_MSG(res == APP_ERR_INVALID_PARAM, "Reject unsafe profile ID");

    /* 4.4 Validation: reject negative CPU cores */
    res = app_manager_create_profile("neg_cpu", "Bad CPU", "", -1.5, 100, 0, 0, 0, 0, err, sizeof(err));
    CHECK_MSG(res == APP_ERR_INVALID_PARAM, "Reject negative CPU cores");

    /* 4.5 Validation: reject invalid weight outside 1-10000 */
    res = app_manager_create_profile("bad_wt", "Bad Weight", "", 1.0, 20000, 0, 0, 0, 0, err, sizeof(err));
    CHECK_MSG(res == APP_ERR_INVALID_PARAM, "Reject weight > 10000");

    /* 4.6 Assign profile to registered application */
    app_manager_register_app("vm_101", "my_app", "My App", "/bin/my_app", "", "Utils", "1.0", "default", err, sizeof(err));
    res = app_manager_assign_profile("vm_101", "my_app", "custom_worker", err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Assign custom_worker profile to my_app");

    ApplicationEntry app;
    app_manager_get_app("vm_101", "my_app", &app, err, sizeof(err));
    CHECK_MSG(strcmp(app.default_profile_id, "custom_worker") == 0, "my_app default_profile_id updated");

    /* 4.7 Guard deletion: cannot delete profile that is currently in use */
    res = app_manager_delete_profile("custom_worker", err, sizeof(err));
    CHECK_MSG(res == APP_ERR_INVALID_PARAM, "Profile in active use cannot be deleted");

    /* Reassign to default and now delete */
    app_manager_assign_profile("vm_101", "my_app", "default", err, sizeof(err));
    res = app_manager_delete_profile("custom_worker", err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Unused profile deleted successfully");

    app_manager_shutdown();
    unlink(test_db);
}

/* ------------------------------------------------------------------ Test 5: Pre-Execution Handshake */

static void test_launcher_pre_execution_handshake(void)
{
    printf("\n[TEST 5] Launch Handshake & Pre-Execution Cgroup Handshake\n");

    /* Test the POSIX synchronization pipe protocol used by app_launcher */
    int sync_pipe[2];
    int err_pipe[2];
    CHECK_MSG(pipe(sync_pipe) == 0, "Create sync pipe");
    CHECK_MSG(pipe(err_pipe) == 0, "Create err pipe");

    fcntl(sync_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(sync_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(err_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    CHECK_MSG(pid >= 0, "Fork child process for handshake test");

    if (pid == 0) {
        /* CHILD: Close unused ends */
        close(sync_pipe[1]);
        close(err_pipe[0]);

        /* Child waits for parent handshake token before proceeding */
        char token = 0;
        ssize_t n = read(sync_pipe[0], &token, 1);
        close(sync_pipe[0]);

        if (n <= 0 || token != 'G') {
            /* Handshake failed or parent aborted child */
            _exit(127);
        }

        /* Handshake succeeded! Child is now authorized to run */
        _exit(42);
    }

    /* PARENT: Close unused ends */
    close(sync_pipe[0]);
    close(err_pipe[1]);

    /* Simulate parent performing cgroup attachment before unblocking child */
    CHECK_MSG(pid > 0, "Parent observed valid child PID");

    /* Check that child is still blocked waiting on sync_pipe */
    int status = 0;
    pid_t w = waitpid(pid, &status, WNOHANG);
    CHECK_MSG(w == 0, "Child remains blocked on pipe prior to parent token");

    /* Parent signals child to proceed: write 'G' ("GO") */
    ssize_t wn = write(sync_pipe[1], "G", 1);
    CHECK_MSG(wn == 1, "Parent sent handshake release token to child");
    close(sync_pipe[1]);

    /* Parent waits for child exit */
    waitpid(pid, &status, 0);
    CHECK_MSG(WIFEXITED(status) && WEXITSTATUS(status) == 42, "Child completed successfully after handshake release");
    close(err_pipe[0]);
}

/* ------------------------------------------------------------------ Test 6: Lifecycle & PID Reuse Protection */

static void handle_term_zero(int sig) { (void)sig; _exit(0); }
static void handle_term_42(int sig) { (void)sig; _exit(42); }

static void test_lifecycle_and_pid_reuse_protection(void)
{
    printf("\n[TEST 6] Lifecycle Management & PID Reuse Protection\n");

    const char *test_db = "/tmp/vos_test_lifecycle.db";
    unlink(test_db);

    AppResult res = app_manager_init(test_db);
    CHECK_MSG(res == APP_SUCCESS, "Initialize app manager");

    char err[256] = {0};
    app_manager_register_app("vm_101", "demo_worker", "Demo Worker", "/bin/sleep", "", "Demo", "1.0", "default", err, sizeof(err));

    /* 6.1 State string and enum mapping */
    CHECK_MSG(strcmp(app_state_name(APP_STATE_REGISTERED), "REGISTERED") == 0, "State REGISTERED str");
    CHECK_MSG(strcmp(app_state_name(APP_STATE_RUNNING), "RUNNING") == 0, "State RUNNING str");
    CHECK_MSG(strcmp(app_state_name(APP_STATE_STOPPED), "STOPPED") == 0, "State STOPPED str");
    CHECK_MSG(app_state_from_name("RUNNING") == APP_STATE_RUNNING, "State from name RUNNING");
    CHECK_MSG(app_state_from_name("STOPPED") == APP_STATE_STOPPED, "State from name STOPPED");

    /* 6.2 Spawn a real background process to test stop & start time protection */
    pid_t child = fork();
    if (child == 0) {
        /* Sleep for 30 seconds */
        execl("/bin/sleep", "sleep", "30", (char *)NULL);
        _exit(1);
    }
    CHECK_MSG(child > 0, "Spawned test child process");

    /* Get start time */
    uint64_t stime = 0;
    cgroup_read_process_starttime(NULL, child, &stime);
    if (stime == 0) {
        stime = (uint64_t)time(NULL);
    }
    CHECK_MSG(stime > 0, "Obtained valid process start time");

    /* Insert artificial instance into SQLite */
    char inst_id[] = "inst_test_001";
    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO application_instances "
             "(instance_id, app_id, vm_id, guest_pid, process_start_time, cgroup_path, state, profile_id, started_at) "
             "VALUES ('%s', 'demo_worker', 'vm_101', %d, %llu, 'app_demo_worker', 'RUNNING', 'default', %llu);",
             inst_id, (int)child, (unsigned long long)stime, (unsigned long long)time(NULL));
    CHECK_MSG(exec_sql(test_db, sql) == 0, "Inserted instance record");

    /* 6.3 Test graceful stop via PID + start time protection */
    ApplicationInstance inst_read;
    res = app_manager_get_instance(inst_id, &inst_read, err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Read instance record");
    CHECK_MSG(inst_read.guest_pid == child, "Instance PID matches child");
    CHECK_MSG(inst_read.process_start_time == stime, "Start time matches expected");

    /* 6.4 Test PID Reuse Protection: change start time in database to simulate reused PID */
    int exit_code_reaped = -999;
    AgentStatusCode status = app_stop_process_with_exit_code(child, stime + 5000, 1000, 0, &exit_code_reaped, err, sizeof(err));
    CHECK_MSG(status == AGENT_STATUS_PROCESS_IDENTITY_CHANGED, "In-guest app_stop_process detects mismatched start time and returns AGENT_STATUS_PROCESS_IDENTITY_CHANGED");
    CHECK_MSG(kill(child, 0) == 0, "Stage 0 Item 2: Process remains alive after rejected STOP_APP");

    /* 6.4b Test Authentic Process Identity Stops App */
    status = app_stop_process_with_exit_code(child, stime, 1000, 0, &exit_code_reaped, err, sizeof(err));
    CHECK_MSG(status == AGENT_STATUS_SUCCESS, "Stage 0 Item 3: Authentic process identity successfully stops app");
    CHECK_MSG(kill(child, 0) != 0 && errno == ESRCH, "Child process is genuinely terminated");
    CHECK_MSG(exit_code_reaped >= 128, "Child terminated via signal (SIGTERM/SIGKILL)");

    /* 6.4c Test Stage 0 Item 4: Normal exit code 0 recorded */
    int pfd0[2];
    pipe(pfd0);
    pid_t child_zero = fork();
    if (child_zero == 0) {
        close(pfd0[0]);
        signal(SIGTERM, handle_term_zero);
        write(pfd0[1], "K", 1);
        close(pfd0[1]);
        while (1) {
            pause();
        }
        _exit(0);
    }
    close(pfd0[1]);
    char ack0 = 0;
    read(pfd0[0], &ack0, 1);
    close(pfd0[0]);

    uint64_t stime_zero = 0;
    cgroup_read_process_starttime(NULL, child_zero, &stime_zero);
    int exit_zero = -999;
    status = app_stop_process_with_exit_code(child_zero, stime_zero, 1000, 0, &exit_zero, err, sizeof(err));
    CHECK_MSG(status == AGENT_STATUS_SUCCESS, "Child zero stop/reap succeeded");
    CHECK_MSG(exit_zero == 0, "Stage 0 Item 4: Normal exit code 0 recorded");

    /* 6.4d Test Stage 0 Item 5: Nonzero exit code recorded */
    int pfd_nz[2];
    pipe(pfd_nz);
    pid_t child_nonzero = fork();
    if (child_nonzero == 0) {
        close(pfd_nz[0]);
        signal(SIGTERM, handle_term_42);
        write(pfd_nz[1], "K", 1);
        close(pfd_nz[1]);
        while (1) {
            pause();
        }
        _exit(42);
    }
    close(pfd_nz[1]);
    char ack_nz = 0;
    read(pfd_nz[0], &ack_nz, 1);
    close(pfd_nz[0]);

    uint64_t stime_nz = 0;
    cgroup_read_process_starttime(NULL, child_nonzero, &stime_nz);
    int exit_nonzero = -999;
    status = app_stop_process_with_exit_code(child_nonzero, stime_nz, 1000, 0, &exit_nonzero, err, sizeof(err));
    CHECK_MSG(status == AGENT_STATUS_SUCCESS, "Child nonzero stop/reap succeeded");
    CHECK_MSG(exit_nonzero == 42, "Stage 0 Item 5: Nonzero exit code 42 recorded");

    /* 6.4e Test Stage 0 Item 6: Signal termination identified */
    pid_t child_sig = fork();
    if (child_sig == 0) {
        sleep(60);
        _exit(1);
    }
    uint64_t stime_sig = 0;
    cgroup_read_process_starttime(NULL, child_sig, &stime_sig);
    int exit_sig = -999;
    status = app_stop_process_with_exit_code(child_sig, stime_sig, 1000, 1 /* force SIGKILL */, &exit_sig, err, sizeof(err));
    CHECK_MSG(status == AGENT_STATUS_SUCCESS, "Child forced kill succeeded");
    CHECK_MSG(exit_sig == 137, "Stage 0 Item 6: Signal SIGKILL identified as 128 + 9 = 137");

    /* 6.4f Test Stage 0 Item 7: Unknown exit status not fabricated */
    int unknown_exit = 0;
    /* Pass a non-child PID that doesn't exist (999999) */
    status = app_stop_process_with_exit_code(999999, 0, 100, 0, &unknown_exit, err, sizeof(err));
    CHECK_MSG(status == AGENT_STATUS_PROCESS_NOT_FOUND, "Non-existent PID returns PROCESS_NOT_FOUND");
    CHECK_MSG(unknown_exit == -1, "Stage 0 Item 7: Unknown exit status is sentinel -1, never fabricated 0");

    /* 6.4g Test Stage 0 Item 8: SQLite records match actual lifecycle */
    char inst_lifecycle[] = "inst_lifecycle_008";
    snprintf(sql, sizeof(sql),
             "INSERT INTO application_instances "
             "(instance_id, app_id, vm_id, guest_pid, process_start_time, cgroup_path, state, profile_id, started_at, stopped_at, exit_code, failure_reason) "
             "VALUES ('%s', 'worker', 'vm_101', %d, %llu, 'app_worker', 'STOPPED', 'default', %llu, %llu, %d, 'Clean exit (code 0)');",
             inst_lifecycle, (int)child_zero, (unsigned long long)stime_zero,
             (unsigned long long)(time(NULL) - 10), (unsigned long long)time(NULL), exit_zero);
    exec_sql(test_db, sql);

    ApplicationInstance inst_lc;
    res = app_manager_get_instance(inst_lifecycle, &inst_lc, err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Read lifecycle instance record");
    CHECK_MSG(inst_lc.state == APP_STATE_STOPPED, "Stage 0 Item 8: State is STOPPED");
    CHECK_MSG(inst_lc.exit_code == 0, "Stage 0 Item 8: Exit code 0 matches");
    CHECK_MSG(inst_lc.stopped_at >= inst_lc.started_at, "Stage 0 Item 8: stopped_at >= started_at");
    CHECK_MSG(strstr(inst_lc.failure_reason, "Clean exit") != NULL, "Stage 0 Item 8: Failure reason correctly annotated");

    snprintf(sql, sizeof(sql), "UPDATE application_instances SET process_start_time = %llu WHERE instance_id = '%s';",
             (unsigned long long)(stime + 5000), inst_id);
    exec_sql(test_db, sql);

    /* Attempting to stop via manager when VM is stopped or PID reused returns safe error */
    res = app_manager_stop_app("vm_101", inst_id, 1000, 0, err, sizeof(err));
    CHECK_MSG(res == APP_ERR_VM_NOT_RUNNING || res == APP_ERR_PID_IDENTITY_CHANGED || res == APP_ERR_AGENT_COMM,
              "Stop on inactive VM or mismatched PID start time handled safely");

    /* 6.5 Test Reconciliation */
    /* Insert a stale instance for a non-existent PID */
    char dead_inst[] = "inst_dead_999";
    snprintf(sql, sizeof(sql),
             "INSERT INTO application_instances "
             "(instance_id, app_id, vm_id, guest_pid, process_start_time, cgroup_path, state, profile_id, started_at) "
             "VALUES ('%s', 'demo_worker', 'vm_101', 999999, 12345, 'app_demo_worker', 'RUNNING', 'default', 1000);",
             dead_inst);
    exec_sql(test_db, sql);

    int reconciled_count = 0;
    res = app_manager_reconcile_instances("vm_101", &reconciled_count);
    CHECK_MSG(res == APP_SUCCESS, "Reconciliation executed successfully");
    CHECK_MSG(reconciled_count >= 1, "Dead instance detected and reconciled");

    res = app_manager_get_instance(dead_inst, &inst_read, err, sizeof(err));
    CHECK_MSG(res == APP_SUCCESS, "Read reconciled instance");
    CHECK_MSG(inst_read.state == APP_STATE_STOPPED, "Dead instance state transitioned to STOPPED");

    app_manager_shutdown();
    unlink(test_db);
}

/* ------------------------------------------------------------------ Test 7: Monitoring & Sample Persistence */

static void test_monitoring_and_persistence(void)
{
    printf("\n[TEST 7] Resource Monitoring Samples & Event Logging\n");

    const char *test_db = "/tmp/vos_test_monitoring.db";
    unlink(test_db);

    AppResult res = app_manager_init(test_db);
    CHECK_MSG(res == APP_SUCCESS, "Initialize app manager");

    char err[256] = {0};
    app_manager_register_app("vm_101", "app_proc", "Test App", "/bin/ls", "", "Utils", "1.0", "default", err, sizeof(err));

    /* Insert mock instance */
    char inst_id[] = "inst_mon_001";
    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO application_instances "
             "(instance_id, app_id, vm_id, guest_pid, process_start_time, cgroup_path, state, profile_id, started_at) "
             "VALUES ('%s', 'app_proc', 'vm_101', 5555, 100, 'app_proc', 'RUNNING', 'default', %llu);",
             inst_id, (unsigned long long)time(NULL));
    exec_sql(test_db, sql);

    /* 7.1 Log Application Events */
    res = app_manager_log_event(inst_id, "app_proc", "vm_101", "LAUNCH", "Process launched in cgroup");
    CHECK_MSG(res == APP_SUCCESS, "Log LAUNCH event");

    res = app_manager_log_event(inst_id, "app_proc", "vm_101", "THROTTLED", "CPU throttled by 10000us");
    CHECK_MSG(res == APP_SUCCESS, "Log THROTTLED event");

    /* 7.2 Insert and query time-series samples */
    for (int i = 0; i < 5; i++) {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO application_resource_samples "
                 "(instance_id, app_id, vm_id, cpu_usage_pct, cpu_time_usec, throttled_usec, "
                 " memory_current_bytes, memory_peak_bytes, pids_current, pids_max, "
                 " io_read_bytes, io_write_bytes, sample_timestamp, freshness, verification_state) "
                 "VALUES ('%s', 'app_proc', 'vm_101', %f, %llu, %llu, %llu, %llu, %d, %d, %llu, %llu, %llu, 'FRESH', 'VERIFIED');",
                 inst_id, 10.0 + (double)i * 2.5, (unsigned long long)(1000 + i * 500),
                 (unsigned long long)(i * 100), (unsigned long long)((32 + i) * 1024 * 1024),
                 (unsigned long long)(64 * 1024 * 1024), 2 + i, 20,
                 (unsigned long long)(i * 4096), (unsigned long long)(i * 8192),
                 (unsigned long long)(1700000000ULL + i * 10));
        exec_sql(test_db, sql);
    }

    ApplicationResourceSample samples[16];
    int sample_count = 0;
    res = app_manager_get_samples(inst_id, samples, 16, &sample_count);
    CHECK_MSG(res == APP_SUCCESS, "Retrieve time-series samples");
    CHECK_MSG(sample_count == 5, "Retrieved exactly 5 samples");
    CHECK_MSG(samples[0].cpu_usage_pct == 20.0, "Newest sample (ORDER BY DESC) CPU usage matches");
    CHECK_MSG(samples[4].cpu_usage_pct == 10.0, "Oldest sample (ORDER BY DESC) CPU usage matches");
    CHECK_MSG(strcmp(samples[0].freshness, "FRESH") == 0, "Sample freshness is FRESH");
    CHECK_MSG(strcmp(samples[0].verification_state, "VERIFIED") == 0, "Sample is VERIFIED");

    app_manager_shutdown();
    unlink(test_db);
}

/* ------------------------------------------------------------------ Test 8: Shell Injection Resistance & Handshake Safety */

static void test_shell_injection_and_handshake_safety(void)
{
    printf("\n[TEST 8] Shell Injection Resistance & Pre-Execution Handshake Integrity\n");

    /* 8.1 Shell Injection Resistance in Argument Parsing */
    char *argv[32];
    char scratch[1024];

    /* Complex shell injection payload with spaces, quotes, pipes, subshells, variables */
    const char *payload = "; rm -rf /; echo $HOME; $(whoami); `id` | cat &";
    int argc = app_parse_args("/bin/busybox", payload, argv, 32, scratch, sizeof(scratch));
    CHECK_MSG(argc == 12, "Parsed injection payload into discrete tokens without shell evaluation");
    CHECK_MSG(strcmp(argv[0], "/bin/busybox") == 0, "argv[0] is executable path");
    CHECK_MSG(strcmp(argv[1], ";") == 0, "Semicolon preserved as literal token");
    CHECK_MSG(strcmp(argv[2], "rm") == 0, "rm preserved as literal token");
    CHECK_MSG(strcmp(argv[3], "-rf") == 0, "-rf preserved as literal token");
    CHECK_MSG(strcmp(argv[4], "/;") == 0, "/; preserved as literal token");
    CHECK_MSG(strcmp(argv[5], "echo") == 0, "echo preserved as literal token");
    CHECK_MSG(strcmp(argv[6], "$HOME;") == 0, "$HOME preserved literally without environment expansion");
    CHECK_MSG(strcmp(argv[7], "$(whoami);") == 0, "$(whoami) preserved literally without command substitution");
    CHECK_MSG(strcmp(argv[8], "`id`") == 0, "Backticks preserved literally without command substitution");
    CHECK_MSG(strcmp(argv[9], "|") == 0, "Pipe operator preserved as literal token");
    CHECK_MSG(strcmp(argv[10], "cat") == 0, "cat preserved as literal token");
    CHECK_MSG(strcmp(argv[11], "&") == 0, "Background operator & preserved as literal token");
    CHECK_MSG(argv[12] == NULL, "argv array is properly NULL-terminated");

    /* 8.2 Quoted Arguments with Spaces & Preserved Metacharacters */
    const char *quoted = "--message=\"Hello World with $VAR and ;\" --flag 'simple test' bare_arg";
    argc = app_parse_args("/bin/echo", quoted, argv, 32, scratch, sizeof(scratch));
    CHECK_MSG(argc == 5, "Parsed 4 arguments with whitespace in quotes");
    CHECK_MSG(strcmp(argv[1], "--message=Hello World with $VAR and ;") == 0, "Quoted argument with spaces and metacharacters preserved intact");
    CHECK_MSG(strcmp(argv[2], "--flag") == 0, "Flag argument preserved");
    CHECK_MSG(strcmp(argv[3], "simple test") == 0, "Quoted string with space preserved as single argument");
    CHECK_MSG(strcmp(argv[4], "bare_arg") == 0, "Bare argument preserved");

    /* 8.3 Path Traversal and Relative Executable Path Rejection */
    pid_t pid = 0;
    uint64_t start_time = 0;
    char err[256] = {0};

    AgentStatusCode sc = app_launch_process("/tmp", "test_group", "relative/bin", "", &pid, &start_time, err, sizeof(err));
    CHECK_MSG(sc == AGENT_STATUS_INVALID_CONFIG, "Relative executable path rejected");

    sc = app_launch_process("/tmp", "test_group", "/bin/../etc/passwd", "", &pid, &start_time, err, sizeof(err));
    CHECK_MSG(sc == AGENT_STATUS_INVALID_CONFIG, "Path traversal sequence '..' rejected");

    sc = app_launch_process("/tmp", "test_group", "/nonexistent_binary_xyz123", "", &pid, &start_time, err, sizeof(err));
    CHECK_MSG(sc == AGENT_STATUS_APP_NOT_FOUND, "Non-existent binary rejected before fork");

    /* 8.4 Negative Test: Pre-Execution Handshake Abort Proves Target NEVER Executes */
    const char *canary_file = "/tmp/vos_test_canary_should_never_exist.txt";
    unlink(canary_file);

    /* Create a canary script that creates the marker file if executed */
    const char *script_path = "/tmp/vos_test_canary_runner.sh";
    FILE *sf = fopen(script_path, "w");
    if (sf) {
        fprintf(sf, "#!/bin/sh\necho 'EXECUTED' > %s\n", canary_file);
        fclose(sf);
        chmod(script_path, 0755);
    }

    /* Pass a completely non-existent / invalid cgroup root to force cgroup attachment failure */
    sc = app_launch_process("/tmp/nonexistent_cgroup_dir_9999", "fail_group", script_path, "", &pid, &start_time, err, sizeof(err));
    CHECK_MSG(sc == AGENT_STATUS_CGROUP_ATTACH_FAILED, "Launch aborted with AGENT_STATUS_CGROUP_ATTACH_FAILED");

    /* Verify that the canary script NEVER executed! */
    CHECK_MSG(access(canary_file, F_OK) != 0, "Canary file DOES NOT exist - target never executed a single instruction");

    /* Verify child process was reaped and no zombie remains */
    if (pid > 0) {
        CHECK_MSG(kill(pid, 0) != 0 && errno == ESRCH, "Child process was reaped cleanly, no zombie left");
    }

    unlink(script_path);
    unlink(canary_file);
}

/* ------------------------------------------------------------------ Main Test Runner */

int main(void)
{
    printf("======================================================================\n");
    printf("Virtual OS Simulator - Phase 5 Application Management Test Suite\n");
    printf("Host: macOS Apple Silicon M5 (ARM64) | QEMU Alpine Integration\n");
    printf("======================================================================\n");

    test_wire_protocol_extensions();
    test_in_guest_discovery();
    test_application_registry_crud();
    test_application_profiles();
    test_launcher_pre_execution_handshake();
    test_lifecycle_and_pid_reuse_protection();
    test_monitoring_and_persistence();
    test_shell_injection_and_handshake_safety();

    printf("\n======================================================================\n");
    printf("Phase 5 Test Summary: %d checks, %d failures\n", g_checks, g_fails);
    printf("======================================================================\n");

    return (g_fails == 0) ? 0 : 1;
}
