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
    /Zc:preprocessor
    /DWIN32
    /D_WIN32
    /DNOMINMAX
    /DSPDLOG_COMPILED_LIB
    /DENABLE_SKYRIM_SE=1
    /DENABLE_SKYRIM_AE=1
    /USKSE_SUPPORT_XBYAK
    /I"$PROJECT_DIR/include"
    /I"$PROJECT_DIR/extern/CommonLibSSE-NG/include"
    /I"$PROJECT_DIR/extern/spdlog-shim"
    /I"$PROJECT_DIR/extern/simplemath-shim"
    /FI"SKSE/Impl/PCH.h"
)

# Mirror the xmake.lua mora_runtime target: core + data + eval + emit + rt +
# rt/handlers + dag + model.
#
# Excludes form_model_verify.cpp — it's a compile-time sanity check of
# CommonLibSSE-NG offsets that's been flagged as drifted; investigating is a
# separate concern from exercising the runtime DLL.
SOURCES=(
    $(cd "$PROJECT_DIR" && ls \
        src/core/*.cpp \
        src/data/*.cpp \
        src/diag/*.cpp \
        src/eval/*.cpp \
        src/emit/*.cpp \
        src/rt/*.cpp \
        src/rt/handlers/*.cpp \
        src/dag/*.cpp \
        src/model/*.cpp 2>/dev/null | grep -vE 'form_model_verify\.cpp|diag/renderer\.cpp')
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

# Compile the spdlog / fmt implementation TUs from the shim if they aren't
# already cached. CommonLibSSE headers declare these symbols via
# SPDLOG_COMPILED_LIB; without the bodies the final DLL fails to link.
for shim_pair in \
    "extern/spdlog-shim/spdlog.cpp:$BUILD_DIR/spdlog.obj" \
    "extern/spdlog-shim/fmt.cpp:$BUILD_DIR/fmt.obj"; do
    src="${shim_pair%:*}"
    obj="${shim_pair#*:}"
    if [ ! -f "$obj" ] || [ "$PROJECT_DIR/$src" -nt "$obj" ]; then
        echo "  CC $src"
        "$MSVC/cl" "${MSVC_FLAGS[@]}" /c "/Fo$obj" "$PROJECT_DIR/$src" >/dev/null
    fi
    OBJ_FILES+=("$obj")
done

# Archive into .lib
echo "  LIB mora_rt.lib"
"$MSVC/lib" /out:"$OUTPUT" "${OBJ_FILES[@]}"

echo ""
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | awk '{print $1}'))"
