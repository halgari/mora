#!/bin/bash
# Deploy Mora runtime to Skyrim for testing
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SKYRIM_DATA="$HOME/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data"

if [ ! -d "$SKYRIM_DATA" ]; then
    echo "Error: Skyrim Data directory not found at $SKYRIM_DATA"
    exit 1
fi

DLL_PATH="$PROJECT_DIR/build/windows/x86_64/MoraRuntime.dll"
if [ ! -f "$DLL_PATH" ]; then
    echo "Error: MoraRuntime.dll not found. Run scripts/build_runtime.sh first."
    exit 1
fi

# Compile .mora rules against live ESP data
echo "Compiling Mora rules..."
"$PROJECT_DIR/build/linux/x86_64/release/mora" compile --no-color \
    --data-dir "$SKYRIM_DATA" \
    "$PROJECT_DIR/test_data/example.mora"

# Copy runtime DLL
echo ""
echo "Deploying to Skyrim..."
mkdir -p "$SKYRIM_DATA/SKSE/Plugins"
cp "$DLL_PATH" "$SKYRIM_DATA/SKSE/Plugins/MoraRuntime.dll"
echo "  -> SKSE/Plugins/MoraRuntime.dll"

# Copy MoraCache
if [ -d "$PROJECT_DIR/test_data/MoraCache" ]; then
    cp -r "$PROJECT_DIR/test_data/MoraCache" "$SKYRIM_DATA/"
    echo "  -> MoraCache/mora.patch"
    echo "  -> MoraCache/mora.rt"
    echo "  -> MoraCache/mora.lock"
fi

echo ""
echo "Done! Launch Skyrim with SKSE to test."
echo "Check SKSE logs at: $SKYRIM_DATA/../SKSE/skse64.log"
