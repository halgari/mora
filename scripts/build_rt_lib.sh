#!/bin/bash
# Build mora_rt.lib — static library for LTO linking into generated DLLs
# Replaces build_rt_bitcode.sh. The .lib contains LTO bitcode objects,
# so lld-link performs LTO at link time (same optimization wins).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/rt"
OUTPUT="$PROJECT_DIR/data/mora_rt.lib"

XWIN="$HOME/.xwin"

[ -d "$XWIN" ] || { echo "Error: xwin SDK not found at $XWIN"; exit 1; }
command -v clang-cl >/dev/null || { echo "Error: clang-cl not found"; exit 1; }
command -v llvm-lib >/dev/null || { echo "Error: llvm-lib not found"; exit 1; }

CLANG_FLAGS=(
    --target=x86_64-pc-windows-msvc
    /std:c++20
    /O2
    /DWIN32
    /D_WIN32
    /DNOMINMAX
    /EHsc
    -flto
    -imsvc "$XWIN/crt/include"
    -imsvc "$XWIN/sdk/include/ucrt"
    -imsvc "$XWIN/sdk/include/um"
    -imsvc "$XWIN/sdk/include/shared"
    -I "$PROJECT_DIR/include"
)

SOURCES=(
    src/rt/crc32.cpp
    src/rt/bst_hashmap.cpp
    src/rt/form_ops.cpp
    src/rt/plugin_entry.cpp
)

mkdir -p "$BUILD_DIR" "$(dirname "$OUTPUT")"

# Compile each source to .obj (with LTO bitcode embedded)
OBJ_FILES=()
for src in "${SOURCES[@]}"; do
    obj="$BUILD_DIR/$(basename "$src" .cpp).obj"
    echo "  CC $src"
    clang-cl "${CLANG_FLAGS[@]}" /c "/Fo$obj" "$PROJECT_DIR/$src"
    OBJ_FILES+=("$obj")
done

# Archive into .lib
echo "  LIB mora_rt.lib"
llvm-lib /out:"$OUTPUT" "${OBJ_FILES[@]}"

echo ""
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | awk '{print $1}'))"
