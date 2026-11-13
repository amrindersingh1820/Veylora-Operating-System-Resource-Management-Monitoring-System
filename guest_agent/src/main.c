/* main.c – Virtual OS Simulator Linux Guest Monitoring Agent Entrypoint
 *
 * Lightweight in-guest background agent that samples kernel metrics and sends
 * structured JSON telemetry to the host backend over virtio-serial, TCP, or stdout.
 */

#include "agent.h"
#include "metrics.h"
#include "cgroup_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static volatile sig_atomic_t s_running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    s_running = 0;
}

void agent_config_default(AgentConfig *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->vm_id, sizeof(cfg->vm_id), "linux_guest");
    cfg->interval_ms   = 1000;
    cfg->channel_type  = CHANNEL_VIRTIO_SERIAL;
    snprintf(cfg->channel_path, sizeof(cfg->channel_path), AGENT_DEFAULT_VIRTIO_PORT);
    cfg->tcp_port      = AGENT_DEFAULT_TCP_PORT;
    cfg->oneshot       = false;
    cfg->verbose       = false;

    /* If hostname is available, use it as default vm_id */
    char hname[64];
    if (gethostname(hname, sizeof(hname)) == 0 && hname[0] != '\0') {
        snprintf(cfg->vm_id, sizeof(cfg->vm_id), "%s", hname);
    }
}

int agent_config_parse(AgentConfig *cfg, int argc, char **argv)
{
    if (!cfg) return -1;
    agent_config_default(cfg);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vm-id") == 0 && i + 1 < argc) {
            snprintf(cfg->vm_id, sizeof(cfg->vm_id), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            int iv = atoi(argv[++i]);
            if (iv >= 100) cfg->interval_ms = iv;
        } else if (strcmp(argv[i], "--channel") == 0 && i + 1 < argc) {
            cfg->channel_type = CHANNEL_VIRTIO_SERIAL;
            snprintf(cfg->channel_path, sizeof(cfg->channel_path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--tcp") == 0 && i + 1 < argc) {
            cfg->channel_type = CHANNEL_TCP_SERVER;
            cfg->tcp_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--stdout") == 0) {
            cfg->channel_type = CHANNEL_STDOUT;
        } else if (strcmp(argv[i], "--oneshot") == 0) {
            cfg->oneshot = true;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            cfg->verbose = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Virtual OS Simulator - Guest Monitoring Agent (Linux ARM64)\n");
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("Options:\n");
            printf("  --vm-id <id>       Identifier for this VM (default: hostname)\n");
            printf("  --interval <ms>    Sampling interval in ms (default: 1000)\n");
            printf("  --channel <path>   Virtio-serial device path (default: %s)\n", AGENT_DEFAULT_VIRTIO_PORT);
            printf("  --tcp <port>       Run in TCP server mode on specified port\n");
            printf("  --stdout           Print JSON telemetry to standard output\n");
            printf("  --oneshot          Collect a single sample and exit\n");
            printf("  --verbose, -v      Enable verbose logging\n");
            printf("  --help, -h         Show this help message\n");
            return 1;
        }
    }
    return 0;
}

static uint64_t get_epoch_ms(void)
{
    struct timespec ts;
#if defined(CLOCK_REALTIME)
    clock_gettime(CLOCK_REALTIME, &ts);
#else
    clock_gettime(0, &ts);
#endif
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static void process_incoming_rpc(int fd, const char *vm_id, bool verbose)
{
    static char in_buf[AGENT_MAX_JSON_SZ];
    static size_t in_len = 0;

    ssize_t n = read(fd, in_buf + in_len, sizeof(in_buf) - in_len - 1);
    if (n <= 0) return;
    in_len += (size_t)n;
    in_buf[in_len] = '\0';

    char *line_start = in_buf;
    char *nl = NULL;
    while ((nl = strchr(line_start, '\n')) != NULL) {
        *nl = '\0';
        while (*line_start == ' ' || *line_start == '\r' || *line_start == '\t') line_start++;

        if (strstr(line_start, "\"operation\"") != NULL) {
            AgentResourceRequest req;
            if (agent_rpc_deserialize_request(line_start, &req) == 0) {
                if (verbose) {
                    fprintf(stderr, "[guest_agent] Handling RPC request '%s' (operation %s, group %s)\n",
                            req.request_id, agent_resource_op_name(req.operation), req.group_id);
                }
                AgentResourceResponse resp;
                cgroup_manager_handle_request(NULL, &req, &resp);
                if (resp.vm_id[0] == '\0' && vm_id && vm_id[0]) {
                    snprintf(resp.vm_id, sizeof(resp.vm_id), "%s", vm_id);
                }

                char resp_buf[AGENT_MAX_JSON_SZ];
                int resp_len = agent_rpc_serialize_response(&resp, resp_buf, sizeof(resp_buf) - 2);
                if (resp_len > 0) {
                    resp_buf[resp_len++] = '\n';
                    resp_buf[resp_len] = '\0';
                    write(fd, resp_buf, (size_t)resp_len);
                }
            }
        }
        line_start = nl + 1;
    }

    size_t rem = strlen(line_start);
    if (rem > 0 && line_start != in_buf) {
        memmove(in_buf, line_start, rem);
    }
    in_len = rem;
    in_buf[in_len] = '\0';
}

int agent_run(const AgentConfig *cfg)
{
    if (!cfg) return -1;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
#if defined(SIGPIPE)
    signal(SIGPIPE, SIG_IGN);
#endif

    cpu_monitor_init();
    memory_monitor_init();
    disk_monitor_init();
    network_monitor_init();
    process_monitor_init();
    cgroup_manager_init(NULL);

    int channel_fd = -1;
    int server_sock = -1;

    if (cfg->channel_type == CHANNEL_STDOUT) {
        channel_fd = STDOUT_FILENO;
    } else if (cfg->channel_type == CHANNEL_TCP_SERVER) {
        server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock >= 0) {
            int opt = 1;
            setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            struct sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_family      = AF_INET;
            sin.sin_addr.s_addr = INADDR_ANY;
            sin.sin_port        = htons((uint16_t)cfg->tcp_port);
            if (bind(server_sock, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
                listen(server_sock, 1);
                if (cfg->verbose) {
                    fprintf(stderr, "[guest_agent] Listening for host connection on TCP port %d\n", cfg->tcp_port);
                }
            } else {
                fprintf(stderr, "[guest_agent] Failed to bind TCP port %d: %s\n", cfg->tcp_port, strerror(errno));
                close(server_sock);
                server_sock = -1;
            }
        }
    }

    if (cfg->verbose) {
        fprintf(stderr, "[guest_agent] Starting agent for VM '%s' (interval %d ms)\n",
                cfg->vm_id, cfg->interval_ms);
    }

    uint64_t seq = 1;
    uint64_t last_telemetry_ms = 0;
    static char json_buffer[AGENT_MAX_JSON_SZ];

    while (s_running) {
        /* 1. Ensure channel connection if virtio-serial or TCP */
        if (cfg->channel_type == CHANNEL_VIRTIO_SERIAL && channel_fd < 0) {
            channel_fd = open(cfg->channel_path, O_RDWR | O_NONBLOCK);
        } else if (cfg->channel_type == CHANNEL_TCP_SERVER && server_sock >= 0 && channel_fd < 0) {
            struct sockaddr_in client_addr;
            socklen_t clen = sizeof(client_addr);
            channel_fd = accept(server_sock, (struct sockaddr *)&client_addr, &clen);
            if (channel_fd >= 0) {
                int flags = fcntl(channel_fd, F_GETFL, 0);
                if (flags >= 0) fcntl(channel_fd, F_SETFL, flags | O_NONBLOCK);
            }
        }

        /* 2. Poll for incoming RPC requests with small timeout (50ms) */
        if (channel_fd >= 0) {
            struct pollfd pfd;
            pfd.fd = channel_fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int pr = poll(&pfd, 1, 50);
            if (pr > 0 && (pfd.revents & POLLIN)) {
                process_incoming_rpc(channel_fd, cfg->vm_id, cfg->verbose);
            } else if (pr > 0 && (pfd.revents & (POLLHUP | POLLERR))) {
                if (channel_fd != STDOUT_FILENO) {
                    close(channel_fd);
                    channel_fd = -1;
                }
            }
        } else {
            struct timespec ts = { 0, 50000000L };
            nanosleep(&ts, NULL);
        }

        /* 3. Emit telemetry when sample interval has elapsed */
        uint64_t now_ms = get_epoch_ms();
        if (now_ms - last_telemetry_ms >= (uint64_t)cfg->interval_ms) {
            last_telemetry_ms = now_ms;

            GuestTelemetryPayload payload;
            memset(&payload, 0, sizeof(payload));
            payload.version      = AGENT_PROTOCOL_VERSION;
            snprintf(payload.vm_id, sizeof(payload.vm_id), "%s", cfg->vm_id);
            payload.timestamp_ms = now_ms;
            payload.sequence     = seq++;

            cpu_monitor_sample(&payload.cpu);
            memory_monitor_sample(&payload.memory);
            disk_monitor_sample(&payload.disk);
            network_monitor_sample(&payload.network);
            process_monitor_sample_ex(payload.processes, AGENT_MAX_PROCESSES, &payload.process_count,
                                      &payload.total_process_count, &payload.process_snapshot_timestamp_ms);

            int json_len = agent_protocol_serialize(&payload, json_buffer, sizeof(json_buffer) - 2);
            if (json_len > 0) {
                json_buffer[json_len++] = '\n';
                json_buffer[json_len]   = '\0';

                if (cfg->channel_type == CHANNEL_STDOUT) {
                    write(STDOUT_FILENO, json_buffer, (size_t)json_len);
                } else if (channel_fd >= 0) {
                    ssize_t w = write(channel_fd, json_buffer, (size_t)json_len);
                    if (w < 0 && (errno == EPIPE || errno == EBADF)) {
                        if (channel_fd != STDOUT_FILENO) {
                            close(channel_fd);
                            channel_fd = -1;
                        }
                    }
                }
            }

            if (cfg->oneshot) break;
        }
    }

    if (channel_fd >= 0 && channel_fd != STDOUT_FILENO) close(channel_fd);
    if (server_sock >= 0) close(server_sock);

    cpu_monitor_shutdown();
    memory_monitor_shutdown();
    disk_monitor_shutdown();
    network_monitor_shutdown();
    process_monitor_shutdown();

    if (cfg->verbose) {
        fprintf(stderr, "[guest_agent] Exiting cleanly.\n");
    }
    return 0;
}

int main(int argc, char **argv)
{
    AgentConfig cfg;
    int opt_rc = agent_config_parse(&cfg, argc, argv);
    if (opt_rc != 0) return (opt_rc == 1) ? 0 : 1;
    return agent_run(&cfg);
}

