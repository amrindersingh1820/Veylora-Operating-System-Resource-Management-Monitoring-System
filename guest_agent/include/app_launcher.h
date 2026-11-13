/* app_launcher.h – In-Guest Application Management & Launch Handshake
 *
 * Provides:
 *   - Safe filesystem discovery of guest executables (/bin, /usr/bin, etc.).
 *   - Guaranteed cgroup v2 assignment BEFORE execution (handshake pipe).
 *   - PID reuse-safe process termination.
 *   - Application-level resource metrics readback.
 */

#ifndef VOS_APP_LAUNCHER_H
#define VOS_APP_LAUNCHER_H

#include <sys/types.h>
#include <stdint.h>
#include "agent_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Discover executables in common system locations (/bin, /usr/bin, /usr/local/bin, /opt).
 * Does not recursively traverse the full filesystem. Does not execute binaries. */
int app_discover_executables(AgentAppDiscoveryList *out_list);
int app_discover_executables_root(const char *root_prefix, AgentAppDiscoveryList *out_list);

/* Safe argument parsing without shell interpretation.
 * Populates argv array with pointers into scratch buffer.
 * Preserves quotes, whitespace within quotes, and treats shell metacharacters literally. */
int app_parse_args(const char *executable_path, const char *args_str,
                   char **argv, int max_args, char *scratch, size_t scratch_sz);

/* Safe cgroup launch handshake:
 * 1. Forks child process.
 * 2. Child blocks on synchronization pipe before execve().
 * 3. Parent verifies child PID and start time from /proc/<pid>/stat.
 * 4. Parent attaches child to target cgroup and verifies cgroup.procs readback.
 * 5. Parent releases child to execute the binary.
 * 6. Returns child PID and start time.
 * If cgroup assignment fails, child is killed immediately without executing target. */
AgentStatusCode app_launch_process(const char *cgroup_root,
                                   const char *group_id,
                                   const char *executable_path,
                                   const char *args_str,
                                   pid_t *out_pid,
                                   uint64_t *out_start_time,
                                   char *err_buf, size_t err_sz);

/* Safe process termination with PID reuse protection.
 * Validates PID and expected start time before signaling.
 * Sends SIGTERM, waits up to timeout_ms, and falls back to SIGKILL. */
AgentStatusCode app_stop_process(pid_t pid,
                                 uint64_t expected_start_time,
                                 int timeout_ms,
                                 int force,
                                 char *err_buf, size_t err_sz);

/* Extended process termination returning reaped exit code or signal.
 * *out_exit_code will be:
 *   >= 0 and < 128: normal exit code from WEXITSTATUS (e.g., 0 for success)
 *   >= 128: signal termination 128 + signal (e.g., 143 for SIGTERM, 137 for SIGKILL)
 *   -1: unknown / unreapable child (never fabricated) */
AgentStatusCode app_stop_process_with_exit_code(pid_t pid,
                                                 uint64_t expected_start_time,
                                                 int timeout_ms,
                                                 int force,
                                                 int *out_exit_code,
                                                 char *err_buf, size_t err_sz);

/* Read application resource metrics from cgroups v2 and procfs. */
AgentStatusCode app_get_metrics(const char *cgroup_root,
                                const char *group_id,
                                pid_t pid,
                                AgentCgroupStatus *out_status,
                                char *err_buf, size_t err_sz);

#ifdef __cplusplus
}
#endif

#endif /* VOS_APP_LAUNCHER_H */
