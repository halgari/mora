#!/bin/bash
# Build MoraTestHarness.dll — standalone SKSE plugin for integration testing
# Links against mora_rt.lib (must be built first via build_rt_lib.sh)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/harness"
RT_LIB="$PROJECT_DIR/data/mora_rt.lib"
OUTPUT="$BUILD_DIR/MoraTestHarness.dll"

XWIN="$HOME/.xwin"

[ -d "$XWIN" ] || { echo "Error: xwin SDK not found at $XWIN"; exit 1; }
[ -f "$RT_LIB" ] || { echo "Error: mora_rt.lib not found. Run build_rt_lib.sh first."; exit 1; }
command -v clang-cl >/dev/null || { echo "Error: clang-cl not found"; exit 1; }
command -v lld-link >/dev/null || { echo "Error: lld-link not found"; exit 1; }

CFLAGS=(
    --target=x86_64-pc-windows-msvc
    /std:c++20
    /O2
    /EHsc
    /DWIN32
    /D_WIN32
    /DNOMINMAX
    -flto
    -imsvc "$XWIN/crt/include"
    -imsvc "$XWIN/sdk/include/ucrt"
    -imsvc "$XWIN/sdk/include/um"
    -imsvc "$XWIN/sdk/include/shared"
    -I "$PROJECT_DIR/include"
)

SOURCES=(
    src/harness/harness_plugin.cpp
    src/harness/weapon_dumper.cpp
    src/harness/ini_reader.cpp
    src/harness/tcp_listener.cpp
)

mkdir -p "$BUILD_DIR/obj"

# Compile
OBJ_FILES=()
for src in "${SOURCES[@]}"; do
    obj="$BUILD_DIR/obj/$(basename "$src" .cpp).obj"
    echo "  CC $src"
    clang-cl "${CFLAGS[@]}" /c "/Fo$obj" "$PROJECT_DIR/$src"
    OBJ_FILES+=("$obj")
done

# Link
echo "  LINK MoraTestHarness.dll"
lld-link \
    /dll \
    /out:"$OUTPUT" \
    /libpath:"$XWIN/crt/lib/x86_64" \
    /libpath:"$XWIN/sdk/lib/um/x86_64" \
    /libpath:"$XWIN/sdk/lib/ucrt/x86_64" \
    "${OBJ_FILES[@]}" \
    "$RT_LIB" \
    msvcrt.lib ucrt.lib vcruntime.lib kernel32.lib ws2_32.lib

echo ""
echo "Done: $OUTPUT ($(du -h "$OUTPUT" | awk '{print $1}'))"
file "$OUTPUT"
