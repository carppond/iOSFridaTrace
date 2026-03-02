#!/bin/bash
#
# Copies compiled frida-gum libraries and headers into the demo project.
# Run this after building frida-gum for iOS ARM64.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
GUM_DIR="$PROJECT_ROOT/frida-gum"
SDK_DIR="$GUM_DIR/deps/sdk-ios-arm64"
BUILD_DIR="$GUM_DIR/build"
LIBS_DIR="$SCRIPT_DIR/libs"

echo "=== Preparing FridaTrace Demo Libraries ==="

# Create output directories
mkdir -p "$LIBS_DIR/lib" "$LIBS_DIR/include/gum/trace"

# Copy frida-gum static library
echo "[1/4] Copying frida-gum library..."
cp "$BUILD_DIR/gum/libfrida-gum-1.0.a" "$LIBS_DIR/lib/"

# Copy core SDK libraries (minimal set needed)
echo "[2/4] Copying SDK dependencies..."
for lib in libglib-2.0.a libgobject-2.0.a libgthread-2.0.a \
           libffi.a libcapstone.a libpcre2-8.a libiconv.a libcharset.a libz.a; do
    if [ -f "$SDK_DIR/lib/$lib" ]; then
        cp "$SDK_DIR/lib/$lib" "$LIBS_DIR/lib/"
    fi
done

# Copy headers
echo "[3/4] Copying headers..."

# GLib headers
cp -r "$SDK_DIR/include/glib-2.0" "$LIBS_DIR/include/"
cp -r "$SDK_DIR/lib/glib-2.0" "$LIBS_DIR/lib/" 2>/dev/null || true
# GLib internal config header
if [ -d "$SDK_DIR/lib/glib-2.0/include" ]; then
    cp -r "$SDK_DIR/lib/glib-2.0/include/"* "$LIBS_DIR/include/glib-2.0/" 2>/dev/null || true
fi

# capstone headers
mkdir -p "$LIBS_DIR/include"
cp -r "$SDK_DIR/include/capstone" "$LIBS_DIR/include/" 2>/dev/null || true

# frida-gum headers - copy all .h files to avoid missing includes
find "$GUM_DIR/gum" -maxdepth 1 -name "*.h" -exec cp {} "$LIBS_DIR/include/gum/" \;

# Generated headers
cp "$BUILD_DIR/gum/gumenumtypes.h" "$LIBS_DIR/include/gum/"

# Architecture-specific headers
for arch_dir in "$GUM_DIR/gum/arch-"*; do
    dir_name=$(basename "$arch_dir")
    mkdir -p "$LIBS_DIR/include/gum/$dir_name"
    find "$arch_dir" -maxdepth 1 -name "*.h" -exec cp {} "$LIBS_DIR/include/gum/$dir_name/" \;
done

# Backend headers (needed by some gum headers)
mkdir -p "$LIBS_DIR/include/gum/backend-darwin"
find "$GUM_DIR/gum/backend-darwin" -maxdepth 1 -name "*.h" -exec cp {} "$LIBS_DIR/include/gum/backend-darwin/" \; 2>/dev/null || true

# Our trace headers
mkdir -p "$LIBS_DIR/include/gum/trace"
cp "$GUM_DIR/gum/trace/gumtrace.h" "$LIBS_DIR/include/gum/trace/"
cp "$GUM_DIR/gum/trace/gumtracerecorder.h" "$LIBS_DIR/include/gum/trace/"

echo "[4/4] Verifying..."
echo "  Libraries:"
ls -lh "$LIBS_DIR/lib/"*.a | awk '{print "    " $NF " (" $5 ")"}'
echo "  Headers:"
find "$LIBS_DIR/include" -name "*.h" | wc -l | xargs echo "   " "header files"

echo ""
echo "=== Done ==="
echo "Add the following to your Xcode project:"
echo "  Header Search Paths:  \$(PROJECT_DIR)/libs/include"
echo "  Library Search Paths: \$(PROJECT_DIR)/libs/lib"
echo "  Other Linker Flags:   -lfrida-gum-1.0 -lglib-2.0 -lgobject-2.0"
echo "                        -lgthread-2.0 -lffi -lcapstone -lpcre2-8"
echo "                        -liconv -lcharset -lz -lresolv"
