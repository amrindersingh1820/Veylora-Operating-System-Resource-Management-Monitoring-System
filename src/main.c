/* Virtual OS Simulator - Universal cross-platform entry point.
 *
 * Routes to:
 *   - SDL2 + Nuklear graphical UI when compiled without NO_SDL (macOS, Linux, Windows/MinGW)
 *   - Interactive Terminal (CLI) when --cli flag is passed or NO_SDL is defined
 *   - Native Win32 GUI when compiled on Windows without SDL2 (legacy fallback)
 *
 * Platform notes:
 *   macOS / Linux : clang/gcc, standard int main()
 *   Windows SDL2  : MinGW – SDL.h redefines main; WinMain is not used
 *   Windows Win32 : MSVC / MinGW with -mwindows – WinMain entry below
 */

#include "db/database.h"
#include "core/util.h"
#include "virtualization/vm_manager.h"
#include "cli/cli.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ forward decls */
#if !defined(NO_SDL)
int gui_sdl_run(int argc, char **argv);
#endif

/* ================================================================= macOS / Linux */
#if !defined(_WIN32)

int main(int argc, char **argv)
{
    /* Resolve database path relative to executable */
    char db_path[1024];
    get_default_db_path(argv[0], db_path, sizeof(db_path));

    if (db_open(db_path) != 0) {
        fprintf(stderr, "Virtual OS Simulator: database error: %s\n", db_error());
        return 1;
    }

    /* Virtualization: initialize VM database and reconcile any stale running states */
    vm_manager_set_db_path(db_path);
    vm_manager_reconcile_db(NULL);

    int rc;

    /* Check for direct CLI subcommands: e.g. "resource", "app" */
    if (argc >= 2 && (strcmp(argv[1], "resource") == 0 || strcmp(argv[1], "--resource") == 0 ||
                      strcmp(argv[1], "app") == 0 || strcmp(argv[1], "--app") == 0)) {
        rc = cli_handle_command(argc, argv);
        db_close();
        return rc;
    }

#if defined(NO_SDL)
    /* CLI-only build */
    (void)argc;
    rc = cli_run();
#else
    /* SDL GUI build: fall back to CLI if --cli flag is passed */
    if (argc >= 2 && strcmp(argv[1], "--cli") == 0) {
        rc = cli_run();
    } else {
        rc = gui_sdl_run(argc, argv);
    }
#endif

    db_close();
    return rc;
}

/* ================================================================= Windows */
#else  /* _WIN32 */

#if defined(NO_SDL)
/* ---- Windows CLI build: standard console entry ---- */
#include <stdlib.h>
int main(int argc, char **argv)
{
    char db_path[512];
    get_default_db_path(argv[0], db_path, sizeof(db_path));
    if (db_open(db_path) != 0) {
        fprintf(stderr, "database error: %s\n", db_error());
        return 1;
    }
    int rc = (argc >= 2 && strcmp(argv[1], "--cli") == 0)
             ? cli_run() : cli_run();
    db_close();
    return rc;
}

#elif defined(USE_SDL)
/* ---- Windows SDL2 build (MinGW / MSYS2): SDL redefines main ---- */
int main(int argc, char **argv)
{
    char db_path[512];
    get_default_db_path(argv[0], db_path, sizeof(db_path));
    if (db_open(db_path) != 0) {
        fprintf(stderr, "database error: %s\n", db_error());
        return 1;
    }
    int rc;
    if (argc >= 2 && strcmp(argv[1], "--cli") == 0)
        rc = cli_run();
    else
        rc = gui_sdl_run(argc, argv);
    db_close();
    return rc;
}

#else
/* ---- Windows native Win32 build (MSVC / -mwindows MinGW) ---- */
#include <windows.h>
#include <commctrl.h>

int gui_run(HINSTANCE hi, int show);   /* declared in src/gui/mainwindow.c */

int WINAPI WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev; (void)cmd;
    INITCOMMONCONTROLSEX ic = {
        sizeof ic,
        ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES
    };
    InitCommonControlsEx(&ic);

    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    strcpy(slash ? slash + 1 : path, "virtual_os.db");

    if (db_open(path) != 0) {
        MessageBoxA(NULL, db_error(),
                    "Virtual OS Simulator - database error", MB_ICONERROR);
        return 1;
    }
    int rc = gui_run(hi, show);
    db_close();
    return rc;
}
#endif /* NO_SDL / USE_SDL / Win32 */

#endif /* _WIN32 */