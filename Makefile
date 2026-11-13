# ==============================================================================
# Virtual OS Simulator - Universal Cross-Platform Makefile
# Supports: macOS (Apple Silicon & Intel), Linux, and Windows (MinGW/MSYS2)
# ==============================================================================

CC       ?= gcc
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra -Isrc -Iinclude -Ithird_party -Ithird_party/nuklear
SQLFLAGS  = -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION

# Detect operating system
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows_NT)

# Source groups
CORE_SRC = src/core/scheduler.c src/core/memory.c src/core/resources.c src/core/util.c src/db/database.c
VIRT_SRC = src/virtualization/vm_config.c src/virtualization/vm_manager.c
MON_SRC  = src/monitoring/host_monitor.c src/monitoring/agent_protocol.c src/monitoring/process_monitor.c src/monitoring/resource_limits.c src/monitoring/vm_monitor.c src/monitoring/resource_manager.c
APP_SRC  = src/application/app_manager.c
API_SRC  = src/api/vos_api.c
CLI_SRC  = src/cli/cli.c
SDL_SRC  = src/gui_sdl/gui_sdl.c
WIN_SRC  = src/gui/mainwindow.c src/gui/views.c src/gui/widgets.c src/gui/dialogs.c

# SDL2 auto-detection via sdl2-config, pkg-config, or standard fallback paths
SDL_CONFIG := $(shell which sdl2-config 2>/dev/null)
PKG_CONFIG := $(shell which pkg-config 2>/dev/null)

ifneq ($(SDL_CONFIG),)
    SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
    SDL_LIBS   := $(shell $(SDL_CONFIG) --libs)
else ifneq ($(PKG_CONFIG),)
    SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
    SDL_LIBS   := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)
else
    ifeq ($(UNAME_S),Darwin)
        SDL_CFLAGS := -I/opt/homebrew/include/SDL2 -I/usr/local/include/SDL2 -D_THREAD_SAFE
        SDL_LIBS   := -L/opt/homebrew/lib -L/usr/local/lib -lSDL2
    else
        SDL_CFLAGS := -I/usr/include/SDL2 -D_THREAD_SAFE
        SDL_LIBS   := -lSDL2
    endif
endif

# Platform-specific targets and settings
ifeq ($(UNAME_S),Darwin)
    TARGET     := virtual_os
    TARGET_CLI := virtual_os_cli
    TARGET_EXT :=
    CLI_LIBS   := -lm -lpthread
    GUI_LIBS   := $(SDL_LIBS) -lm -lpthread
    all: $(TARGET)
else ifeq ($(UNAME_S),Linux)
    TARGET     := virtual_os
    TARGET_CLI := virtual_os_cli
    TARGET_EXT :=
    CLI_LIBS   := -lm -lpthread
    GUI_LIBS   := $(SDL_LIBS) -lm -lpthread
    all: $(TARGET)
else
    # Windows (MinGW, MSYS2, Cygwin)
    TARGET     := VirtualOS.exe
    TARGET_CLI := virtual_os_cli.exe
    TARGET_EXT := .exe
    CLI_LIBS   := -lpthread
    GUI_LIBS   := -lcomctl32 -lgdi32 -luser32 -lpthread
    all: $(TARGET)
endif

.PHONY: all gui sdl cli win32 dylib test test_vm test_monitoring test_resource_enforcement test_application_management test_api test_all guest_agent clean help

# SQLite amalgamation compilation
build/sqlite3.o: third_party/sqlite3.c
	@mkdir -p build
	$(CC) $(SQLFLAGS) -fPIC -c $< -o $@

# Dynamic Shared Library (for Flutter Dart FFI)
ifeq ($(UNAME_S),Darwin)
    SHARED_LIB := build/libvos_backend.dylib
    SHARED_FLAGS := -dynamiclib
else ifeq ($(UNAME_S),Linux)
    SHARED_LIB := build/libvos_backend.so
    SHARED_FLAGS := -shared
else
    SHARED_LIB := build/vos_backend.dll
    SHARED_FLAGS := -shared
endif

dylib: $(SHARED_LIB)

$(SHARED_LIB): $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) $(APP_SRC) $(API_SRC) guest_agent/src/cgroup_manager.c guest_agent/src/app_launcher.c build/sqlite3.o
	@mkdir -p build
	$(CC) $(CFLAGS) -Iguest_agent/include -fPIC $(SHARED_FLAGS) -o $@ $^ -lm -lpthread
	@echo "Built $(SHARED_LIB) successfully!"

# Primary Cross-Platform Target (SDL2 GUI + CLI)
$(TARGET): src/main.c $(SDL_SRC) $(CLI_SRC) $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) $(APP_SRC) build/sqlite3.o
	@mkdir -p build
ifeq ($(findstring Windows_NT,$(UNAME_S)),Windows_NT)
	$(CC) -std=gnu11 -O2 -Wall -Isrc -Ithird_party -mwindows -static -o $@ src/main.c $(WIN_SRC) $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) $(APP_SRC) build/sqlite3.o $(GUI_LIBS)
else
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ src/main.c $(SDL_SRC) $(CLI_SRC) $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) $(APP_SRC) build/sqlite3.o $(GUI_LIBS)
endif
	@echo "Built $@ successfully!"

# Explicit SDL2 GUI target (all platforms)
sdl: src/main.c $(SDL_SRC) $(CLI_SRC) $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) $(APP_SRC) build/sqlite3.o
	@mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DUSE_SDL -o virtual_os_sdl$(TARGET_EXT) src/main.c $(SDL_SRC) $(CLI_SRC) $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) $(APP_SRC) build/sqlite3.o $(SDL_LIBS) -lm -lpthread
	@echo "Built virtual_os_sdl$(TARGET_EXT) successfully!"

# Standalone CLI / Terminal UI target (zero GUI dependencies)
cli: src/main.c $(CLI_SRC) $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) $(APP_SRC) build/sqlite3.o
	@mkdir -p build
	$(CC) $(CFLAGS) -DNO_SDL -o $(TARGET_CLI) src/main.c $(CLI_SRC) $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) $(APP_SRC) build/sqlite3.o $(CLI_LIBS)
	@echo "Built $(TARGET_CLI) successfully!"

# Native Win32 GUI target (Windows or MinGW cross-compiler)
win32: src/main.c $(WIN_SRC) $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) $(APP_SRC) build/sqlite3.o
	@mkdir -p build
	$(CC) -std=gnu11 -O2 -Wall -Isrc -Ithird_party -mwindows -static -o VirtualOS.exe src/main.c $(WIN_SRC) $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) $(APP_SRC) build/sqlite3.o -lcomctl32 -lgdi32 -luser32 -lpthread
	@echo "Built VirtualOS.exe successfully!"

# Automated Unit Tests (existing engine + database)
test: build/sqlite3.o
	@mkdir -p build
	$(CC) $(CFLAGS) -g tests/test_core.c $(CORE_SRC) build/sqlite3.o -lm -o build/test_core
	./build/test_core

# VM module tests (no guest OS required)
test_vm: build/sqlite3.o
	@mkdir -p build
	$(CC) $(CFLAGS) -g tests/test_vm.c $(CORE_SRC) $(VIRT_SRC) build/sqlite3.o -lm -o build/test_vm
	./build/test_vm

# Monitoring and Resource Management tests
test_monitoring: build/sqlite3.o
	@mkdir -p build
	$(CC) $(CFLAGS) -Iguest_agent/include -g tests/test_monitoring.c $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) guest_agent/src/cpu_monitor.c guest_agent/src/memory_monitor.c guest_agent/src/disk_monitor.c guest_agent/src/network_monitor.c guest_agent/src/process_monitor.c guest_agent/src/metrics_common.c build/sqlite3.o -lm -lpthread -o build/test_monitoring
	./build/test_monitoring

# Phase 4 Resource Enforcement tests
test_resource_enforcement: build/sqlite3.o
	@mkdir -p build
	$(CC) $(CFLAGS) -Iguest_agent/include -g tests/test_resource_enforcement.c $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) guest_agent/src/cgroup_manager.c guest_agent/src/app_launcher.c build/sqlite3.o -lm -lpthread -o build/test_resource_enforcement
	./build/test_resource_enforcement

# Phase 5 Application Management tests
test_application_management: build/sqlite3.o
	@mkdir -p build
	$(CC) $(CFLAGS) -Iguest_agent/include -g tests/test_application_management.c $(CORE_SRC) $(VIRT_SRC) $(MON_SRC) $(APP_SRC) guest_agent/src/cgroup_manager.c guest_agent/src/app_launcher.c build/sqlite3.o -lm -lpthread -o build/test_application_management
	./build/test_application_management

# Phase 6 C-ABI Stable Interface & FFI Gateway tests
test_api: dylib
	@mkdir -p build
	$(CC) $(CFLAGS) -Iguest_agent/include -g tests/test_api.c $(SHARED_LIB) -lm -lpthread -o build/test_api
	./build/test_api

# Guest Agent target
guest_agent:
	$(MAKE) -C guest_agent

# Run Flutter macOS frontend
run_frontend: dylib
	cd frontend && DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer flutter run -d macos

# Run all tests
test_all: test test_vm test_monitoring test_resource_enforcement test_application_management test_api

# Clean build artifacts
clean:
	rm -rf build virtual_os virtual_os_cli virtual_os_sdl virtual_os_sdl.exe virtual_os.exe virtual_os_cli.exe
	$(MAKE) -C guest_agent clean

# Help menu
help:
	@echo "Virtual OS Simulator - Build Targets:"
	@echo "  make         - Builds default program for your OS (virtual_os or VirtualOS.exe)"
	@echo "  make sdl     - Builds cross-platform SDL2 GUI version"
	@echo "  make cli     - Builds standalone Interactive Terminal (CLI) version (zero GUI dependencies)"
	@echo "  make win32   - Builds native Win32 GUI (Windows only)"
	@echo "  make test    - Runs 256 automated engine and database tests"
	@echo "  make clean   - Removes compiled binaries and build cache"
