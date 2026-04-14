#!/bin/bash
# Build MoraRuntime.dll from mora_rt.lib + CommonLibSSE.lib
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/runtime"
RT_LIB="$PROJECT_DIR/data/mora_rt.lib"
COMMONLIB="$PROJECT_DIR/extern/CommonLibSSE-NG/CommonLibSSE.lib"
OUTPUT="$BUILD_DIR/MoraRuntime.dll"

MSVC="/opt/msvc/bin/x64"

[ -f "$RT_LIB" ] || { echo "Error: mora_rt.lib not found. Run build_rt_lib.sh first."; exit 1; }
[ -f "$COMMONLIB" ] || { echo "Error: CommonLibSSE.lib not found. Run build_commonlib.sh first."; exit 1; }

mkdir -p "$BUILD_DIR"

echo "  LINK MoraRuntime.dll"
"$MSVC/link" \
    /dll \
    /out:"$OUTPUT" \
    /machine:x64 \
    /def:"$SCRIPT_DIR/MoraRuntime.def" \
    /wholearchive:"$RT_LIB" \
    "$COMMONLIB" \
    msvcrt.lib ucrt.lib vcruntime.lib kernel32.lib user32.lib advapi32.lib ole32.lib shell32.lib

echo ""
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | awk '{print $1}'))"
file "$OUTPUT"
