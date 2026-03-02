#!/bin/bash
#
# FridaTrace Setup Script
#
# Clones frida-gum, applies patches, and copies the trace module files.
# Run this after cloning the FridaTrace repository.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FRIDA_GUM_DIR="$SCRIPT_DIR/frida-gum"
TRACE_SRC_DIR="$SCRIPT_DIR/src/trace"
PATCH_DIR="$SCRIPT_DIR/patches"

# Pinned frida-gum version - our patches are validated against this commit.
FRIDA_GUM_REPO="https://github.com/frida/frida-gum.git"
FRIDA_GUM_COMMIT="7804914181117f05d20d3b143f7c13935658777f"  # 2026-02-18

echo "=== FridaTrace Setup ==="
echo "  frida-gum pinned commit: ${FRIDA_GUM_COMMIT:0:10}"

# Step 1: Clone frida-gum at pinned version
if [ ! -d "$FRIDA_GUM_DIR" ]; then
    echo "[1/3] Cloning frida-gum at pinned version..."
    git clone "$FRIDA_GUM_REPO" "$FRIDA_GUM_DIR"
    cd "$FRIDA_GUM_DIR"
    git checkout "$FRIDA_GUM_COMMIT"
    cd "$SCRIPT_DIR"
else
    echo "[1/3] frida-gum already exists, verifying version..."
    CURRENT_COMMIT="$(cd "$FRIDA_GUM_DIR" && git rev-parse HEAD)"
    if [ "$CURRENT_COMMIT" != "$FRIDA_GUM_COMMIT" ]; then
        echo "  WARNING: Current commit ($CURRENT_COMMIT) differs from pinned ($FRIDA_GUM_COMMIT)"
        echo "  Patches may not apply cleanly. Consider removing frida-gum/ and re-running."
    else
        echo "  Version matches."
    fi
fi

# Step 2: Copy trace module files into frida-gum
echo "[2/3] Copying trace module files..."
mkdir -p "$FRIDA_GUM_DIR/gum/trace"
cp "$TRACE_SRC_DIR"/*.c "$TRACE_SRC_DIR"/*.h "$FRIDA_GUM_DIR/gum/trace/"
echo "  Copied $(ls "$TRACE_SRC_DIR"/*.c "$TRACE_SRC_DIR"/*.h | wc -l | tr -d ' ') files to frida-gum/gum/trace/"

# Step 3: Apply patches to Stalker source
echo "[3/3] Applying patches..."
cd "$FRIDA_GUM_DIR"
for patch in "$PATCH_DIR"/*.patch; do
    if [ -f "$patch" ]; then
        patch_name="$(basename "$patch")"
        if git apply --check "$patch" 2>/dev/null; then
            git apply "$patch"
            echo "  Applied: $patch_name"
        else
            echo "  Skipped (already applied or conflict): $patch_name"
        fi
    fi
done

# Step 4: Initialize submodules needed for build
echo "[4/4] Initializing submodules..."
cd "$FRIDA_GUM_DIR"
if [ ! -f releng/meson/meson.py ]; then
    git submodule update --init --recursive --depth 1
fi

echo ""
echo "=== Setup complete ==="
echo ""
echo "Project structure:"
echo "  frida-gum/gum/trace/     - Trace module (our code)"
echo "  frida-gum/gum/gumstalker.h - Modified (new API)"
echo "  frida-gum/gum/backend-arm64/gumstalker-arm64.c - Modified (inline trace)"
echo "  frida-gum/gum/meson.build  - Modified (added trace sources)"
echo ""
echo "Build for iOS ARM64:"
echo "  cd frida-gum"
echo "  ./configure --host=ios-arm64 -- -Dtests=disabled"
echo "  make"
echo ""
echo "Output: frida-gum/build/gum/libfrida-gum-1.0.a"
