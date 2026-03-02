#!/bin/bash
#
# Build libfridatrace_inject.dylib for iOS ARM64.
#
# Usage:
#   ./build.sh
#
# Output:
#   libfridatrace_inject.dylib (iOS ARM64)
#
# Deploy to jailbroken device:
#   scp libfridatrace_inject.dylib root@<device>:/usr/lib/
#   ssh root@<device> ldid -S /usr/lib/libfridatrace_inject.dylib
#
# Inject methods:
#   1. DYLD_INSERT_LIBRARIES=/usr/lib/libfridatrace_inject.dylib /path/to/app
#   2. Copy to /Library/MobileSubstrate/DynamicLibraries/ with .plist filter
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
GUM_DIR="$PROJECT_ROOT/frida-gum"
SDK_DIR="$GUM_DIR/deps/sdk-ios-arm64"
BUILD_DIR="$GUM_DIR/build"

# iOS SDK
IOS_SDK=$(xcrun --sdk iphoneos --show-sdk-path)
IOS_MIN_VERSION="15.0"

echo "=== Building FridaTrace Inject Dylib ==="

# Verify prerequisites
if [ ! -f "$BUILD_DIR/gum/libfrida-gum-1.0.a" ]; then
    echo "ERROR: frida-gum not built for iOS. Run:"
    echo "  cd $PROJECT_ROOT && ./setup.sh"
    echo "  cd frida-gum && ./configure --host=ios-arm64 -- -Dtests=disabled && make"
    exit 1
fi

echo "[1/2] Compiling..."
xcrun -sdk iphoneos clang \
    -arch arm64 \
    -isysroot "$IOS_SDK" \
    -miphoneos-version-min=$IOS_MIN_VERSION \
    -dynamiclib \
    -o "$SCRIPT_DIR/libfridatrace_inject.dylib" \
    "$SCRIPT_DIR/fridatrace_inject.c" \
    -I "$GUM_DIR" \
    -I "$GUM_DIR/gum" \
    -I "$GUM_DIR/gum/trace" \
    -I "$BUILD_DIR/gum" \
    -I "$SDK_DIR/include/glib-2.0" \
    -I "$SDK_DIR/lib/glib-2.0/include" \
    -I "$SDK_DIR/include/capstone" \
    "$BUILD_DIR/gum/libfrida-gum-1.0.a" \
    -L "$SDK_DIR/lib" \
    -lglib-2.0 -lgobject-2.0 -lgio-2.0 -lffi -lz \
    -lcapstone -lpcre2-8 -liconv -lcharset -lresolv \
    -framework Foundation -framework CoreFoundation \
    -dead_strip \
    -install_name /usr/lib/libfridatrace_inject.dylib

echo "[2/2] Verifying..."
file "$SCRIPT_DIR/libfridatrace_inject.dylib"
ls -lh "$SCRIPT_DIR/libfridatrace_inject.dylib" | awk '{print "  Size:", $5}'

echo ""
echo "=== Build Complete ==="
echo ""
echo "Deploy to jailbroken device:"
echo "  scp libfridatrace_inject.dylib root@<device-ip>:/usr/lib/"
echo "  ssh root@<device-ip> ldid -S /usr/lib/libfridatrace_inject.dylib"
echo ""
echo "Inject into a process:"
echo "  DYLD_INSERT_LIBRARIES=/usr/lib/libfridatrace_inject.dylib /path/to/app"
echo ""
echo "Or use MobileSubstrate:"
echo "  cp libfridatrace_inject.dylib /Library/MobileSubstrate/DynamicLibraries/"
echo "  # Create matching .plist filter (see README)"
