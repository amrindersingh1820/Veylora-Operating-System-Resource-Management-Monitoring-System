/* app_launcher.c – In-Guest Application Management Implementation
 *
 * Implements:
 *   - Safe filesystem discovery of guest executables.
 *   - Race-free cgroup launch handshake using synchronization pipes.
 *   - PID reuse-protected process termination.
 *   - Application resource metrics readback.
 */

#include "app_launcher.h"
#include "cgroup_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

/* Safe discovery directories */
static const char *s_search_dirs[] = {
    "/bin",
    "/usr/bin",
    "/usr/local/bin",
    "/opt",
    NULL
};

/* Package mapping helper for Alpine Linux APK */
static void detect_apk_package(const char *path, char *out_pkg, size_t out_sz)
{
    if (!out_pkg || out_sz == 0) return;
    out_pkg[0] = '\0';

    /* Only attempt if apk database exists */
    struct stat st;
    if (stat("/lib/apk/db/installed", &st) != 0) {
        return;
    }

    /* Common known Alpine core packages */
    if (strncmp(path, "/bin/busybox", 12) == 0 ||
        strncmp(path, "/bin/sh", 7) == 0 ||
        strncmp(path, "/bin/ls", 7) == 0 ||
        strncmp(path, "/bin/cat", 8) == 0 ||
        strncmp(path, "/bin/sleep", 10) == 0) {
        snprintf(out_pkg, out_sz, "busybox");
        return;
    }
    if (strstr(path, "python3")) {
        snprintf(out_pkg, out_sz, "python3");
        return;
    }
    if (strstr(path, "htop")) {
        snprintf(out_pkg, out_sz, "htop");
        return;
    }
}

int app_discover_executables_root(const char *root_prefix, AgentAppDiscoveryList *out_list)
{
    if (!out_list) return -1;
    out_list->count = 0;

    for (int d = 0; s_search_dirs[d] != NULL; d++) {
        char dir_path[512];
        if (root_prefix && root_prefix[0]) {
            snprintf(dir_path, sizeof(dir_path), "%s%s", root_prefix, s_search_dirs[d]);
        } else {
            snprintf(dir_path, sizeof(dir_path), "%s", s_search_dirs[d]);
        }

        DIR *dir = opendir(dir_path);
        if (!dir) continue;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue; /* Skip hidden, ., .. */

            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

            struct stat st;
            if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
                if (access(full_path, X_OK) == 0) {
                    if (out_list->count < AGENT_MAX_DISCOVERED_APPS) {
                        AgentDiscoveredApp *item = &out_list->apps[out_list->count];
                        snprintf(item->name, sizeof(item->name), "%s", entry->d_name);
                        snprintf(item->path, sizeof(item->path), "%s", full_path);
                        item->is_executable = 1;
                        item->mode = (uint32_t)st.st_mode;
                        detect_apk_package(full_path, item->package, sizeof(item->package));
                        out_list->count++;
                    }
                }
            }

            if (out_list->count >= AGENT_MAX_DISCOVERED_APPS) break;
        }
        closedir(dir);
        if (out_list->count >= AGENT_MAX_DISCOVERED_APPS) break;
    }

    return out_list->count;
}

int app_discover_executables(AgentAppDiscoveryList *out_list)
{
    return app_discover_executables_root(NULL, out_list);
}

/* Parse argument string into argv array without invoking a shell.
 * Treats all metacharacters (;, $, |, &, <, >, *, etc.) as literal argument bytes.
 * Handles single quotes and double quotes with whitespace preservation and quote stripping. */
int app_parse_args(const char *executable_path, const char *args_str,
                   char **argv, int max_args, char *scratch, size_t scratch_sz)
{
    if (!executable_path || !argv || max_args < 2 || !scratch || scratch_sz == 0) return -1;

    argv[0] = (char *)executable_path;
    int argc = 1;

    if (!args_str || !args_str[0]) {
        argv[argc] = NULL;
        return argc;
    }

    size_t in_len = strlen(args_str);
    if (in_len >= scratch_sz) return -1;

    const char *r = args_str;
    char *w = scratch;
    char *w_end = scratch + scratch_sz - 1;

    while (*r && argc < max_args - 1) {
        /* Skip leading whitespace between tokens */
        while (*r && isspace((unsigned char)*r)) r++;
        if (!*r) break;

        argv[argc++] = w;
        char in_quote = 0;

        while (*r && w < w_end) {
            if (in_quote) {
                if (*r == in_quote) {
                    in_quote = 0;
                    r++;
                } else if (*r == '\\' && in_quote == '"' && (*(r + 1) == '"' || *(r + 1) == '\\')) {
                    r++;
                    *w++ = *r++;
                } else {
                    *w++ = *r++;
                }
            } else {
                if (isspace((unsigned char)*r)) {
                    r++;
                    break;
                } else if (*r == '\'' || *r == '"') {
                    in_quote = *r++;
                } else if (*r == '\\' && *(r + 1) != '\0') {
                    r++;
                    *w++ = *r++;
                } else {
                    *w++ = *r++;
                }
            }
        }
        *w++ = '\0';
    }
    argv[argc] = NULL;
    return argc;
}

static int parse_args_safe(const char *executable_path, const char *args_str,
                           char **argv, int max_args, char *scratch, size_t scratch_sz)
{
    return app_parse_args(executable_path, args_str, argv, max_args, scratch, scratch_sz);
}

AgentStatusCode app_launch_process(const char *cgroup_root,
                                   const char *group_id,
                                   const char *executable_path,
                                   const char *args_str,
                                   pid_t *out_pid,
                                   uint64_t *out_start_time,
                                   char *err_buf, size_t err_sz)
{
    if (err_buf && err_sz > 0) err_buf[0] = '\0';
    if (!out_pid || !out_start_time) return AGENT_STATUS_INVALID_CONFIG;
    *out_pid = 0;
    *out_start_time = 0;

    /* 1. Validate executable path */
    if (!executable_path || executable_path[0] != '/') {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Executable path must be absolute");
        return AGENT_STATUS_INVALID_CONFIG;
    }
    if (strstr(executable_path, "..")) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Path traversal sequence rejected");
        return AGENT_STATUS_INVALID_CONFIG;
    }

    struct stat st;
    if (stat(executable_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Executable not found: %s", executable_path);
        return AGENT_STATUS_APP_NOT_FOUND;
    }
    if (access(executable_path, X_OK) != 0) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Permission denied (not executable): %s", executable_path);
        return AGENT_STATUS_PERMISSION_DENIED;
    }

    /* 2. Validate group_id and ensure cgroup exists */
    if (!group_id || !cgroup_is_safe_name(group_id)) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Invalid resource group identifier");
        return AGENT_STATUS_INVALID_CONFIG;
    }

    /* Ensure group exists */
    cgroup_create_group(cgroup_root, group_id, err_buf, err_sz);

    /* 3. Prepare arguments */
    char *argv[32];
    char scratch[1024];
    if (parse_args_safe(executable_path, args_str, argv, 32, scratch, sizeof(scratch)) < 0) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Failed to parse application arguments");
        return AGENT_STATUS_INVALID_CONFIG;
    }

    /* 4. Setup synchronization pipes for the launch handshake */
    int sync_pipe[2];
    if (pipe(sync_pipe) != 0) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "pipe() failed: %s", strerror(errno));
        return AGENT_STATUS_FAILED;
    }

    int err_pipe[2];
    if (pipe(err_pipe) != 0) {
        close(sync_pipe[0]); close(sync_pipe[1]);
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "pipe() failed: %s", strerror(errno));
        return AGENT_STATUS_FAILED;
    }

    /* Close-on-exec for error pipe and sync write pipe */
    fcntl(sync_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(err_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC);

    pid_t child_pid = fork();
    if (child_pid < 0) {
        close(sync_pipe[0]); close(sync_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "fork() failed: %s", strerror(errno));
        return AGENT_STATUS_FAILED;
    }

    if (child_pid == 0) {
        /* ---------------- Child Process ---------------- */
        close(sync_pipe[1]);
        close(err_pipe[0]);

        /* Handshake Step 2: Block child before execve() until cgroup assignment is verified */
        char sync_val = 0;
        ssize_t rd = read(sync_pipe[0], &sync_val, 1);
        close(sync_pipe[0]);

        if (rd <= 0 || sync_val != 'G') {
            /* Handshake aborted by parent */
            _exit(125);
        }

        /* Handshake Step 6: Unblocked! Execute target application with direct execve() */
        char *default_envp[] = {
            "PATH=/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin",
            "HOME=/root",
            "TERM=linux",
            "USER=root",
            "LOGNAME=root",
            NULL
        };
        execve(executable_path, argv, default_envp);

        /* Reaching here means execve failed */
        int err_code = errno;
        ssize_t ignored = write(err_pipe[1], &err_code, sizeof(err_code));
        (void)ignored;
        close(err_pipe[1]);
        _exit(127);
    }

    /* ---------------- Parent Process ---------------- */
    close(sync_pipe[0]);
    close(err_pipe[1]);

    /* Handshake Step 3: Read stationary child's PID and start time from procfs */
    uint64_t start_time = 0;
    if (cgroup_read_process_starttime(cgroup_root, child_pid, &start_time) != 0) {
        /* Procfs read failed or child exited prematurely */
        close(sync_pipe[1]);
        close(err_pipe[0]);
        kill(child_pid, SIGKILL);
        waitpid(child_pid, NULL, 0);
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Failed to read child process start time");
        return AGENT_STATUS_LAUNCH_FAILED;
    }

    /* Handshake Step 4 & 5: Attach child to designated cgroup and verify readback */
    AgentStatusCode att_sc = cgroup_attach_process_with_identity(cgroup_root, group_id,
                                                                 child_pid, start_time,
                                                                 err_buf, err_sz);
    if (att_sc != AGENT_STATUS_SUCCESS) {
        /* Cgroup assignment failed: strictly abort before child ever begins execution */
        close(sync_pipe[1]);
        close(err_pipe[0]);
        kill(child_pid, SIGKILL);
        waitpid(child_pid, NULL, 0);
        if (err_buf && err_sz > 0 && !err_buf[0]) {
            snprintf(err_buf, err_sz, "Cgroup assignment failed for PID %d in group '%s'", (int)child_pid, group_id);
        }
        return AGENT_STATUS_CGROUP_ATTACH_FAILED;
    }

    /* Handshake Step 6: Release the blocked child to execute target */
    ssize_t wr = write(sync_pipe[1], "G", 1);
    (void)wr;
    close(sync_pipe[1]);

    /* Handshake Step 7: Check if child succeeded execv or reported error */
    int child_exec_err = 0;
    ssize_t err_read = read(err_pipe[0], &child_exec_err, sizeof(child_exec_err));
    close(err_pipe[0]);

    if (err_read > 0) {
        /* Child reported exec failure */
        waitpid(child_pid, NULL, 0);
        if (err_buf && err_sz > 0) {
            snprintf(err_buf, err_sz, "Failed to execute '%s': %s (errno %d)",
                     executable_path, strerror(child_exec_err), child_exec_err);
        }
        return AGENT_STATUS_LAUNCH_FAILED;
    }

    /* Handshake Step 8: Success! Return PID and start time to host */
    *out_pid = child_pid;
    *out_start_time = start_time;
    return AGENT_STATUS_SUCCESS;
}

AgentStatusCode app_stop_process_with_exit_code(pid_t pid,
                                                 uint64_t expected_start_time,
                                                 int timeout_ms,
                                                 int force,
                                                 int *out_exit_code,
                                                 char *err_buf, size_t err_sz)
{
    if (err_buf && err_sz > 0) err_buf[0] = '\0';
    if (out_exit_code) *out_exit_code = -1; /* Sentinel: unknown exit code, not fabricated */

    /* System init and kernel task protection */
    if (pid <= 1) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Refusing to terminate system init / PID %d", (int)pid);
        return AGENT_STATUS_PERMISSION_DENIED;
    }

    /* 0. Check if child already exited and can be reaped */
    int pre_status = 0;
    pid_t pre_wr = waitpid(pid, &pre_status, WNOHANG);
    if (pre_wr == pid) {
        if (out_exit_code) {
            if (WIFEXITED(pre_status)) {
                *out_exit_code = WEXITSTATUS(pre_status);
            } else if (WIFSIGNALED(pre_status)) {
                *out_exit_code = 128 + WTERMSIG(pre_status);
            }
        }
        return AGENT_STATUS_SUCCESS;
    }

    /* 1. PID existence check */
    if (kill(pid, 0) != 0 && errno == ESRCH) {
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "PID %d not found (already stopped)", (int)pid);
        return AGENT_STATUS_PROCESS_NOT_FOUND;
    }

    /* 2. PID reuse protection: verify start time against procfs */
    uint64_t current_starttime = 0;
    if (cgroup_read_process_starttime(NULL, pid, &current_starttime) == 0) {
        if (expected_start_time != 0 && current_starttime != expected_start_time) {
            if (err_buf && err_sz > 0) {
                snprintf(err_buf, err_sz, "PID %d identity changed: expected starttime %llu, found %llu (PID was reused)",
                         (int)pid, (unsigned long long)expected_start_time, (unsigned long long)current_starttime);
            }
            return AGENT_STATUS_PROCESS_IDENTITY_CHANGED;
        }
    }

    /* 3. Graceful termination with SIGTERM */
    if (!force) {
        kill(pid, SIGTERM);

        int waited_ms = 0;
        int step_ms = 50;
        int max_wait = (timeout_ms > 0) ? timeout_ms : 3000;

        while (waited_ms < max_wait) {
            int wstatus = 0;
            pid_t wr = waitpid(pid, &wstatus, WNOHANG);
            if (wr == pid) {
                if (out_exit_code) {
                    if (WIFEXITED(wstatus)) {
                        *out_exit_code = WEXITSTATUS(wstatus);
                    } else if (WIFSIGNALED(wstatus)) {
                        *out_exit_code = 128 + WTERMSIG(wstatus);
                    }
                }
                return AGENT_STATUS_SUCCESS;
            } else if (wr == -1 && errno == ECHILD) {
                /* Not our direct child process: verify via kill(pid, 0) */
                if (kill(pid, 0) != 0 && errno == ESRCH) {
                    if (out_exit_code) *out_exit_code = -1; /* Unknown status, never fabricated */
                    return AGENT_STATUS_SUCCESS;
                }
            }

            usleep((useconds_t)step_ms * 1000);
            waited_ms += step_ms;
        }
    }

    /* 4. Forced termination with SIGKILL if still running */
    kill(pid, SIGKILL);

    int post_kill_waited = 0;
    while (post_kill_waited < 1000) {
        int wstatus = 0;
        pid_t wr = waitpid(pid, &wstatus, WNOHANG);
        if (wr == pid) {
            if (out_exit_code) {
                if (WIFEXITED(wstatus)) {
                    *out_exit_code = WEXITSTATUS(wstatus);
                } else if (WIFSIGNALED(wstatus)) {
                    *out_exit_code = 128 + WTERMSIG(wstatus);
                }
            }
            return AGENT_STATUS_SUCCESS;
        } else if (wr == -1 && errno == ECHILD) {
            if (kill(pid, 0) != 0 && errno == ESRCH) {
                if (out_exit_code) *out_exit_code = -1;
                return AGENT_STATUS_SUCCESS;
            }
        }

        usleep(50000);
        post_kill_waited += 50;
    }

    return AGENT_STATUS_SUCCESS;
}

AgentStatusCode app_stop_process(pid_t pid,
                                 uint64_t expected_start_time,
                                 int timeout_ms,
                                 int force,
                                 char *err_buf, size_t err_sz)
{
    int dummy_exit_code = -1;
    return app_stop_process_with_exit_code(pid, expected_start_time, timeout_ms, force,
                                          &dummy_exit_code, err_buf, err_sz);
}

AgentStatusCode app_get_metrics(const char *cgroup_root,
                                const char *group_id,
                                pid_t pid,
                                AgentCgroupStatus *out_status,
                                char *err_buf, size_t err_sz)
{
    if (err_buf && err_sz > 0) err_buf[0] = '\0';
    if (!out_status) return AGENT_STATUS_INVALID_CONFIG;

    /* Read cgroup status */
    AgentStatusCode sc = cgroup_get_status(cgroup_root, group_id, out_status, err_buf, err_sz);
    if (sc != AGENT_STATUS_SUCCESS) {
        return sc;
    }

    /* Check if target PID is still active */
    if (pid > 0 && kill(pid, 0) != 0 && errno == ESRCH) {
        /* PID terminated */
        if (err_buf && err_sz > 0) snprintf(err_buf, err_sz, "Process PID %d has exited", (int)pid);
    }

    return AGENT_STATUS_SUCCESS;
}
