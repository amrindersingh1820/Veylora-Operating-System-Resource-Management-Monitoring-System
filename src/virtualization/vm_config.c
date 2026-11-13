/* vm_config.c – VM configuration and validation implementation. */
#include "vm_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#  include <strings.h>
#endif
#if defined(__APPLE__)
#  include <sys/sysctl.h>
#endif

/* sys/stat for path existence checks */
#if defined(_WIN32)
#  include <sys/types.h>
#  include <sys/stat.h>
#  define stat_fn  _stat
#  define stat_st  struct _stat
#  define strcasecmp _stricmp
#else
#  include <sys/stat.h>
#  define stat_fn  stat
#  define stat_st  struct stat
#endif

/* ================================================================ State names */
static const char *STATE_NAMES[VM_STATE_COUNT] = {
    "created",
    "disk_ready",
    "starting",
    "running",
    "stopping",
    "stopped",
    "failed"
};

const char *vm_state_name(VmState s)
{
    if (s >= 0 && s < VM_STATE_COUNT) return STATE_NAMES[s];
    return "unknown";
}

/* ================================================================ Architecture */
static const char *ARCH_NAMES[VM_ARCH_COUNT] = {
    "aarch64",
    "x86_64",
    "unknown"
};

const char *vm_arch_name(VmArch a)
{
    if (a >= 0 && a < VM_ARCH_COUNT) return ARCH_NAMES[a];
    return "unknown";
}

VmArch vm_arch_parse(const char *s)
{
    if (!s) return VM_ARCH_UNKNOWN;
    if (strcmp(s, "aarch64") == 0 || strcmp(s, "arm64") == 0)
        return VM_ARCH_AARCH64;
    if (strcmp(s, "x86_64") == 0 || strcmp(s, "amd64") == 0)
        return VM_ARCH_X86_64;
    return VM_ARCH_UNKNOWN;
}

/* ================================================================ Display */
static const char *DISPLAY_NAMES[VM_DISPLAY_COUNT] = {
    "none",
    "cocoa",
    "sdl",
    "vnc"
};

const char *vm_display_name(VmDisplay d)
{
    if (d >= 0 && d < VM_DISPLAY_COUNT) return DISPLAY_NAMES[d];
    return "unknown";
}

VmDisplay vm_display_parse(const char *s)
{
    if (!s) return VM_DISPLAY_NONE;
    if (strcmp(s, "none") == 0) return VM_DISPLAY_NONE;
    if (strcmp(s, "cocoa") == 0) return VM_DISPLAY_COCOA;
    if (strcmp(s, "sdl") == 0) return VM_DISPLAY_SDL;
    if (strcmp(s, "vnc") == 0) return VM_DISPLAY_VNC;
    return VM_DISPLAY_NONE;
}

/* ================================================================ Network */
static const char *NET_NAMES[VM_NET_COUNT] = {
    "none",
    "user",
    "tap"
};

const char *vm_net_name(VmNet n)
{
    if (n >= 0 && n < VM_NET_COUNT) return NET_NAMES[n];
    return "unknown";
}

VmNet vm_net_parse(const char *s)
{
    if (!s) return VM_NET_NONE;
    if (strcmp(s, "none") == 0) return VM_NET_NONE;
    if (strcmp(s, "user") == 0) return VM_NET_USER;
    if (strcmp(s, "tap") == 0) return VM_NET_TAP;
    return VM_NET_NONE;
}

/* ================================================================ Init */
void vm_config_init(VmConfig *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof *cfg);
    cfg->cpu_count  = 2;
    cfg->memory_mb  = 2048;
    cfg->arch       = VM_ARCH_AARCH64;
    cfg->display    = VM_DISPLAY_NONE;
    cfg->net        = VM_NET_USER;
    cfg->state      = VM_STATE_CREATED;
    snprintf(cfg->work_dir, sizeof cfg->work_dir, ".");
}

/* ================================================================ Validation helpers */
static int path_exists(const char *p)
{
    if (!p || p[0] == '\0') return 0;
    stat_st st;
    return stat_fn(p, &st) == 0;
}

int vm_config_resolve_path(const char *path, char *out_resolved, size_t out_sz)
{
    if (!path || path[0] == '\0') return 0;
    if (path_exists(path)) {
        if (out_resolved && out_sz > 0) snprintf(out_resolved, out_sz, "%s", path);
        return 1;
    }

    char candidate[VM_PATH_MAX_LEN];
    static const char *prefixes[] = {
        "../",
        "../../",
        "/Users/amrindersingh/Downloads/VirtualOS_Simulator/",
        NULL
    };

    for (int i = 0; prefixes[i]; i++) {
        snprintf(candidate, sizeof(candidate), "%s%s", prefixes[i], path);
        if (path_exists(candidate)) {
            if (out_resolved && out_sz > 0) snprintf(out_resolved, out_sz, "%s", candidate);
            return 1;
        }
    }
    return 0;
}

static int dir_exists(const char *p)
{
    if (!p || p[0] == '\0') return 0;
    stat_st st;
    if (stat_fn(p, &st) != 0) return 0;
#if defined(_WIN32)
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

static int is_supported_image_ext(const char *path)
{
    if (!path || path[0] == '\0') return 0;
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    if (strcasecmp(dot, ".iso") == 0) return 1;
    if (strcasecmp(dot, ".qcow2") == 0) return 1;
    if (strcasecmp(dot, ".img") == 0) return 1;
    if (strcasecmp(dot, ".raw") == 0) return 1;
    if (strcasecmp(dot, ".bin") == 0) return 1;
    return 0;
}

static unsigned long long get_host_ram_mb(void)
{
#if defined(__APPLE__)
    uint64_t mem = 0;
    size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, NULL, 0) == 0) {
        return (unsigned long long)(mem / (1024ULL * 1024ULL));
    }
#endif
    return 0;
}

/* ================================================================ Validate */
int vm_config_validate(const VmConfig *cfg,
                       const char *qemu_path,
                       char *err_buf, size_t err_buf_sz)
{
    /* Always produce a NUL-terminated result even on early return. */
    if (err_buf && err_buf_sz > 0) err_buf[0] = '\0';

#define FAIL(code, ...) \
    do { \
        if (err_buf && err_buf_sz > 0) \
            snprintf(err_buf, err_buf_sz, __VA_ARGS__); \
        return (code); \
    } while (0)

    if (!cfg) FAIL(VM_ERR_INVALID_ID, "cfg is NULL");

    /* --- Identity --- */
    if (cfg->id[0] == '\0')
        FAIL(VM_ERR_INVALID_ID, "VM id is empty");
    if (strlen(cfg->id) >= VM_ID_MAX_LEN)
        FAIL(VM_ERR_INVALID_ID, "VM id too long (max %d chars)", VM_ID_MAX_LEN - 1);
    if (cfg->name[0] == '\0')
        FAIL(VM_ERR_INVALID_NAME, "VM name is empty");

    /* --- Architecture --- */
    if (cfg->arch == VM_ARCH_UNKNOWN || (int)cfg->arch >= VM_ARCH_COUNT)
        FAIL(VM_ERR_INVALID_ARCH, "Unknown or unsupported guest architecture");

    /* --- Display & Networking --- */
    if ((int)cfg->display < 0 || (int)cfg->display >= VM_DISPLAY_COUNT)
        FAIL(VM_ERR_INVALID_DISPLAY, "Invalid display mode: %d", (int)cfg->display);
    if ((int)cfg->net < 0 || (int)cfg->net >= VM_NET_COUNT)
        FAIL(VM_ERR_INVALID_NET, "Invalid network mode: %d", (int)cfg->net);

    /* --- CPU --- */
    if (cfg->cpu_count < 1)
        FAIL(VM_ERR_INVALID_CPU, "cpu_count must be >= 1 (got %d)", cfg->cpu_count);
    if (cfg->cpu_count > VM_MAX_CPUS)
        FAIL(VM_ERR_INVALID_CPU,
             "cpu_count %d exceeds maximum %d", cfg->cpu_count, VM_MAX_CPUS);

    /* --- Memory --- */
    if (cfg->memory_mb < VM_MIN_MEM_MB)
        FAIL(VM_ERR_INVALID_MEM,
             "memory_mb %d is below minimum %d MiB", cfg->memory_mb, VM_MIN_MEM_MB);
    if (cfg->memory_mb > VM_MAX_MEM_MB)
        FAIL(VM_ERR_INVALID_MEM,
             "memory_mb %d exceeds maximum %d MiB", cfg->memory_mb, VM_MAX_MEM_MB);

    unsigned long long host_ram = get_host_ram_mb();
    if (host_ram > 0 && cfg->memory_mb > (int)(host_ram * 0.90)) {
        FAIL(VM_ERR_INVALID_MEM,
             "Configured RAM %d MiB exceeds 90%% of host RAM (%llu MiB). Allocate less to preserve macOS responsiveness.",
             cfg->memory_mb, host_ram);
    }

    /* --- Guest OS image --- */
    if (cfg->os_image_path[0] == '\0')
        FAIL(VM_ERR_MISSING_IMAGE,
             "os_image_path is empty – supply a .iso, .qcow2, or .img guest image");
    char resolved_os[VM_PATH_MAX_LEN] = {0};
    if (!vm_config_resolve_path(cfg->os_image_path, resolved_os, sizeof(resolved_os)))
        FAIL(VM_ERR_IMAGE_NOT_FOUND,
             "os_image_path not found: %s", cfg->os_image_path);
    if (!is_supported_image_ext(resolved_os))
        FAIL(VM_ERR_UNSUPPORTED_FORMAT,
             "Unsupported OS image format '%s'. Supported formats: .iso, .qcow2, .img, .raw",
             cfg->os_image_path);

    /* --- Optional secondary disk image --- */
    if (cfg->disk_image_path[0] != '\0') {
        char resolved_disk[VM_PATH_MAX_LEN] = {0};
        if (!vm_config_resolve_path(cfg->disk_image_path, resolved_disk, sizeof(resolved_disk)))
            FAIL(VM_ERR_INVALID_DISK,
                 "disk_image_path not found: %s", cfg->disk_image_path);
        if (!is_supported_image_ext(resolved_disk))
            FAIL(VM_ERR_UNSUPPORTED_FORMAT,
                 "Unsupported virtual disk format '%s'. Supported formats: .qcow2, .img, .raw",
                 cfg->disk_image_path);
    }

    /* --- Working directory --- */
    if (cfg->work_dir[0] == '\0')
        FAIL(VM_ERR_INVALID_WORKDIR, "work_dir is empty");
    if (!dir_exists(cfg->work_dir))
        FAIL(VM_ERR_WORKDIR_MISSING,
             "work_dir does not exist: %s", cfg->work_dir);

    /* --- QEMU executable (optional check) --- */
    if (qemu_path && qemu_path[0] != '\0') {
        if (!path_exists(qemu_path))
            FAIL(VM_ERR_MISSING_QEMU,
                 "QEMU executable not found: %s", qemu_path);
    }

#undef FAIL
    return VM_OK;
}
