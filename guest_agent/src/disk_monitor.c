/* disk_monitor.c – Guest Disk Metric Collection
 *
 * Collects read/write bytes and I/O operations strictly from Linux /proc/diskstats.
 * Filters out loop, RAM block devices, and prevents partition double-counting
 * against parent whole-disk devices (e.g. vda vs vda1, vda2, vda3).
 *
 * Requirements:
 *   - Sectors represented in 512-byte units.
 *   - Cumulative counters vs. interval rates distinguished.
 *   - Never uses host macOS fallbacks.
 */

#include "metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define MAX_DISK_DEVS 64

typedef struct {
    int      major;
    int      minor;
    char     name[64];
    uint64_t reads;
    uint64_t read_sectors;
    uint64_t writes;
    uint64_t write_sectors;
    uint64_t in_flight;
    int      is_partition;
} DiskDevRecord;

static uint64_t s_prev_read_bytes = 0;
static uint64_t s_prev_write_bytes = 0;
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

int disk_monitor_init(void)
{
    s_prev_read_bytes = 0;
    s_prev_write_bytes = 0;
    s_prev_time_ms = 0;
    s_has_prev = 0;
    return 0;
}

void disk_monitor_shutdown(void)
{
    s_has_prev = 0;
    s_prev_time_ms = 0;
    s_prev_read_bytes = 0;
    s_prev_write_bytes = 0;
}

/* Helper to detect if child is a partition of parent */
static int is_child_partition(const char *child, const char *parent)
{
    size_t plen = strlen(parent);
    size_t clen = strlen(child);
    if (clen <= plen) return 0;
    if (strncmp(child, parent, plen) != 0) return 0;

    /* e.g. "vda1" of "vda" or "sda2" of "sda" */
    if (isdigit((unsigned char)child[plen])) return 1;

    /* e.g. "nvme0n1p1" of "nvme0n1" */
    if (child[plen] == 'p' && isdigit((unsigned char)child[plen + 1])) return 1;

    return 0;
}

int disk_monitor_sample(GuestDiskMetrics *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    snprintf(out->primary_disk, sizeof(out->primary_disk), "none");

    char diskstats_path[512];
    snprintf(diskstats_path, sizeof(diskstats_path), "%s/diskstats", agent_get_proc_root());

    FILE *fp = fopen(diskstats_path, "r");
    if (!fp) {
        /* /proc/diskstats unavailable - return error */
        return -1;
    }

    char line[512];
    DiskDevRecord records[MAX_DISK_DEVS];
    int record_count = 0;

    while (fgets(line, sizeof(line), fp) && record_count < MAX_DISK_DEVS) {
        int major = 0, minor = 0;
        char dev_name[64] = {0};
        unsigned long long reads = 0, r_merged = 0, r_sectors = 0, r_ms = 0;
        unsigned long long writes = 0, w_merged = 0, w_sectors = 0, w_ms = 0;
        unsigned long long in_flight = 0;

        int fields = sscanf(line, "%d %d %63s %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                            &major, &minor, dev_name,
                            &reads, &r_merged, &r_sectors, &r_ms,
                            &writes, &w_merged, &w_sectors, &w_ms,
                            &in_flight);

        if (fields >= 11) {
            /* Filter out loop devices, RAM disks, zram, and device-mapper */
            if (strncmp(dev_name, "loop", 4) == 0 ||
                strncmp(dev_name, "ram", 3) == 0  ||
                strncmp(dev_name, "zram", 4) == 0 ||
                strncmp(dev_name, "dm-", 3) == 0) {
                continue;
            }

            DiskDevRecord *r = &records[record_count++];
            r->major         = major;
            r->minor         = minor;
            snprintf(r->name, sizeof(r->name), "%s", dev_name);
            r->reads         = reads;
            r->read_sectors  = r_sectors;
            r->writes        = writes;
            r->write_sectors = w_sectors;
            r->in_flight     = in_flight;
            r->is_partition  = 0;
        }
    }
    fclose(fp);

    /* Identify partitions vs. parent disks to prevent double-counting */
    int parent_disk_count = 0;
    for (int i = 0; i < record_count; i++) {
        for (int j = 0; j < record_count; j++) {
            if (i != j && is_child_partition(records[i].name, records[j].name)) {
                records[i].is_partition = 1;
                break;
            }
        }
        if (!records[i].is_partition) {
            parent_disk_count++;
        }
    }

    uint64_t total_reads = 0, total_writes = 0;
    uint64_t total_read_sectors = 0, total_write_sectors = 0;
    uint64_t total_io_in_flight = 0;
    char primary_dev[32] = {0};

    /* If parent disks were identified, aggregate ONLY parent disks.
     * If no parent disk was found (e.g. single partition in isolated fixture),
     * fall back to aggregating all parsed non-loop devices. */
    int use_all = (parent_disk_count == 0);

    for (int i = 0; i < record_count; i++) {
        if (use_all || !records[i].is_partition) {
            total_reads         += records[i].reads;
            total_writes        += records[i].writes;
            total_read_sectors  += records[i].read_sectors;
            total_write_sectors += records[i].write_sectors;
            total_io_in_flight  += records[i].in_flight;

            if (primary_dev[0] == '\0') {
                snprintf(primary_dev, sizeof(primary_dev), "%s", records[i].name);
            }
        }
    }

    /* Standard Linux disk sector size in /proc/diskstats is 512 bytes */
    out->read_bytes     = total_read_sectors * 512ULL;
    out->write_bytes    = total_write_sectors * 512ULL;
    out->read_ops       = total_reads;
    out->write_ops      = total_writes;
    out->io_in_progress = total_io_in_flight;
    snprintf(out->primary_disk, sizeof(out->primary_disk), "%s",
             primary_dev[0] ? primary_dev : "vda");

    /* Calculate interval rate */
    uint64_t now_ms = get_time_ms();
    if (s_has_prev && now_ms > s_prev_time_ms) {
        double dt_s = (double)(now_ms - s_prev_time_ms) / 1000.0;
        if (dt_s > 0.0) {
            if (out->read_bytes >= s_prev_read_bytes) {
                out->read_bytes_per_sec = (double)(out->read_bytes - s_prev_read_bytes) / dt_s;
            }
            if (out->write_bytes >= s_prev_write_bytes) {
                out->write_bytes_per_sec = (double)(out->write_bytes - s_prev_write_bytes) / dt_s;
            }
        }
    } else {
        out->read_bytes_per_sec = 0.0;
        out->write_bytes_per_sec = 0.0;
    }

    s_prev_read_bytes  = out->read_bytes;
    s_prev_write_bytes = out->write_bytes;
    s_prev_time_ms     = now_ms;
    s_has_prev         = 1;

    return 0;
}
