#!/usr/bin/env bash
# ==============================================================================
# Virtual OS Simulator - Build & Run Script for macOS & Linux
# ==============================================================================
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "========================================================"
echo " Building Virtual OS Simulator (Multi-Platform Edition)  "
echo "========================================================"

# Detect Compiler
CC="${CC:-gcc}"
if ! command -v "$CC" >/dev/null 2>&1; then
    if command -v clang >/dev/null 2>&1; then
        CC=clang
    else
        echo "Error: Neither gcc nor clang was found."
        echo "  - On macOS: Run 'xcode-select --install'"
        echo "  - On Ubuntu/Debian: Run 'sudo apt update && sudo apt install build-essential'"
        echo "  - On Fedora: Run 'sudo dnf groupinstall \"Development Tools\"'"
        exit 1
    fi
fi
echo "Using compiler: $CC ($($CC --version | head -n 1))"

# Check for SDL2
HAS_SDL=0
SDL_CFLAGS=""
SDL_LIBS=""

if command -v sdl2-config >/dev/null 2>&1; then
    HAS_SDL=1
    SDL_CFLAGS="$(sdl2-config --cflags)"
    SDL_LIBS="$(sdl2-config --libs)"
elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2; then
    HAS_SDL=1
    SDL_CFLAGS="$(pkg-config --cflags sdl2)"
    SDL_LIBS="$(pkg-config --libs sdl2)"
elif [ -d "/opt/homebrew/include/SDL2" ]; then
    HAS_SDL=1
    SDL_CFLAGS="-I/opt/homebrew/include/SDL2 -D_THREAD_SAFE"
    SDL_LIBS="-L/opt/homebrew/lib -lSDL2"
elif [ -d "/usr/include/SDL2" ]; then
    HAS_SDL=1
    SDL_CFLAGS="-I/usr/include/SDL2 -D_THREAD_SAFE"
    SDL_LIBS="-lSDL2"
fi

mkdir -p build

# 1. Compile SQLite amalgamation if not yet compiled
if [ ! -f "build/sqlite3.o" ]; then
    echo "Compiling SQLite (first time only)..."
    $CC -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -c third_party/sqlite3.c -o build/sqlite3.o
fi

# 2. Build the Simulator
CORE_SRC="src/core/scheduler.c src/core/memory.c src/core/resources.c src/core/util.c src/db/database.c"

if [ "$HAS_SDL" -eq 1 ]; then
    echo "SDL2 detected: Building full Graphical (Nuklear + SDL2) + CLI Simulator..."
    $CC -std=c11 -O2 -Wall -Wextra -Isrc -Ithird_party -Ithird_party/nuklear $SDL_CFLAGS \
        src/main.c src/gui_sdl/gui_sdl.c src/cli/cli.c $CORE_SRC build/sqlite3.o \
        $SDL_LIBS -lm -o virtual_os
    echo ""
    echo "SUCCESS: Built 'virtual_os' (Nuklear GUI edition)!"
    echo "  - Run './virtual_os' to launch the graphical desktop interface."
    echo "  - Run './virtual_os --cli' to launch the interactive terminal interface."
else
    echo "Warning: SDL2 not detected. Building Interactive Terminal (CLI) edition..."
    echo "  (To enable the graphical interface:"
    echo "     macOS: brew install sdl2"
    echo "     Ubuntu/Debian: sudo apt install libsdl2-dev"
    echo "     Fedora: sudo dnf install SDL2-devel)"
    $CC -std=c11 -O2 -Wall -Wextra -Isrc -Ithird_party -DNO_SDL \
        src/main.c src/cli/cli.c $CORE_SRC build/sqlite3.o \
        -lm -o virtual_os
    echo ""
    echo "SUCCESS: Built 'virtual_os' (CLI edition)!"
    echo "  - Run './virtual_os' to launch the interactive terminal simulator."
fi

# Optional auto-launch
if [ "$1" = "--run" ] || [ "$1" = "-r" ]; then
    shift
    exec ./virtual_os "$@"
fi
