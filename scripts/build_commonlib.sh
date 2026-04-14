#!/bin/bash
# Build CommonLibSSE-NG as a static library using MSVC under wine.
# This is a one-time build that produces extern/CommonLibSSE-NG/CommonLibSSE.lib
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
COMMONLIB_DIR="$PROJECT_DIR/extern/CommonLibSSE-NG"
BUILD_DIR="$PROJECT_DIR/build/commonlib"
OUTPUT="$COMMONLIB_DIR/CommonLibSSE.lib"

MSVC="/opt/msvc/bin/x64"

[ -x "$MSVC/cl" ] || { echo "Error: MSVC not found at $MSVC"; exit 1; }

MSVC_FLAGS=(
    /std:c++latest
    /O2
    /EHsc
    /utf-8
    /Zc:preprocessor
    /DWIN32
    /D_WIN32
    /DNOMINMAX
    /DSPDLOG_COMPILED_LIB
    /DENABLE_SKYRIM_SE=1
    /DENABLE_SKYRIM_AE=1
    /I"$COMMONLIB_DIR/include"
    /I"$PROJECT_DIR/extern/spdlog-shim"
    /FI"SKSE/Impl/PCH.h"
    /w  # suppress warnings for CommonLib code
)

mkdir -p "$BUILD_DIR"

# Find all .cpp files
SOURCES=($(find "$COMMONLIB_DIR/src" -name "*.cpp" | sort))
echo "Building CommonLibSSE-NG (${#SOURCES[@]} source files)..."

# Compile in parallel batches
OBJ_FILES=()
FAILED=0
for src in "${SOURCES[@]}"; do
    base=$(basename "$src" .cpp)
    # Prefix with relative path to avoid name collisions
    relpath="${src#$COMMONLIB_DIR/src/}"
    objname=$(echo "$relpath" | tr '/' '_' | sed 's/\.cpp$//')
    obj="$BUILD_DIR/$objname.obj"

    if [ -f "$obj" ] && [ "$obj" -nt "$src" ]; then
        OBJ_FILES+=("$obj")
        continue
    fi

    "$MSVC/cl" "${MSVC_FLAGS[@]}" /c "/Fo$obj" "$src" >/dev/null 2>&1 && {
        OBJ_FILES+=("$obj")
    } || {
        echo "  SKIP $relpath (compile error)"
        FAILED=$((FAILED + 1))
    }
done

echo "Compiled ${#OBJ_FILES[@]}/${#SOURCES[@]} files ($FAILED skipped)"

# Archive
echo "Creating CommonLibSSE.lib..."
"$MSVC/lib" /nologo /out:"$OUTPUT" "${OBJ_FILES[@]}"

echo ""
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | awk '{print $1}'))"
