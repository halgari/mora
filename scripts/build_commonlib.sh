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
    /I"$PROJECT_DIR/extern/simplemath-shim"
    /FI"SKSE/Impl/PCH.h"
    /w  # suppress warnings for CommonLib code
)

mkdir -p "$BUILD_DIR"

# Find all .cpp files
SOURCES=($(find "$COMMONLIB_DIR/src" -name "*.cpp" | sort))
echo "Building CommonLibSSE-NG (${#SOURCES[@]} source files)..."

# Parallel compile via xargs. JOBS defaults to nproc but capped at 16 —
# wine-msvc has some per-process fixed overhead and throughput plateaus
# above ~16 workers on most boxes.
JOBS="${JOBS:-$(( $(nproc) < 16 ? $(nproc) : 16 ))}"
echo "  using $JOBS parallel workers"

# Emit a tab-separated (src, obj) list so xargs can pass both to a worker.
JOBLIST=$(mktemp)
for src in "${SOURCES[@]}"; do
    relpath="${src#$COMMONLIB_DIR/src/}"
    objname=$(echo "$relpath" | tr '/' '_' | sed 's/\.cpp$//')
    obj="$BUILD_DIR/$objname.obj"
    # Skip already-current objs
    if [ -f "$obj" ] && [ "$obj" -nt "$src" ]; then
        continue
    fi
    printf '%s\t%s\n' "$src" "$obj" >> "$JOBLIST"
done

TO_BUILD=$(wc -l < "$JOBLIST")
echo "  ($TO_BUILD files need compiling; the rest are cached)"

if [ "$TO_BUILD" -gt 0 ]; then
    # Export env the worker needs.
    export MSVC
    MSVC_FLAGS_STR=$(printf '%q ' "${MSVC_FLAGS[@]}")
    export MSVC_FLAGS_STR
    export BUILD_LOG="$BUILD_DIR/compile.log"
    : > "$BUILD_LOG"

    # Worker reads "src<TAB>obj", runs cl, logs errors on failure.
    awk -F'\t' '{print $1 "\t" $2}' "$JOBLIST" | \
        xargs -n1 -P"$JOBS" -I{} bash -c '
            line="{}"
            src="${line%%$'"'"'\t'"'"'*}"
            obj="${line##*$'"'"'\t'"'"'}"
            rel="${src#'"$COMMONLIB_DIR"'/src/}"
            if eval "\"$MSVC/cl\" $MSVC_FLAGS_STR /c /Fo\"$obj\" \"$src\"" >/dev/null 2>>"$BUILD_LOG"; then
                :
            else
                echo "  SKIP $rel" >&2
            fi
        '
fi

# Re-walk the source list to assemble the final .obj list from whatever
# survived (either cached or just-built).
OBJ_FILES=()
for src in "${SOURCES[@]}"; do
    relpath="${src#$COMMONLIB_DIR/src/}"
    objname=$(echo "$relpath" | tr '/' '_' | sed 's/\.cpp$//')
    obj="$BUILD_DIR/$objname.obj"
    [ -f "$obj" ] && OBJ_FILES+=("$obj")
done
FAILED=$((${#SOURCES[@]} - ${#OBJ_FILES[@]}))

echo "Compiled ${#OBJ_FILES[@]}/${#SOURCES[@]} files ($FAILED skipped)"
rm -f "$JOBLIST"

# Archive
echo "Creating CommonLibSSE.lib..."
"$MSVC/lib" /nologo /out:"$OUTPUT" "${OBJ_FILES[@]}"

echo ""
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | awk '{print $1}'))"
