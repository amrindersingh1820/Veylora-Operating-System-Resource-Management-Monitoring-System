/* vm_manager.c – QEMU backend implementation for Virtual OS Simulator.
 *
 * Platform: C11, POSIX (macOS, Linux).
 * Windows stubs are provided so the file compiles but process management is
 * disabled there; contribute a CreateProcess implementation when needed.
 *
 * Security notes:
 *   - All argument arrays are built programmatically (no shell / system()).
 *   - posix_spawn(2) is used directly for process creation.
 *   - File paths are validated before they reach execv.
 *   - SIGKILL is only used after a graceful-stop timeout.
 */
#include "vm_manager.h"
#include "vm_config.h"
#include "../db/database.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <stdarg.h>

/* Platform-specific includes */
#if defined(_WIN32)
#  include <windows.h>
#  define POSIX_AVAILABLE 0
#else
#  include <unistd.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <sys/wait.h>
#  include <sys/stat.h>
#  include <spawn.h>
#  include <time.h>
#  define POSIX_AVAILABLE 1
extern char **environ;   /* required by posix_spawn */
#endif

/* ================================================================ Types */
/* VmManager struct is defined in vm_manager.h (visible for white-box testing) */

/* ================================================================ Helpers */

static void fill_err(char *buf, size_t sz, const char *fmt, ...)
{
    if (!buf || sz == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sz, fmt, ap);
    va_end(ap);
}

/* Find an entry in the vm table by id.  Returns pointer or NULL. */
static VmHandle *find_handle(VmManager *mgr, const char *vm_id)
{
    for (int i = 0; i < mgr->nvm; i++) {
        if (strcmp(mgr->vms[i].cfg.id, vm_id) == 0)
            return &mgr->vms[i];
    }
    return NULL;
}

/* ================================================================ Init / Shutdown */

VmManager *vm_manager_init(const char *qemu_search_path)
{
    VmManager *mgr = calloc(1, sizeof *mgr);
    if (!mgr) return NULL;
    if (qemu_search_path && qemu_search_path[0] != '\0') {
        mgr->custom_search_path = 1;
        snprintf(mgr->qemu_search_path, sizeof mgr->qemu_search_path,
                 "%s", qemu_search_path);
    } else {
        mgr->custom_search_path = 0;
        /* Use system PATH */
        const char *p = getenv("PATH");
        if (p) snprintf(mgr->qemu_search_path, sizeof mgr->qemu_search_path, "%s", p);
    }
    return mgr;
}

void vm_manager_shutdown(VmManager *mgr)
{
    free(mgr);
}

/* ================================================================ QEMU detection */

/* Architecture-to-binary name mapping. */
static const char *qemu_binary_name(VmArch arch)
{
    switch (arch) {
        case VM_ARCH_AARCH64: return "qemu-system-aarch64";
        case VM_ARCH_X86_64:  return "qemu-system-x86_64";
        default:              return NULL;
    }
}

/* Search for a binary in a colon-separated path list.
 * Writes the full path into out if found.  Returns 1 if found, 0 if not. */
static int search_path(const char *path_list, const char *binary,
                       char *out, size_t out_sz)
{
#if POSIX_AVAILABLE
    char buf[2048];
    snprintf(buf, sizeof buf, "%s", path_list);

    char *ctx = NULL;
    char *dir = strtok_r(buf, ":", &ctx);
    while (dir) {
        char candidate[VM_PATH_MAX_LEN];
        snprintf(candidate, sizeof candidate, "%s/%s", dir, binary);
        struct stat st;
        if (stat(candidate, &st) == 0 && (st.st_mode & S_IXUSR)) {
            snprintf(out, out_sz, "%s", candidate);
            return 1;
        }
        dir = strtok_r(NULL, ":", &ctx);
    }
#else
    (void)path_list; (void)binary; (void)out; (void)out_sz;
#endif
    return 0;
}

int vm_manager_detect_qemu(VmManager *mgr, VmArch arch,
                           char *qemu_path_out, size_t path_len,
                           char *err_buf, size_t err_buf_sz)
{
    if (!mgr) { fill_err(err_buf, err_buf_sz, "manager is NULL"); return VM_MGR_ERR_INTERNAL; }

    const char *bin = qemu_binary_name(arch);
    if (!bin) {
        fill_err(err_buf, err_buf_sz,
                 "No QEMU binary name known for arch %s", vm_arch_name(arch));
        return VM_MGR_ERR_QEMU_MISSING;
    }

    if (search_path(mgr->qemu_search_path, bin, qemu_path_out, path_len)) {
        return VM_MGR_OK;
    }

    /* Also try common macOS Homebrew prefix explicitly when using default PATH */
    if (!mgr->custom_search_path) {
#if defined(__APPLE__)
        const char *homebrew_prefixes[] = {
            "/opt/homebrew/bin",   /* Apple Silicon */
            "/usr/local/bin",      /* Intel Mac */
            NULL
        };
        for (int i = 0; homebrew_prefixes[i]; i++) {
            char candidate[VM_PATH_MAX_LEN];
            snprintf(candidate, sizeof candidate, "%s/%s", homebrew_prefixes[i], bin);
            struct stat st;
            if (stat(candidate, &st) == 0 && (st.st_mode & S_IXUSR)) {
                snprintf(qemu_path_out, path_len, "%s", candidate);
                return VM_MGR_OK;
            }
        }
#endif
    }

    fill_err(err_buf, err_buf_sz,
             "'%s' not found in PATH.\n"
             "To install on macOS:  brew install qemu\n"
             "To install on Linux:  sudo apt install qemu-system-arm  (Ubuntu/Debian)\n"
             "                      sudo dnf install qemu-system-aarch64  (Fedora)\n"
             "QEMU is required to start virtual machines. "
             "The rest of the simulator remains functional without it.",
             bin);
    return VM_MGR_ERR_QEMU_MISSING;
}

int vm_manager_qemu_version(const char *qemu_path,
                            char *version_out, size_t version_len)
{
    if (!qemu_path || qemu_path[0] == '\0' || !version_out || version_len == 0)
        return -1;

    version_out[0] = '\0';

#if POSIX_AVAILABLE
    /* Use popen for read-only stdout capture – safe because qemu_path is
     * validated (exists + executable) before this function is called. */
    char cmd[VM_PATH_MAX_LEN + 32];
    snprintf(cmd, sizeof cmd, "\"%s\" --version 2>&1", qemu_path);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    if (fgets(version_out, (int)version_len, fp) == NULL) version_out[0] = '\0';
    /* Strip trailing newline */
    size_t n = strlen(version_out);
    if (n > 0 && version_out[n - 1] == '\n') version_out[n - 1] = '\0';
    pclose(fp);
    return 0;
#else
    (void)qemu_path;
    snprintf(version_out, version_len, "(version detection not supported on this platform)");
    return 0;
#endif
}

/* Search for system UEFI/EDK2 firmware for ARM64 (e.g. edk2-aarch64-code.fd).
 * Returns 0 if found and copies path into fw_out; -1 if not found. */
int vm_manager_find_firmware(VmArch arch, char *fw_out, size_t fw_sz)
{
    if (!fw_out || fw_sz == 0) return -1;
    if (arch != VM_ARCH_AARCH64) return -1;

    static const char *candidates[] = {
        "/opt/homebrew/share/qemu/edk2-aarch64-code.fd",
        "/usr/local/share/qemu/edk2-aarch64-code.fd",
        "/usr/share/qemu/edk2-aarch64-code.fd",
        "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd",
        "/usr/share/edk2/aarch64/QEMU_EFI.fd",
        NULL
    };

    for (int i = 0; candidates[i]; i++) {
        struct stat st;
        if (stat(candidates[i], &st) == 0 && S_ISREG(st.st_mode)) {
            snprintf(fw_out, fw_sz, "%s", candidates[i]);
            return 0;
        }
    }
    return -1;
}

/* ================================================================ Build QEMU args */

/* Maximum number of QEMU argument strings we construct. */
#define MAX_QEMU_ARGS 64

/* Build the QEMU argument array for the given config.
 *
 * args[] is an array of string pointers.  The strings themselves are stored in
 * buf, a caller-supplied scratch buffer.  args[argc] is set to NULL.
 *
 * Returns the argument count on success, -1 if the buffer is too small or any
 * required field is missing.
 *
 * This function does NOT invoke any process; it is safe to call in tests.
 */
static int build_qemu_args(const VmConfig *cfg,
                           const char *qemu_path,
                           char *buf, size_t buf_sz,
                           char *args[], int max_args)
{
    if (!cfg || !qemu_path || !buf || !args) return -1;

    /* We allocate sub-strings by advancing a cursor through buf. */
    size_t used = 0;
    int    argc = 0;

#define PUSH_STR(s) \
    do { \
        size_t _n = strlen(s) + 1; \
        if (used + _n > buf_sz || argc >= max_args - 1) return -1; \
        memcpy(buf + used, (s), _n); \
        args[argc++] = buf + used; \
        used += _n; \
    } while (0)

#define PUSH_FMT(...) \
    do { \
        if (used >= buf_sz || argc >= max_args - 1) return -1; \
        int _written = snprintf(buf + used, buf_sz - used, __VA_ARGS__); \
        if (_written < 0 || (size_t)_written >= buf_sz - used) return -1; \
        args[argc++] = buf + used; \
        used += (size_t)_written + 1; \
    } while (0)

    PUSH_STR(qemu_path);

    /* Machine and acceleration */
    if (cfg->arch == VM_ARCH_AARCH64) {
        PUSH_STR("-machine"); PUSH_STR("virt");
        PUSH_STR("-cpu");
#if defined(__APPLE__) && defined(__aarch64__)
        /* Use host CPU model for best performance on Apple Silicon with HVF */
        PUSH_STR("host");
        PUSH_STR("-accel"); PUSH_STR("hvf");
#else
        PUSH_STR("cortex-a57");
        PUSH_STR("-accel"); PUSH_STR("tcg,thread=multi");
#endif
        /* Automatically configure EDK2 UEFI firmware if available */
        char fw[VM_PATH_MAX_LEN] = {0};
        if (vm_manager_find_firmware(cfg->arch, fw, sizeof fw) == 0) {
            PUSH_STR("-bios");
            PUSH_STR(fw);
        }
    } else if (cfg->arch == VM_ARCH_X86_64) {
        PUSH_STR("-machine"); PUSH_STR("q35");
#if defined(__x86_64__) || defined(_M_AMD64)
        PUSH_STR("-cpu"); PUSH_STR("host");
        PUSH_STR("-accel"); PUSH_STR("kvm");
#else
        PUSH_STR("-cpu"); PUSH_STR("qemu64");
        PUSH_STR("-accel"); PUSH_STR("tcg,thread=multi");
#endif
    }

    /* SMP (vCPUs) and memory */
    PUSH_FMT("-smp"); PUSH_FMT("%d", cfg->cpu_count);
    PUSH_FMT("-m"); PUSH_FMT("%dM", cfg->memory_mb);

    /* Storage: distinguish ISO boot media from virtual disk */
    const char *dot = strrchr(cfg->os_image_path, '.');
    int is_iso = (dot && strcasecmp(dot, ".iso") == 0);
    if (is_iso) {
        PUSH_STR("-cdrom");
        PUSH_STR(cfg->os_image_path);
    } else {
        int is_qcow2 = (dot && strcasecmp(dot, ".qcow2") == 0);
        PUSH_STR("-drive");
        PUSH_FMT("file=%s,format=%s,if=virtio",
                 cfg->os_image_path, is_qcow2 ? "qcow2" : "raw");
    }

    /* Optional secondary / writable disk image */
    if (cfg->disk_image_path[0] != '\0') {
        const char *ddot = strrchr(cfg->disk_image_path, '.');
        int d_is_qcow2 = (ddot && strcasecmp(ddot, ".qcow2") == 0);
        PUSH_STR("-drive");
        PUSH_FMT("file=%s,format=%s,if=virtio",
                 cfg->disk_image_path, d_is_qcow2 ? "qcow2" : "raw");
    }

    /* Display */
    PUSH_STR("-display");
    switch (cfg->display) {
        case VM_DISPLAY_COCOA:  PUSH_STR("cocoa");  break;
        case VM_DISPLAY_SDL:    PUSH_STR("sdl");    break;
        case VM_DISPLAY_VNC:    PUSH_STR("vnc=127.0.0.1:0"); break;
        default:                PUSH_STR("none");   break;
    }

    /* Graphics adapter & input devices for interactive boot screen display */
    if (cfg->display == VM_DISPLAY_COCOA || cfg->display == VM_DISPLAY_SDL) {
        PUSH_STR("-device"); PUSH_STR("virtio-gpu-pci");
        PUSH_STR("-device"); PUSH_STR("usb-ehci");
        PUSH_STR("-device"); PUSH_STR("usb-kbd");
        PUSH_STR("-device"); PUSH_STR("usb-tablet");
    }

    /* Networking */
    if (cfg->net == VM_NET_USER) {
        PUSH_STR("-nic"); PUSH_STR("user,model=virtio-net-pci");
    } else {
        PUSH_STR("-nic"); PUSH_STR("none");
    }

    /* Redirect serial console to stdio so we capture output */
    PUSH_STR("-serial"); PUSH_STR("stdio");
    /* No graphical monitor by default (security: don't expose control socket) */
    PUSH_STR("-monitor"); PUSH_STR("none");

    /* Virtio-serial channel for Virtual OS Guest Monitoring Agent */
    PUSH_STR("-chardev");
    PUSH_FMT("socket,id=vos_agent,path=/tmp/vos_agent_%s.sock,server=on,wait=off", cfg->id);
    PUSH_STR("-device"); PUSH_STR("virtio-serial-pci");
    PUSH_STR("-device"); PUSH_STR("virtserialport,chardev=vos_agent,name=org.virtualos.agent.0");

    args[argc] = NULL;

#undef PUSH_STR
#undef PUSH_FMT
    return argc;
}

/* ================================================================ Database schema */

/* SQL to extend the existing database with VM tables.
 * Uses CREATE TABLE IF NOT EXISTS so this is safe to run multiple times. */
static const char *VM_SCHEMA =
    "PRAGMA foreign_keys=ON;"
    "CREATE TABLE IF NOT EXISTS virtual_machines("
    "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  vm_id           TEXT    NOT NULL UNIQUE,"
    "  name            TEXT    NOT NULL,"
    "  guest_arch      TEXT    NOT NULL,"
    "  os_image_path   TEXT    NOT NULL,"
    "  cpu_count       INTEGER NOT NULL CHECK(cpu_count >= 1),"
    "  memory_mb       INTEGER NOT NULL CHECK(memory_mb >= 128),"
    "  disk_image_path TEXT    NOT NULL DEFAULT '',"
    "  display         TEXT    NOT NULL DEFAULT 'none',"
    "  net             TEXT    NOT NULL DEFAULT 'user',"
    "  work_dir        TEXT    NOT NULL DEFAULT '.',"
    "  status          TEXT    NOT NULL DEFAULT 'created',"
    "  created_at      TEXT    NOT NULL DEFAULT (datetime('now','localtime')),"
    "  updated_at      TEXT    NOT NULL DEFAULT (datetime('now','localtime'))"
    ");"
    "CREATE TABLE IF NOT EXISTS vm_events("
    "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  vm_db_id    INTEGER NOT NULL REFERENCES virtual_machines(id) ON DELETE CASCADE,"
    "  event_type  TEXT    NOT NULL,"
    "  message     TEXT    NOT NULL DEFAULT '',"
    "  created_at  TEXT    NOT NULL DEFAULT (datetime('now','localtime'))"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_vm_events_vm ON vm_events(vm_db_id);";

#include "sqlite3.h"

static sqlite3 *VM_DB = NULL;
static char     VM_DB_PATH[VM_PATH_MAX_LEN] = {0};

static int vm_db_open_if_needed(void)
{
    if (VM_DB) return 0;
    const char *path = VM_DB_PATH[0] ? VM_DB_PATH : "virtual_os.db";
    if (sqlite3_open(path, &VM_DB) != SQLITE_OK) {
        VM_DB = NULL;
        return -1;
    }
    sqlite3_exec(VM_DB, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);
    return 0;
}

int vm_db_ensure_schema(void)
{
    if (vm_db_open_if_needed() != 0) return -1;
    char *err = NULL;
    if (sqlite3_exec(VM_DB, VM_SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        sqlite3_free(err);
        return -1;
    }
    /* Non-destructive migrations for existing databases */
    sqlite3_exec(VM_DB, "ALTER TABLE virtual_machines ADD COLUMN display TEXT NOT NULL DEFAULT 'none';", NULL, NULL, NULL);
    sqlite3_exec(VM_DB, "ALTER TABLE virtual_machines ADD COLUMN net TEXT NOT NULL DEFAULT 'user';", NULL, NULL, NULL);
    sqlite3_exec(VM_DB, "ALTER TABLE virtual_machines ADD COLUMN work_dir TEXT NOT NULL DEFAULT '.';", NULL, NULL, NULL);
    return 0;
}

void vm_manager_set_db_path(const char *path)
{
    if (path) snprintf(VM_DB_PATH, sizeof VM_DB_PATH, "%s", path);
}

int vm_db_save(const VmHandle *h)
{
    if (!h) return -1;
    if (vm_db_open_if_needed() != 0) return -1;

    const VmConfig *c = &h->cfg;
    sqlite3_stmt   *st = NULL;
    int rc;

    int db_id = h->db_vm_id;
    if (db_id == 0) {
        /* Check if vm_id is already in the database */
        sqlite3_stmt *chk = NULL;
        if (sqlite3_prepare_v2(VM_DB, "SELECT id FROM virtual_machines WHERE vm_id=?", -1, &chk, NULL) == SQLITE_OK) {
            sqlite3_bind_text(chk, 1, c->id, -1, SQLITE_STATIC);
            if (sqlite3_step(chk) == SQLITE_ROW) {
                db_id = sqlite3_column_int(chk, 0);
            }
            sqlite3_finalize(chk);
        }
    }

    if (db_id == 0) {
        /* INSERT */
        const char *sql =
            "INSERT INTO virtual_machines"
            "(vm_id, name, guest_arch, os_image_path, cpu_count, memory_mb,"
            " disk_image_path, display, net, work_dir, status)"
            "VALUES(?,?,?,?,?,?,?,?,?,?,?)";
        if (sqlite3_prepare_v2(VM_DB, sql, -1, &st, NULL) != SQLITE_OK) return -1;
        sqlite3_bind_text(st, 1, c->id,            -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, c->name,          -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, vm_arch_name(c->arch), -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 4, c->os_image_path, -1, SQLITE_STATIC);
        sqlite3_bind_int (st, 5, c->cpu_count);
        sqlite3_bind_int (st, 6, c->memory_mb);
        sqlite3_bind_text(st, 7, c->disk_image_path, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 8, vm_display_name(c->display), -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 9, vm_net_name(c->net), -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 10, c->work_dir[0] ? c->work_dir : ".", -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 11, vm_state_name(h->state), -1, SQLITE_STATIC);
        rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE) return -1;
        return (int)sqlite3_last_insert_rowid(VM_DB);
    } else {
        /* UPDATE */
        const char *sql =
            "UPDATE virtual_machines SET"
            " name=?, guest_arch=?, os_image_path=?, cpu_count=?, memory_mb=?,"
            " disk_image_path=?, display=?, net=?, work_dir=?, status=?, updated_at=datetime('now','localtime')"
            " WHERE id=?";
        if (sqlite3_prepare_v2(VM_DB, sql, -1, &st, NULL) != SQLITE_OK) return -1;
        sqlite3_bind_text(st, 1, c->name,          -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, vm_arch_name(c->arch), -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, c->os_image_path, -1, SQLITE_STATIC);
        sqlite3_bind_int (st, 4, c->cpu_count);
        sqlite3_bind_int (st, 5, c->memory_mb);
        sqlite3_bind_text(st, 6, c->disk_image_path, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 7, vm_display_name(c->display), -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 8, vm_net_name(c->net), -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 9, c->work_dir[0] ? c->work_dir : ".", -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 10, vm_state_name(h->state), -1, SQLITE_STATIC);
        sqlite3_bind_int (st, 11, db_id);
        rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE) return -1;
        return db_id;
    }
}

int vm_db_log_event(int db_vm_id, const char *event_type, const char *message)
{
    if (vm_db_open_if_needed() != 0) return -1;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO vm_events(vm_db_id, event_type, message)"
        " VALUES(?,?,?)";
    if (sqlite3_prepare_v2(VM_DB, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int (st, 1, db_vm_id);
    sqlite3_bind_text(st, 2, event_type ? event_type : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, message    ? message    : "", -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int vm_manager_reconcile_db(VmManager *mgr)
{
    if (vm_db_open_if_needed() != 0) return -1;
    vm_db_ensure_schema();

    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT id, vm_id, status FROM virtual_machines"
        " WHERE status IN ('starting', 'running', 'stopping');";
    if (sqlite3_prepare_v2(VM_DB, sql, -1, &st, NULL) != SQLITE_OK) return -1;

    int reconciled = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        int row_id = sqlite3_column_int(st, 0);
        const char *vmid = (const char *)sqlite3_column_text(st, 1);
        const char *st_str = (const char *)sqlite3_column_text(st, 2);

        int is_alive = 0;
        if (mgr && vmid) {
            VmHandle *h = find_handle(mgr, vmid);
            if (h && h->pid > 0) {
#if POSIX_AVAILABLE
                if (kill(h->pid, 0) == 0) is_alive = 1;
#endif
            }
        }

        if (!is_alive) {
            sqlite3_stmt *up = NULL;
            const char *up_sql =
                "UPDATE virtual_machines SET status='stopped', updated_at=datetime('now','localtime')"
                " WHERE id=?";
            if (sqlite3_prepare_v2(VM_DB, up_sql, -1, &up, NULL) == SQLITE_OK) {
                sqlite3_bind_int(up, 1, row_id);
                sqlite3_step(up);
                sqlite3_finalize(up);
            }
            char msg[256];
            snprintf(msg, sizeof msg, "Reconciled stale state '%s' -> 'stopped' on startup", st_str ? st_str : "unknown");
            vm_db_log_event(row_id, "reconciled", msg);
            reconciled++;
        }
    }
    sqlite3_finalize(st);
    return reconciled;
}

int vm_db_list(VmConfig *out_configs, VmState *out_states, int *out_pids, int max_vms)
{
    if (!out_configs || max_vms <= 0) return 0;
    if (vm_db_open_if_needed() != 0) return -1;
    vm_db_ensure_schema();

    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT vm_id, name, guest_arch, os_image_path, cpu_count, memory_mb,"
        " disk_image_path, display, net, work_dir, status"
        " FROM virtual_machines ORDER BY id ASC LIMIT ?;";
    if (sqlite3_prepare_v2(VM_DB, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, max_vms);

    int count = 0;
    while (sqlite3_step(st) == SQLITE_ROW && count < max_vms) {
        VmConfig *c = &out_configs[count];
        vm_config_init(c);

        const char *vmid = (const char *)sqlite3_column_text(st, 0);
        const char *name = (const char *)sqlite3_column_text(st, 1);
        const char *arch = (const char *)sqlite3_column_text(st, 2);
        const char *os_img = (const char *)sqlite3_column_text(st, 3);
        int cpus = sqlite3_column_int(st, 4);
        int mem  = sqlite3_column_int(st, 5);
        const char *d_img = (const char *)sqlite3_column_text(st, 6);
        const char *disp = (const char *)sqlite3_column_text(st, 7);
        const char *net = (const char *)sqlite3_column_text(st, 8);
        const char *work = (const char *)sqlite3_column_text(st, 9);
        const char *status = (const char *)sqlite3_column_text(st, 10);

        if (vmid) snprintf(c->id, sizeof c->id, "%s", vmid);
        if (name) snprintf(c->name, sizeof c->name, "%s", name);
        c->arch = vm_arch_parse(arch);
        if (os_img) snprintf(c->os_image_path, sizeof c->os_image_path, "%s", os_img);
        c->cpu_count = cpus;
        c->memory_mb = mem;
        if (d_img) snprintf(c->disk_image_path, sizeof c->disk_image_path, "%s", d_img);
        c->display = vm_display_parse(disp);
        c->net = vm_net_parse(net);
        if (work) snprintf(c->work_dir, sizeof c->work_dir, "%s", work);

        VmState st_val = VM_STATE_CREATED;
        if (status) {
            for (int s = 0; s < VM_STATE_COUNT; s++) {
                if (strcmp(status, vm_state_name((VmState)s)) == 0) {
                    st_val = (VmState)s;
                    break;
                }
            }
        }
        c->state = st_val;
        if (out_states) out_states[count] = st_val;
        if (out_pids) out_pids[count] = 0;

        count++;
    }
    sqlite3_finalize(st);
    return count;
}

int vm_db_get(const char *vm_id, VmConfig *out_cfg, VmState *out_state)
{
    if (!vm_id || !out_cfg) return -1;
    if (vm_db_open_if_needed() != 0) return -1;
    vm_db_ensure_schema();

    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT name, guest_arch, os_image_path, cpu_count, memory_mb,"
        " disk_image_path, display, net, work_dir, status"
        " FROM virtual_machines WHERE vm_id=? LIMIT 1;";
    if (sqlite3_prepare_v2(VM_DB, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);

    int found = -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        vm_config_init(out_cfg);
        snprintf(out_cfg->id, sizeof out_cfg->id, "%s", vm_id);

        const char *name = (const char *)sqlite3_column_text(st, 0);
        const char *arch = (const char *)sqlite3_column_text(st, 1);
        const char *os_img = (const char *)sqlite3_column_text(st, 2);
        int cpus = sqlite3_column_int(st, 3);
        int mem  = sqlite3_column_int(st, 4);
        const char *d_img = (const char *)sqlite3_column_text(st, 5);
        const char *disp = (const char *)sqlite3_column_text(st, 6);
        const char *net = (const char *)sqlite3_column_text(st, 7);
        const char *work = (const char *)sqlite3_column_text(st, 8);
        const char *status = (const char *)sqlite3_column_text(st, 9);

        if (name) snprintf(out_cfg->name, sizeof out_cfg->name, "%s", name);
        out_cfg->arch = vm_arch_parse(arch);
        if (os_img) snprintf(out_cfg->os_image_path, sizeof out_cfg->os_image_path, "%s", os_img);
        out_cfg->cpu_count = cpus;
        out_cfg->memory_mb = mem;
        if (d_img) snprintf(out_cfg->disk_image_path, sizeof out_cfg->disk_image_path, "%s", d_img);
        out_cfg->display = vm_display_parse(disp);
        out_cfg->net = vm_net_parse(net);
        if (work) snprintf(out_cfg->work_dir, sizeof out_cfg->work_dir, "%s", work);

        VmState st_val = VM_STATE_CREATED;
        if (status) {
            for (int s = 0; s < VM_STATE_COUNT; s++) {
                if (strcmp(status, vm_state_name((VmState)s)) == 0) {
                    st_val = (VmState)s;
                    break;
                }
            }
        }
        out_cfg->state = st_val;
        if (out_state) *out_state = st_val;
        found = 0;
    }
    sqlite3_finalize(st);
    return found;
}

int vm_db_delete(const char *vm_id, char *err_buf, size_t err_buf_sz)
{
    if (err_buf && err_buf_sz > 0) err_buf[0] = '\0';
    if (!vm_id) {
        if (err_buf && err_buf_sz > 0) snprintf(err_buf, err_buf_sz, "vm_id is NULL");
        return -1;
    }
    if (vm_db_open_if_needed() != 0) return -1;
    vm_db_ensure_schema();

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(VM_DB, "SELECT status FROM virtual_machines WHERE vm_id=?", -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char *st_str = (const char *)sqlite3_column_text(st, 0);
        if (st_str && (strcmp(st_str, "running") == 0 || strcmp(st_str, "starting") == 0)) {
            sqlite3_finalize(st);
            if (err_buf && err_buf_sz > 0) {
                snprintf(err_buf, err_buf_sz, "Cannot delete VM '%s': state is %s. Stop it first.", vm_id, st_str);
            }
            return -1;
        }
    } else {
        sqlite3_finalize(st);
        if (err_buf && err_buf_sz > 0) snprintf(err_buf, err_buf_sz, "VM '%s' not found", vm_id);
        return -1;
    }
    sqlite3_finalize(st);

    sqlite3_stmt *del = NULL;
    if (sqlite3_prepare_v2(VM_DB, "DELETE FROM virtual_machines WHERE vm_id=?", -1, &del, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(del, 1, vm_id, -1, SQLITE_STATIC);
    int rc = sqlite3_step(del);
    sqlite3_finalize(del);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int vm_db_get_events(const char *vm_id, char *out_log, size_t out_log_sz)
{
    if (!vm_id || !out_log || out_log_sz == 0) return -1;
    out_log[0] = '\0';
    if (vm_db_open_if_needed() != 0) return -1;
    vm_db_ensure_schema();

    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT e.created_at, e.event_type, e.message FROM vm_events e "
        "JOIN virtual_machines v ON e.vm_db_id = v.id "
        "WHERE v.vm_id=? ORDER BY e.id DESC LIMIT 20;";
    if (sqlite3_prepare_v2(VM_DB, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, vm_id, -1, SQLITE_STATIC);

    size_t used = 0;
    while (sqlite3_step(st) == SQLITE_ROW && used < out_log_sz - 1) {
        const char *ts = (const char *)sqlite3_column_text(st, 0);
        const char *ev = (const char *)sqlite3_column_text(st, 1);
        const char *msg = (const char *)sqlite3_column_text(st, 2);
        int written = snprintf(out_log + used, out_log_sz - used,
                               "[%s] %-14s %s\n",
                               ts ? ts : "", ev ? ev : "", msg ? msg : "");
        if (written > 0) used += (size_t)written;
    }
    sqlite3_finalize(st);
    return (int)used;
}

static void read_log_tail(const char *path, char *out, size_t out_sz)
{
    if (!path || !out || out_sz == 0) return;
    out[0] = '\0';
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    if (fseek(fp, 0, SEEK_END) == 0) {
        long sz = ftell(fp);
        if (sz > 0) {
            long read_len = (sz < (long)(out_sz - 1)) ? sz : (long)(out_sz - 1);
            fseek(fp, sz - read_len, SEEK_SET);
            size_t n = fread(out, 1, (size_t)read_len, fp);
            out[n] = '\0';
        }
    }
    fclose(fp);
}

/* ================================================================ vm_start */

int vm_start(VmManager *mgr, VmConfig *cfg,
             char *err_buf, size_t err_buf_sz)
{
    if (!mgr || !cfg) {
        fill_err(err_buf, err_buf_sz, "NULL argument");
        return VM_MGR_ERR_INTERNAL;
    }

    /* 1. Check for existing handle */
    VmHandle *h = find_handle(mgr, cfg->id);
    if (h) {
        if (h->state == VM_STATE_RUNNING || h->state == VM_STATE_STARTING || h->state == VM_STATE_STOPPING) {
            fill_err(err_buf, err_buf_sz,
                     "VM '%s' is already active (state: %s, PID: %d)",
                     cfg->id, vm_state_name(h->state), (int)h->pid);
            return VM_MGR_ERR_ALREADY_RUN;
        }
        /* Reuse existing handle slot */
    } else {
        if (mgr->nvm >= VM_MGR_MAX_VMS) {
            fill_err(err_buf, err_buf_sz, "Maximum concurrent VMs (%d) reached", VM_MGR_MAX_VMS);
            return VM_MGR_ERR_INTERNAL;
        }
        h = &mgr->vms[mgr->nvm];
        mgr->nvm++;
    }

    /* 2. Detect QEMU */
    char qemu_path[VM_PATH_MAX_LEN] = {0};
    int det = vm_manager_detect_qemu(mgr, cfg->arch, qemu_path, sizeof qemu_path,
                                     err_buf, err_buf_sz);
    if (det != VM_MGR_OK) return det;

    /* 3. Validate config */
    char verr[512];
    int vrc = vm_config_validate(cfg, qemu_path, verr, sizeof verr);
    if (vrc != VM_OK) {
        fill_err(err_buf, err_buf_sz, "Config invalid: %s", verr);
        cfg->state = VM_STATE_FAILED;
        return VM_MGR_ERR_CONFIG;
    }

    /* 3.1 Canonicalize image paths so QEMU receives valid paths regardless of working directory */
    vm_config_resolve_path(cfg->os_image_path, cfg->os_image_path, sizeof(cfg->os_image_path));
    if (cfg->disk_image_path[0] != '\0') {
        vm_config_resolve_path(cfg->disk_image_path, cfg->disk_image_path, sizeof(cfg->disk_image_path));
    }

    /* 4. Build argument array */
    char     arg_buf[8192];
    char    *args[MAX_QEMU_ARGS];
    int      argc = build_qemu_args(cfg, qemu_path, arg_buf, sizeof arg_buf, args, MAX_QEMU_ARGS);
    if (argc < 0) {
        fill_err(err_buf, err_buf_sz, "Failed to build QEMU argument list (buffer too small?)");
        cfg->state = VM_STATE_FAILED;
        return VM_MGR_ERR_INTERNAL;
    }

    /* 5. Set up handle metadata */
    int prev_db_id = h->db_vm_id;
    memset(h, 0, sizeof *h);
    h->cfg         = *cfg;
    h->state       = VM_STATE_STARTING;
    h->pid         = 0;
    h->guest_ready = 0;
    h->db_vm_id    = prev_db_id;
    snprintf(h->console_log, sizeof h->console_log, "%s/%s_console.log",
             (cfg->work_dir[0] ? cfg->work_dir : "."), cfg->id);

    /* Save to DB */
    vm_db_ensure_schema();
    int db_id = vm_db_save(h);
    h->db_vm_id = (db_id > 0) ? db_id : prev_db_id;

#if POSIX_AVAILABLE
    /* 6. Spawn the QEMU process with console output logged */
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);

    /* Redirect stdout and stderr to the VM console log */
    int log_flags = O_WRONLY | O_CREAT | O_TRUNC;
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, h->console_log, log_flags, 0644);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, h->console_log, log_flags, 0644);

    pid_t pid = 0;
    int spawn_rc = posix_spawn(&pid, qemu_path, &fa, NULL, args, environ);
    posix_spawn_file_actions_destroy(&fa);

    if (spawn_rc != 0) {
        fill_err(err_buf, err_buf_sz,
                 "posix_spawn failed for '%s': %s", qemu_path, strerror(spawn_rc));
        h->state = VM_STATE_FAILED;
        snprintf(h->last_error, sizeof h->last_error, "%s", err_buf ? err_buf : "spawn error");
        if (h->db_vm_id > 0) {
            vm_db_save(h);
            vm_db_log_event(h->db_vm_id, "failed", h->last_error);
        }
        cfg->state = VM_STATE_FAILED;
        return VM_MGR_ERR_SPAWN;
    }

    /* 7. Startup probe: wait 150ms and check whether QEMU crashed immediately */
    struct timespec probe_delay = { 0, 150000000L };
    nanosleep(&probe_delay, NULL);

    int wstatus = 0;
    pid_t probe_ret = waitpid(pid, &wstatus, WNOHANG);
    if (probe_ret == pid) {
        h->exit_code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
        h->pid       = 0;
        h->state     = VM_STATE_FAILED;
        cfg->state   = VM_STATE_FAILED;

        char log_snippet[256] = {0};
        read_log_tail(h->console_log, log_snippet, sizeof log_snippet);
        for (char *p = log_snippet; *p; p++) {
            if (*p == '\n' || *p == '\r') *p = ' ';
        }

        fill_err(err_buf, err_buf_sz,
                 "QEMU failed to start (exit code %d). Log: %s",
                 h->exit_code, log_snippet[0] ? log_snippet : "(no output)");
        snprintf(h->last_error, sizeof h->last_error, "%s", err_buf ? err_buf : "startup crash");

        if (h->db_vm_id > 0) {
            vm_db_save(h);
            vm_db_log_event(h->db_vm_id, "startup_failed", h->last_error);
        }
        return VM_MGR_ERR_SPAWN;
    }

    /* 8. Process is active */
    h->pid         = pid;
    h->state       = VM_STATE_RUNNING;
    h->guest_ready = 0; /* Guest OS booting; boot readiness unconfirmed */
    cfg->state     = VM_STATE_RUNNING;

    if (h->db_vm_id > 0) {
        vm_db_save(h);
        char msg[256];
        snprintf(msg, sizeof msg, "QEMU PID %d running (%s)", (int)pid, qemu_path);
        vm_db_log_event(h->db_vm_id, "started", msg);
    }
    return VM_MGR_OK;

#else
    /* Windows / unsupported platform stub */
    (void)argc;
    fill_err(err_buf, err_buf_sz,
             "Process spawning is not implemented on this platform. "
             "Implement CreateProcess support for Windows.");
    h->state = VM_STATE_FAILED;
    if (h->db_vm_id > 0) {
        vm_db_save(h);
        vm_db_log_event(h->db_vm_id, "failed", "platform not supported");
    }
    cfg->state = VM_STATE_FAILED;
    return VM_MGR_ERR_SPAWN;
#endif
}

/* ================================================================ vm_stop */

int vm_stop(VmManager *mgr, const char *vm_id,
            int timeout_ms,
            char *err_buf, size_t err_buf_sz)
{
    if (!mgr || !vm_id) {
        fill_err(err_buf, err_buf_sz, "NULL argument");
        return VM_MGR_ERR_INTERNAL;
    }
    VmHandle *h = find_handle(mgr, vm_id);
    if (!h || h->pid == 0 || h->state != VM_STATE_RUNNING) {
        fill_err(err_buf, err_buf_sz, "VM '%s' is not running", vm_id);
        return VM_MGR_ERR_NOT_RUNNING;
    }

    if (timeout_ms <= 0) timeout_ms = 5000;

    h->state = VM_STATE_STOPPING;
    if (h->db_vm_id > 0) {
        vm_db_save(h);
        vm_db_log_event(h->db_vm_id, "stopping", "graceful shutdown requested");
    }

#if POSIX_AVAILABLE
    /* Send SIGTERM */
    kill(h->pid, SIGTERM);

    /* Poll until the process exits or timeout expires */
    int   elapsed_ms = 0;
    int   interval   = 100;  /* ms between waitpid polls */
    int   forced     = 0;
    while (elapsed_ms < timeout_ms) {
        int wstatus = 0;
        pid_t ret = waitpid(h->pid, &wstatus, WNOHANG);
        if (ret == h->pid) {
            h->exit_code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
            goto exited;
        }
        struct timespec ts = { 0, (long)interval * 1000000L };
        nanosleep(&ts, NULL);
        elapsed_ms += interval;
    }

    /* Timeout – force kill */
    kill(h->pid, SIGKILL);
    forced = 1;
    waitpid(h->pid, NULL, 0);   /* reap */

exited:
    h->pid         = 0;
    h->state       = VM_STATE_STOPPED;
    h->guest_ready = 0;
    if (h->db_vm_id > 0) {
        vm_db_save(h);
        vm_db_log_event(h->db_vm_id, "stopped",
                        forced ? "SIGKILL after timeout" : "exited cleanly");
    }
    if (forced) {
        fill_err(err_buf, err_buf_sz,
                 "VM '%s' did not stop within %d ms; SIGKILL was sent",
                 vm_id, timeout_ms);
        return VM_MGR_ERR_TIMEOUT;
    }
    return VM_MGR_OK;
#else
    fill_err(err_buf, err_buf_sz, "vm_stop not implemented on this platform");
    return VM_MGR_ERR_INTERNAL;
#endif
}

/* ================================================================ Reap children */

int vm_reap_children(VmManager *mgr)
{
    if (!mgr) return 0;
    int count = 0;
#if POSIX_AVAILABLE
    for (int i = 0; i < mgr->nvm; i++) {
        VmHandle *h = &mgr->vms[i];
        if (h->pid == 0 || h->state != VM_STATE_RUNNING) continue;
        int wstatus = 0;
        pid_t ret = waitpid(h->pid, &wstatus, WNOHANG);
        if (ret == h->pid) {
            h->exit_code   = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
            h->pid         = 0;
            h->state       = VM_STATE_FAILED;
            h->guest_ready = 0;
            count++;

            char log_snippet[128] = {0};
            read_log_tail(h->console_log, log_snippet, sizeof log_snippet);
            for (char *p = log_snippet; *p; p++) {
                if (*p == '\n' || *p == '\r') *p = ' ';
            }
            snprintf(h->last_error, sizeof h->last_error,
                     "unexpected exit (code %d)%s%s",
                     h->exit_code, log_snippet[0] ? ": " : "", log_snippet);

            if (h->db_vm_id > 0) {
                vm_db_save(h);
                vm_db_log_event(h->db_vm_id, "failed", h->last_error);
            }
        }
    }
#endif
    return count;
}

/* ================================================================ Status queries */

VmState vm_get_state(VmManager *mgr, const char *vm_id)
{
    if (!mgr || !vm_id) return VM_STATE_COUNT;
    VmHandle *h = find_handle(mgr, vm_id);
    return h ? h->state : VM_STATE_COUNT;
}

int vm_get_handle(VmManager *mgr, const char *vm_id, VmHandle *out)
{
    if (!mgr || !vm_id || !out) return -1;
    VmHandle *h = find_handle(mgr, vm_id);
    if (!h) return -1;
    *out = *h;
    return 0;
}
