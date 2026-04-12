#!/bin/bash
# Build mora_rt.bc — runtime support bitcode for LTO linking into generated DLLs
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT="$PROJECT_DIR/data/mora_rt.bc"

XWIN="$HOME/.xwin"

CLANG_FLAGS=(
    -target x86_64-pc-windows-msvc
    -isystem "$XWIN/crt/include"
    -isystem "$XWIN/sdk/include/ucrt"
    -isystem "$XWIN/sdk/include/um"
    -isystem "$XWIN/sdk/include/shared"
    -I "$PROJECT_DIR/include"
    -std=c++20 -O2 -DWIN32 -DNOMINMAX -D_WIN32 -fno-exceptions
    -emit-llvm -c
)

SOURCES=(
    src/rt/crc32.cpp
    src/rt/bst_hashmap.cpp
    src/rt/form_ops.cpp
    src/rt/patch_walker.cpp
    src/rt/plugin_entry.cpp
)

mkdir -p "$(dirname "$OUTPUT")"

# Compile each source to bitcode
BC_FILES=()
for src in "${SOURCES[@]}"; do
    bc="/tmp/mora_rt_$(basename "$src" .cpp).bc"
    echo "  BC $src"
    clang "${CLANG_FLAGS[@]}" -o "$bc" "$PROJECT_DIR/$src"
    BC_FILES+=("$bc")
done

# Link into single bitcode file
echo "  LINK mora_rt.bc"
llvm-link "${BC_FILES[@]}" -o "$OUTPUT"

# Cleanup
rm -f "${BC_FILES[@]}"

echo ""
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | awk '{print $1}'))"
