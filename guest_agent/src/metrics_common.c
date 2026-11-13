/* metrics_common.c – Shared Utilities for In-Guest Procfs Sampling
 *
 * Provides configurable procfs root resolution for pure Linux procfs access.
 * Strictly guarantees that host APIs are never invoked for guest telemetry.
 */

#include "metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_custom_proc_root[256] = "";

const char *agent_get_proc_root(void)
{
    if (s_custom_proc_root[0] != '\0') {
        return s_custom_proc_root;
    }
    const char *env = getenv("VOS_PROC_ROOT");
    if (env && env[0] != '\0') {
        return env;
    }
    return "/proc";
}

void agent_set_proc_root(const char *root)
{
    if (root && root[0] != '\0') {
        snprintf(s_custom_proc_root, sizeof(s_custom_proc_root), "%s", root);
    } else {
        s_custom_proc_root[0] = '\0';
    }
}

