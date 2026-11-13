#!/usr/bin/env bash
# Virtual OS Simulator - Frontend Launcher
# Sets the Xcode developer directory automatically to bypass macOS command-line tools pointer
export DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer"

# Ensure flutter is on PATH if installed in user home
if ! command -v flutter &> /dev/null; then
    if [ -d "$HOME/flutter/bin" ]; then
        export PATH="$HOME/flutter/bin:$PATH"
    fi
fi

# Ensure backend dynamic library exists
if [ ! -f "../build/libvos_backend.dylib" ]; then
    echo "Building backend dynamic library libvos_backend.dylib..."
    make -C .. dylib
fi

# Ensure vm_images symlink exists in frontend directory
if [ ! -e "vm_images" ] && [ -d "../vm_images" ]; then
    ln -s ../vm_images vm_images 2>/dev/null || true
fi

# Pre-stage Frameworks directory and AppIcon in both Veylora.app and virtual_os_simulator.app
mkdir -p "build/macos/Build/Products/Debug/Veylora.app/Contents/Frameworks" 2>/dev/null || true
cp -f ../build/libvos_backend.dylib "build/macos/Build/Products/Debug/Veylora.app/Contents/Frameworks/" 2>/dev/null || true
if [ -f "macos/Runner/AppIcon.icns" ]; then
    cp -f "macos/Runner/AppIcon.icns" "build/macos/Build/Products/Debug/Veylora.app/Contents/Resources/AppIcon.icns" 2>/dev/null || true
fi
mkdir -p "build/macos/Build/Products/Debug/virtual_os_simulator.app/Contents/Frameworks" 2>/dev/null || true
cp -f ../build/libvos_backend.dylib "build/macos/Build/Products/Debug/virtual_os_simulator.app/Contents/Frameworks/" 2>/dev/null || true

exec flutter run -d macos "$@"
