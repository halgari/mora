#!/bin/bash
# Build mora_rt.lib — static library for linking into generated DLLs.
# Uses MSVC under wine (msvc-wine) to compile with CommonLibSSE-NG headers.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/rt"
OUTPUT="$PROJECT_DIR/data/mora_rt.lib"

MSVC="/opt/msvc/bin/x64"

[ -x "$MSVC/cl" ] || { echo "Error: MSVC not found at $MSVC (need msvc-wine)"; exit 1; }

MSVC_FLAGS=(
    /std:c++latest
    /O2
    /EHsc
    /utf-8
    /DWIN32
    /D_WIN32
    /DNOMINMAX
    /DSPDLOG_COMPILED_LIB
    /DENABLE_SKYRIM_SE=1
    /DENABLE_SKYRIM_AE=1
    /DSKSE_SUPPORT_XBYAK=0
    /I"$PROJECT_DIR/include"
    /I"$PROJECT_DIR/extern/CommonLibSSE-NG/include"
    /I"$PROJECT_DIR/extern/spdlog-shim"
    /FI"SKSE/Impl/PCH.h"
)

SOURCES=(
    src/rt/crc32.cpp
    src/rt/bst_hashmap.cpp
    src/rt/form_ops.cpp
    src/rt/patch_walker.cpp
    src/rt/plugin_entry.cpp
)

mkdir -p "$BUILD_DIR" "$(dirname "$OUTPUT")"

# Compile each source to .obj
OBJ_FILES=()
for src in "${SOURCES[@]}"; do
    obj="$BUILD_DIR/$(basename "$src" .cpp).obj"
    echo "  CC $src"
    "$MSVC/cl" "${MSVC_FLAGS[@]}" /c "/Fo$obj" "$PROJECT_DIR/$src"
    OBJ_FILES+=("$obj")
done

# Archive into .lib
echo "  LIB mora_rt.lib"
"$MSVC/lib" /out:"$OUTPUT" "${OBJ_FILES[@]}"

echo ""
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | awk '{print $1}'))"
