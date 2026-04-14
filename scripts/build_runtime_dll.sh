#!/bin/bash
# Build MoraRuntime.dll from mora_rt.lib
# Links the pre-compiled .lib into a shared DLL using lld-link
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/runtime"
RT_LIB="$PROJECT_DIR/data/mora_rt.lib"
OUTPUT="$BUILD_DIR/MoraRuntime.dll"

MSVC="/opt/msvc/bin/x64"

[ -f "$RT_LIB" ] || { echo "Error: mora_rt.lib not found. Run build_rt_lib.sh first."; exit 1; }

mkdir -p "$BUILD_DIR"

# Link using MSVC linker under wine
echo "  LINK MoraRuntime.dll"
"$MSVC/link" \
    /dll \
    /out:"$OUTPUT" \
    /machine:x64 \
    "$RT_LIB" \
    msvcrt.lib ucrt.lib vcruntime.lib kernel32.lib user32.lib

echo ""
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | awk '{print $1}'))"
file "$OUTPUT"
