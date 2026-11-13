/* network_monitor.c – Guest Network Metric Collection
 *
 * Collects transmitted and received bytes, packets, and errors strictly from Linux /proc/net/dev.
 * Filters out loopback (lo), aggregates active interfaces (eth0, enp0s*), and computes
 * interval throughput rates without fabricating artificial metrics.
 *
 * Requirements:
 *   - Genuine zero-traffic reporting when network is idle.
 *   - Multi-interface detection and active interface counting.
 *   - Never uses host macOS fallbacks.
 */

#include "metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t s_prev_rx_bytes = 0;
static uint64_t s_prev_tx_bytes = 0;
static uint64_t s_prev_time_ms = 0;
static int      s_has_prev = 0;

static uint64_t get_time_ms(void)
{
    struct timespec ts;
#if defined(CLOCK_REALTIME)
    clock_gettime(CLOCK_REALTIME, &ts);
#else
    clock_gettime(0, &ts);
#endif
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

int network_monitor_init(void)
{
    s_prev_rx_bytes = 0;
    s_prev_tx_bytes = 0;
    s_prev_time_ms = 0;
    s_has_prev = 0;
    return 0;
}

void network_monitor_shutdown(void)
{
    s_has_prev = 0;
    s_prev_time_ms = 0;
    s_prev_rx_bytes = 0;
    s_prev_tx_bytes = 0;
}

int network_monitor_sample(GuestNetworkMetrics *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    snprintf(out->iface_name, sizeof(out->iface_name), "none");

    char netdev_path[512];
    snprintf(netdev_path, sizeof(netdev_path), "%s/net/dev", agent_get_proc_root());

    FILE *fp = fopen(netdev_path, "r");
    if (!fp) {
        /* /proc/net/dev unavailable - return error */
        return -1;
    }

    char line[512];
    /* Skip header lines */
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return -1; }
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return -1; }

    uint64_t total_rx_b = 0, total_tx_b = 0;
    uint64_t total_rx_p = 0, total_tx_p = 0;
    uint64_t total_rx_e = 0, total_tx_e = 0;
    char primary_iface[32] = {0};
    int iface_count = 0;

    while (fgets(line, sizeof(line), fp)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = ' ';

        char ifname[64] = {0};
        unsigned long long r_bytes = 0, r_pkt = 0, r_err = 0, r_drop = 0, r_fifo = 0, r_frame = 0, r_comp = 0, r_mcast = 0;
        unsigned long long t_bytes = 0, t_pkt = 0, t_err = 0, t_drop = 0, t_fifo = 0, t_coll = 0, t_carrier = 0, t_comp = 0;

        int matched = sscanf(line, "%63s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                             ifname,
                             &r_bytes, &r_pkt, &r_err, &r_drop, &r_fifo, &r_frame, &r_comp, &r_mcast,
                             &t_bytes, &t_pkt, &t_err, &t_drop, &t_fifo, &t_coll, &t_carrier, &t_comp);

        if (matched >= 11) {
            /* Skip loopback interface */
            if (strcmp(ifname, "lo") == 0) continue;

            iface_count++;
            total_rx_b += r_bytes;
            total_rx_p += r_pkt;
            total_rx_e += r_err;
            total_tx_b += t_bytes;
            total_tx_p += t_pkt;
            total_tx_e += t_err;

            if (primary_iface[0] == '\0') {
                snprintf(primary_iface, sizeof(primary_iface), "%s", ifname);
            }
        }
    }
    fclose(fp);

    out->rx_bytes        = total_rx_b;
    out->tx_bytes        = total_tx_b;
    out->rx_packets      = total_rx_p;
    out->tx_packets      = total_tx_p;
    out->rx_errors       = total_rx_e;
    out->tx_errors       = total_tx_e;
    out->interface_count = iface_count;
    snprintf(out->iface_name, sizeof(out->iface_name), "%s",
             primary_iface[0] ? primary_iface : "eth0");

    /* Calculate interval throughput rate */
    uint64_t now_ms = get_time_ms();
    if (s_has_prev && now_ms > s_prev_time_ms) {
        double dt_s = (double)(now_ms - s_prev_time_ms) / 1000.0;
        if (dt_s > 0.0) {
            if (out->rx_bytes >= s_prev_rx_bytes) {
                out->rx_bytes_per_sec = (double)(out->rx_bytes - s_prev_rx_bytes) / dt_s;
            }
            if (out->tx_bytes >= s_prev_tx_bytes) {
                out->tx_bytes_per_sec = (double)(out->tx_bytes - s_prev_tx_bytes) / dt_s;
            }
        }
    } else {
        out->rx_bytes_per_sec = 0.0;
        out->tx_bytes_per_sec = 0.0;
    }

    s_prev_rx_bytes = out->rx_bytes;
    s_prev_tx_bytes = out->tx_bytes;
    s_prev_time_ms  = now_ms;
    s_has_prev      = 1;

    return 0;
}
