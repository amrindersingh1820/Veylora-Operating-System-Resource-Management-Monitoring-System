cmake_minimum_required(VERSION 3.16)
project(VirtualOS_Simulator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(src third_party third_party/nuklear)

# SQLite library
add_library(sqlite3 STATIC third_party/sqlite3.c)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=0 SQLITE_OMIT_LOAD_EXTENSION)

# Core engine and database
set(CORE_SOURCES
    src/core/scheduler.c
    src/core/memory.c
    src/core/resources.c
    src/core/util.c
    src/db/database.c
)
add_library(vos_core STATIC ${CORE_SOURCES})
target_link_libraries(vos_core PUBLIC sqlite3)
if(NOT WIN32)
    target_link_libraries(vos_core PUBLIC m)
endif()

# Virtualization module
set(VIRT_SOURCES
    src/virtualization/vm_config.c
    src/virtualization/vm_manager.c
)
add_library(vos_virt STATIC ${VIRT_SOURCES})
target_link_libraries(vos_virt PUBLIC vos_core)

# Monitoring and Resource Management module
set(MON_SOURCES
    src/monitoring/host_monitor.c
    src/monitoring/agent_protocol.c
    src/monitoring/process_monitor.c
    src/monitoring/resource_limits.c
    src/monitoring/resource_manager.c
    src/monitoring/vm_monitor.c
    src/application/app_manager.c
)
add_library(vos_mon STATIC ${MON_SOURCES})
find_package(Threads REQUIRED)
target_link_libraries(vos_mon PUBLIC vos_virt vos_core Threads::Threads)

# Standalone CLI executable (Zero GUI dependencies)
add_executable(virtual_os_cli src/main.c src/cli/cli.c)
target_compile_definitions(virtual_os_cli PRIVATE NO_SDL)
target_link_libraries(virtual_os_cli PRIVATE vos_mon vos_virt vos_core)

# Tests executable (existing engine + database tests)
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE vos_core)
enable_testing()
add_test(NAME CoreEngineTests COMMAND test_core)

# VM module tests (no QEMU or guest OS required)
add_executable(test_vm tests/test_vm.c)
target_link_libraries(test_vm PRIVATE vos_virt)
add_test(NAME VmModuleTests COMMAND test_vm)

# Monitoring module tests
add_executable(test_monitoring tests/test_monitoring.c)
target_link_libraries(test_monitoring PRIVATE vos_mon vos_virt vos_core)
add_test(NAME MonitoringModuleTests COMMAND test_monitoring)

# Application Management module tests
add_executable(test_application_management tests/test_application_management.c)
target_link_libraries(test_application_management PRIVATE vos_mon vos_virt vos_core)
add_test(NAME ApplicationManagementTests COMMAND test_application_management)

# Find SDL2 for cross-platform GUI
find_package(SDL2 QUIET)

if(NOT SDL2_FOUND)
    # Check pkg-config fallback
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SDL2 QUIET sdl2)
    endif()
endif()

if(SDL2_FOUND OR DEFINED SDL2_INCLUDE_DIRS OR DEFINED SDL2_LIBRARIES)
    add_executable(virtual_os src/main.c src/gui_sdl/gui_sdl.c src/cli/cli.c)
    if(TARGET SDL2::SDL2)
        target_link_libraries(virtual_os PRIVATE SDL2::SDL2 vos_mon vos_virt vos_core)
    else()
        target_include_directories(virtual_os PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(virtual_os PRIVATE ${SDL2_LIBRARIES} vos_mon vos_virt vos_core)
    endif()
    message(STATUS "SDL2 found: 'virtual_os' graphical target enabled.")
else()
    # Check Homebrew paths on macOS
    if(APPLE AND EXISTS "/opt/homebrew/include/SDL2/SDL.h")
        add_executable(virtual_os src/main.c src/gui_sdl/gui_sdl.c src/cli/cli.c)
        target_include_directories(virtual_os PRIVATE /opt/homebrew/include/SDL2)
        target_link_libraries(virtual_os PRIVATE -L/opt/homebrew/lib -lSDL2 vos_mon vos_virt vos_core)
        message(STATUS "macOS Homebrew SDL2 found: 'virtual_os' target enabled.")
    else()
        message(STATUS "SDL2 not found: only 'virtual_os_cli' target will be built.")
    endif()
endif()

# Windows Native Win32 GUI target
if(WIN32)
    set(WIN32_GUI_SOURCES
        src/gui/mainwindow.c
        src/gui/views.c
        src/gui/widgets.c
        src/gui/dialogs.c
    )
    add_executable(VirtualOS_Win32 WIN32 src/main.c ${WIN32_GUI_SOURCES})
    target_link_libraries(VirtualOS_Win32 PRIVATE vos_core comctl32 gdi32 user32)
endif()
