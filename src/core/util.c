#include "util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

int parse_int_list(const char *s, int *out, int max)
{
    int n = 0;
    const char *p = s;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p || n >= max || v < -1000000 || v > 1000000) return -1;
        out[n++] = (int)v;
        p = end;
        while (*p == ' ' || *p == '\t') p++;
        if (*p && *p != ',') return -1;
    }
    return n;
}

void get_default_db_path(const char *argv0, char *out, size_t outsz)
{
    char exe_dir[1024] = {0};
#if defined(_WIN32)
    GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir));
    char *slash = strrchr(exe_dir, '\\');
    if (!slash) slash = strrchr(exe_dir, '/');
#elif defined(__APPLE__)
    uint32_t size = sizeof(exe_dir);
    if (_NSGetExecutablePath(exe_dir, &size) != 0) {
        if (argv0) strncpy(exe_dir, argv0, sizeof(exe_dir) - 1);
    }
    char *slash = strrchr(exe_dir, '/');
#else
    ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len <= 0) {
        if (argv0) strncpy(exe_dir, argv0, sizeof(exe_dir) - 1);
    }
    char *slash = strrchr(exe_dir, '/');
#endif
    if (slash) {
        *slash = '\0';
        snprintf(out, outsz, "%s/virtual_os.db", exe_dir);
    } else {
        snprintf(out, outsz, "virtual_os.db");
    }
}
