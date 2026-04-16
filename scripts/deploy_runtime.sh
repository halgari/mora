#!/bin/bash
# Deploy the Mora runtime DLL (and optionally a freshly-compiled patch
# set) into a Skyrim install. Auto-detects GOG first (preferred — no
# Steam DRM overhead, fastest turnaround) and falls back to Steam.
#
# Usage:
#   scripts/deploy_runtime.sh                 # just deploys the DLL
#   scripts/deploy_runtime.sh --compile       # also compiles test_data/example.mora
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

GOG_SKYRIM="$HOME/Games/gog/the-elder-scrolls-v-skyrim-special-edition/drive_c/GOG Games/Skyrim Anniversary Edition"
GOG_PREFIX="$HOME/Games/gog/the-elder-scrolls-v-skyrim-special-edition"
STEAM_SKYRIM="$HOME/.local/share/Steam/steamapps/common/Skyrim Special Edition"
STEAM_PREFIX="$HOME/.local/share/Steam/steamapps/compatdata/489830/pfx"
PROTON="$HOME/.local/share/Steam/compatibilitytools.d/GE-Proton10-34/files"

if [ -f "$GOG_SKYRIM/SkyrimSE.exe" ]; then
    SKYRIM="$GOG_SKYRIM"; PREFIX="$GOG_PREFIX"; FLAVOR="GOG"
elif [ -d "$STEAM_SKYRIM" ]; then
    SKYRIM="$STEAM_SKYRIM"; PREFIX="$STEAM_PREFIX"; FLAVOR="Steam"
else
    echo "Error: neither GOG nor Steam Skyrim found."
    echo "  GOG:   $GOG_SKYRIM"
    echo "  Steam: $STEAM_SKYRIM"
    exit 1
fi
echo "Target: $FLAVOR Skyrim at $SKYRIM"

DLL_PATH="$PROJECT_DIR/build/windows/x64/release/MoraRuntime.dll"
if [ ! -f "$DLL_PATH" ]; then
    echo "Error: MoraRuntime.dll not found at $DLL_PATH"
    echo "Build it with: xmake f -p windows -a x64 --toolchain=xwin-clang-cl -m release && xmake build mora_runtime"
    exit 1
fi

# Refresh the prefix's msvcp140.dll from GE-Proton's Wine copy when
# newer — the 2016 VS2015 redistributable that seeds most prefixes
# ships a pre-SRWLock std::mutex that mismatches our clang-cl+xwin
# ABI (see docs/src/cross-compile-windows.md, trap #3).
WINE_MSVCP="$PROTON/lib/wine/x86_64-windows/msvcp140.dll"
PFX_MSVCP="$PREFIX/drive_c/windows/system32/msvcp140.dll"
if [ -f "$WINE_MSVCP" ] && [ -f "$PFX_MSVCP" ] && [ "$WINE_MSVCP" -nt "$PFX_MSVCP" ]; then
    echo "Refreshing prefix msvcp140.dll from GE-Proton..."
    cp "$WINE_MSVCP" "$PFX_MSVCP"
fi

if [ "$1" = "--compile" ]; then
    MORA_BIN="$PROJECT_DIR/build/linux/x86_64/release/mora"
    [ -x "$MORA_BIN" ] || { echo "Error: $MORA_BIN not built (xmake build mora on linux)"; exit 1; }
    echo "Compiling Mora rules..."
    "$MORA_BIN" compile --no-color \
        --data-dir "$SKYRIM/Data" \
        "$PROJECT_DIR/test_data/example.mora"
fi

echo "Deploying..."
mkdir -p "$SKYRIM/Data/SKSE/Plugins"
cp "$DLL_PATH" "$SKYRIM/Data/SKSE/Plugins/MoraRuntime.dll"
echo "  -> Data/SKSE/Plugins/MoraRuntime.dll"

if [ -f "$PROJECT_DIR/MoraCache/mora_patches.bin" ]; then
    cp "$PROJECT_DIR/MoraCache/mora_patches.bin" "$SKYRIM/Data/SKSE/Plugins/mora_patches.bin"
    echo "  -> Data/SKSE/Plugins/mora_patches.bin"
fi

case "$FLAVOR" in
    GOG)   LOGS="$PREFIX/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition GOG/SKSE" ;;
    Steam) LOGS="$PREFIX/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SKSE" ;;
esac
echo ""
echo "Done. Launch via SKSE (scripts/test_runtime.sh) or skse64_loader.exe."
echo "Logs: $LOGS/{skse64.log, MoraRuntime.log}"
