@echo off
REM ==============================================================================
REM Virtual OS Simulator - Build script for Windows (MinGW-w64 / MSYS2 gcc)
REM ==============================================================================
where gcc >nul 2>nul
if errorlevel 1 (
  echo Error: gcc was not found. Install MinGW-w64 or MSYS2 and add its bin folder to PATH.
  exit /b 1
)

if not exist build mkdir build

if not exist build\sqlite3.o (
  echo Compiling SQLite amalgamation (first time only)...
  gcc -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -c third_party\sqlite3.c -o build\sqlite3.o
  if errorlevel 1 exit /b 1
)

set CORE=src\core\scheduler.c src\core\memory.c src\core\resources.c src\core\util.c src\db\database.c

if "%1"=="cli" goto build_cli
if "%1"=="test" goto build_test

:build_gui
echo Compiling Native Windows GUI (VirtualOS.exe)...
gcc -std=gnu11 -O2 -Wall -Isrc -Ithird_party -mwindows -static -o VirtualOS.exe ^
  src\main.c src\gui\mainwindow.c src\gui\views.c src\gui\widgets.c src\gui\dialogs.c ^
  %CORE% build\sqlite3.o -lcomctl32 -lgdi32 -luser32
if errorlevel 1 exit /b 1
echo.
echo Built VirtualOS.exe successfully! Double-click to start.
exit /b 0

:build_cli
echo Compiling Interactive Terminal CLI (virtual_os_cli.exe)...
gcc -std=c11 -O2 -Wall -Isrc -Ithird_party -DNO_SDL -o virtual_os_cli.exe ^
  src\main.c src\cli\cli.c %CORE% build\sqlite3.o
if errorlevel 1 exit /b 1
echo.
echo Built virtual_os_cli.exe successfully! Run: virtual_os_cli.exe
exit /b 0

:build_test
echo Compiling and running automated unit tests...
gcc -std=c11 -O2 -Wall -Isrc -Ithird_party -g tests\test_core.c %CORE% build\sqlite3.o -lm -o build\test_core.exe
if errorlevel 1 exit /b 1
build\test_core.exe
exit /b 0
