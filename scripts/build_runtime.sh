#!/bin/bash
# Build Mora SKSE Runtime DLL via clang-cl cross-compilation
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/windows/x86_64"

XWIN="$HOME/.xwin"
COMMONLIB="$HOME/oss/CommonLibSSE/include"

# Verify toolchain
command -v clang-cl >/dev/null || { echo "Error: clang-cl not found"; exit 1; }
command -v lld-link >/dev/null || { echo "Error: lld-link not found"; exit 1; }
[ -d "$XWIN" ] || { echo "Error: xwin SDK not found at $XWIN"; exit 1; }

mkdir -p "$BUILD_DIR/obj"

# Common flags
CFLAGS=(
    --target=x86_64-pc-windows-msvc
    /std:c++20
    /EHsc
    /permissive-
    /W3
    /DWIN32
    /D_WINDOWS
    /DNOMINMAX
    -imsvc "$XWIN/crt/include"
    -imsvc "$XWIN/sdk/include/ucrt"
    -imsvc "$XWIN/sdk/include/um"
    -imsvc "$XWIN/sdk/include/shared"
    -I "$PROJECT_DIR/include"
)

# Add CommonLibSSE if available
if [ -d "$COMMONLIB" ]; then
    CFLAGS+=(-I "$COMMONLIB")
fi

LDFLAGS=(
    /libpath:"$XWIN/crt/lib/x86_64"
    /libpath:"$XWIN/sdk/lib/um/x86_64"
    /libpath:"$XWIN/sdk/lib/ucrt/x86_64"
    /dll
    /out:"$BUILD_DIR/MoraRuntime.dll"
)

# Source files — shared core + runtime
SOURCES=(
    # Core
    src/core/arena.cpp
    src/core/string_pool.cpp
    src/core/source_location.cpp
    # Data
    src/data/value.cpp
    src/data/indexed_relation.cpp
    src/data/schema_registry.cpp
    # Eval (no ESP-specific code)
    src/eval/fact_db.cpp
    src/eval/patch_set.cpp
    src/eval/phase_classifier.cpp
    src/eval/evaluator.cpp
    # Emit (readers only — writer not needed at runtime, but included for simplicity)
    src/emit/patch_reader.cpp
    src/emit/patch_writer.cpp
    src/emit/rt_writer.cpp
    src/emit/lock_file.cpp
    # AST types (needed by evaluator)
    src/ast/types.cpp
    src/ast/ast.cpp
    # Diagnostics (needed by dynamic_runner)
    src/diag/diagnostic.cpp
    # Runtime
    src/runtime/form_bridge.cpp
    src/runtime/patch_applier.cpp
    src/runtime/dynamic_runner.cpp
    src/runtime/plugin.cpp
)

echo "Building Mora SKSE Runtime DLL..."
echo "  Sources: ${#SOURCES[@]} files"
echo "  Target:  $BUILD_DIR/MoraRuntime.dll"

# Compile each source
OBJECTS=()
for src in "${SOURCES[@]}"; do
    obj="$BUILD_DIR/obj/$(basename "$src" .cpp).obj"
    echo "  CC $src"
    clang-cl "${CFLAGS[@]}" /c "/Fo$obj" "$PROJECT_DIR/$src"
    OBJECTS+=("$obj")
done

# Link
echo "  LINK MoraRuntime.dll"
lld-link "${OBJECTS[@]}" "${LDFLAGS[@]}"

echo ""
echo "Done: $BUILD_DIR/MoraRuntime.dll"
file "$BUILD_DIR/MoraRuntime.dll"
