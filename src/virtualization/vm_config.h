/* vm_config.h – Virtual Machine configuration model for Virtual OS Simulator.
 *
 * Platform: C11, portable (macOS, Linux; Windows stubs allowed).
 * This file is pure data-model and validation; it has no I/O or process
 * management.  All fields are value types (no heap ownership) so the caller
 * can store VmConfig on the stack or embed it in a larger struct.
 */
#ifndef VOS_VM_CONFIG_H
#define VOS_VM_CONFIG_H

#include <stddef.h>

/* ------------------------------------------------------------------ Constants */

#define VM_ID_MAX_LEN      64    /* UUID-like string, e.g. "vm-001" */
#define VM_NAME_MAX_LEN    128
#define VM_PATH_MAX_LEN    1024
#define VM_ARCH_MAX_LEN    32    /* e.g. "aarch64", "x86_64" */

/* Sensible host resource limits used during validation.
 * These are conservative defaults; validation can accept larger values when
 * the host has been explicitly configured to allow them. */
#define VM_MAX_CPUS        64
#define VM_MIN_MEM_MB      128
#define VM_MAX_MEM_MB      (64 * 1024)   /* 64 GiB */

/* ---------------------------------------------------------------- Lifecycle states
 *
 * A VM must pass through states in order.  Backward transitions are only
 * allowed on error or explicit reset.
 *
 *   CREATED → DISK_READY → STARTING → RUNNING → STOPPING → STOPPED
 *                                                          ↘ FAILED
 */
typedef enum {
    VM_STATE_CREATED   = 0,  /* Configuration record exists, disk not verified */
    VM_STATE_DISK_READY,     /* Guest image and disk are present and accessible */
    VM_STATE_STARTING,       /* QEMU process launch has been requested */
    VM_STATE_RUNNING,        /* QEMU process is alive (PID valid) */
    VM_STATE_STOPPING,       /* Graceful shutdown requested, waiting */
    VM_STATE_STOPPED,        /* Process has exited cleanly */
    VM_STATE_FAILED,         /* Process exited with error or was forcibly killed */
    VM_STATE_COUNT
} VmState;

/* Human-readable name for each state (never NULL). */
const char *vm_state_name(VmState s);

/* ---------------------------------------------------------------- Guest architecture */
typedef enum {
    VM_ARCH_AARCH64 = 0,  /* ARM 64-bit – native on Apple Silicon */
    VM_ARCH_X86_64,        /* x86-64 – fully emulated on ARM host */
    VM_ARCH_UNKNOWN,
    VM_ARCH_COUNT
} VmArch;

const char *vm_arch_name(VmArch a);   /* "aarch64", "x86_64", "unknown" */

/* Parse an architecture string.  Returns VM_ARCH_UNKNOWN if unrecognised. */
VmArch vm_arch_parse(const char *s);

/* ---------------------------------------------------------------- Display config */
typedef enum {
    VM_DISPLAY_NONE = 0,   /* No graphical output (headless / serial console) */
    VM_DISPLAY_COCOA,      /* macOS native Cocoa display window */
    VM_DISPLAY_SDL,        /* QEMU SDL display */
    VM_DISPLAY_VNC,        /* QEMU VNC server (bound to 127.0.0.1 for security) */
    VM_DISPLAY_COUNT
} VmDisplay;

const char *vm_display_name(VmDisplay d);
VmDisplay   vm_display_parse(const char *s);

/* ---------------------------------------------------------------- Network config */
typedef enum {
    VM_NET_NONE = 0,       /* No networking (isolated sandbox) */
    VM_NET_USER,           /* QEMU user-mode NAT (safe, no root, no exposed ports) */
    VM_NET_TAP,            /* TAP device (requires elevated privileges) */
    VM_NET_COUNT
} VmNet;

const char *vm_net_name(VmNet n);
VmNet       vm_net_parse(const char *s);

/* ---------------------------------------------------------------- Main configuration struct */
typedef struct {
    /* Identity */
    char id[VM_ID_MAX_LEN];           /* Unique VM identifier */
    char name[VM_NAME_MAX_LEN];       /* Human-readable display name */

    /* Hardware */
    VmArch arch;                      /* Guest CPU architecture */
    int    cpu_count;                 /* Number of virtual CPUs (≥1) */
    int    memory_mb;                 /* RAM in MiB (≥VM_MIN_MEM_MB) */

    /* Storage */
    char   os_image_path[VM_PATH_MAX_LEN];   /* Boot image (.iso / .img / .qcow2) */
    char   disk_image_path[VM_PATH_MAX_LEN]; /* Writeable disk image (may be empty) */
    char   work_dir[VM_PATH_MAX_LEN];        /* Working directory for QEMU process */

    /* Display and networking (optional) */
    VmDisplay display;
    VmNet     net;

    /* Lifecycle state (runtime – not persisted in raw form; db stores string) */
    VmState state;
} VmConfig;

/* ---------------------------------------------------------------- Validation */

/* Error codes returned by vm_config_validate(). */
typedef enum {
    VM_OK                   =  0,
    VM_ERR_INVALID_ID       = -1,   /* ID empty or too long */
    VM_ERR_INVALID_NAME     = -2,   /* Name empty */
    VM_ERR_INVALID_ARCH     = -3,   /* Architecture unknown */
    VM_ERR_INVALID_CPU      = -4,   /* cpu_count < 1 or > VM_MAX_CPUS */
    VM_ERR_INVALID_MEM      = -5,   /* memory_mb out of range */
    VM_ERR_MISSING_IMAGE    = -6,   /* os_image_path empty */
    VM_ERR_IMAGE_NOT_FOUND  = -7,   /* os_image_path does not exist on disk */
    VM_ERR_INVALID_WORKDIR  = -8,   /* work_dir empty */
    VM_ERR_WORKDIR_MISSING  = -9,   /* work_dir does not exist and cannot be created */
    VM_ERR_MISSING_QEMU     = -10,  /* QEMU executable not found */
    VM_ERR_DUPLICATE_ID     = -11,  /* Another VM with this ID already registered */
    VM_ERR_INVALID_DISK     = -12,  /* disk_image_path specified but not found */
    VM_ERR_INVALID_DISPLAY  = -13,  /* display mode invalid */
    VM_ERR_INVALID_NET      = -14,  /* network mode invalid */
    VM_ERR_UNSUPPORTED_FORMAT = -15, /* OS image format unsupported */
} VmConfigError;

/* Validate the configuration.
 *
 * @param cfg          Configuration to validate (read-only).
 * @param qemu_path    Path to the QEMU executable (may be NULL to skip check).
 * @param err_buf      Buffer to receive a human-readable error message.
 * @param err_buf_sz   Size of err_buf in bytes.
 *
 * Returns VM_OK (0) on success, or a negative VmConfigError code on failure.
 * err_buf is always NUL-terminated on return.
 */
int vm_config_validate(const VmConfig *cfg,
                       const char *qemu_path,
                       char *err_buf, size_t err_buf_sz);

/* Resolve an image path against working directory and parent paths if relative. */
int vm_config_resolve_path(const char *path, char *out_resolved, size_t out_sz);

/* Initialise a VmConfig with safe zero/default values. */
void vm_config_init(VmConfig *cfg);

#endif /* VOS_VM_CONFIG_H */
