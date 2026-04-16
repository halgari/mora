#!/bin/bash
# Test MoraRuntime.dll against GOG Skyrim via GE-Proton.
# Launches Skyrim on Hyprland workspace 10, waits for patch application log.
#
# Prerequisites:
#   - GOG Skyrim AE at ~/Games/gog/the-elder-scrolls-v-skyrim-special-edition/
#   - GOG SKSE installed (skse64_2_02_06_gog.7z from skse.silverlock.org)
#   - Address Library: versionlib-1-6-1179-0.bin in Data/SKSE/Plugins/
#   - GE-Proton10-34 at ~/.local/share/Steam/compatibilitytools.d/
#   - Hyprland running with XWayland on :1
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

GOG_SKYRIM="$HOME/Games/gog/the-elder-scrolls-v-skyrim-special-edition/drive_c/GOG Games/Skyrim Anniversary Edition"
GOG_PREFIX="$HOME/Games/gog/the-elder-scrolls-v-skyrim-special-edition"
PROTON="$HOME/.local/share/Steam/compatibilitytools.d/GE-Proton10-34/files"
MORA_LOG="$GOG_PREFIX/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition GOG/SKSE/MoraRuntime.log"
SKSE_LOG="$GOG_PREFIX/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition GOG/SKSE/skse64.log"

[ -f "$GOG_SKYRIM/SkyrimSE.exe" ] || { echo "Error: GOG Skyrim not found"; exit 1; }
[ -f "$GOG_SKYRIM/skse64_loader.exe" ] || { echo "Error: SKSE not installed in GOG Skyrim"; exit 1; }
[ -f "$PROTON/bin/wine64" ] || { echo "Error: GE-Proton not found"; exit 1; }

# Prefixes seeded with the 2016-era VS2015 redistributable ship a
# msvcp140.dll whose std::mutex layout predates the SRWLock-based
# rewrite. Our clang-cl+xwin build targets the modern std::mutex
# ABI, where `_Mymtx` is zero-initialized and lazy-initialized on
# first lock; the old runtime's _Mtx_lock expects a pre-allocated
# SRWLock pointer, so the first lock inside `REL::Module::get()`
# dereferences null → 0xC0000005 inside MSVCP140!_Mtx_lock, which
# SKSE surfaces as "fatal error occurred while loading plugin".
# Swap in GE-Proton's bundled Wine msvcp140 (ABI-matched to current
# MSVC STL). Idempotent — only updates the prefix if the bundled
# copy is newer.
WINE_MSVCP="$PROTON/lib/wine/x86_64-windows/msvcp140.dll"
PFX_MSVCP="$GOG_PREFIX/drive_c/windows/system32/msvcp140.dll"
if [ -f "$WINE_MSVCP" ] && [ -f "$PFX_MSVCP" ] && [ "$WINE_MSVCP" -nt "$PFX_MSVCP" ]; then
    echo "Refreshing prefix msvcp140.dll from Wine (modern std::mutex ABI)..."
    cp "$WINE_MSVCP" "$PFX_MSVCP"
fi

# Deploy latest DLL from the xmake build output
DLL="$PROJECT_DIR/build/windows/x64/release/MoraRuntime.dll"
[ -f "$DLL" ] || { echo "Error: MoraRuntime.dll not found at $DLL"
                   echo "Build it with: xmake f -p windows -a x64 --toolchain=xwin-clang-cl -m release && xmake build mora_runtime"
                   exit 1; }
cp "$DLL" "$GOG_SKYRIM/Data/SKSE/Plugins/MoraRuntime.dll"

# Deploy patches if available
if [ -f "$1" ]; then
    cp "$1" "$GOG_SKYRIM/Data/SKSE/Plugins/mora_patches.bin"
    echo "Using patches: $1"
elif [ -f "$GOG_SKYRIM/Data/SKSE/Plugins/mora_patches.bin" ]; then
    echo "Using existing patches"
else
    echo "Warning: no mora_patches.bin found"
fi

# Clear old logs
rm -f "$MORA_LOG" "$SKSE_LOG"

# Environment
export WINEPREFIX="$GOG_PREFIX"
export DISPLAY=:1
export WAYLAND_DISPLAY=wayland-1
export XDG_RUNTIME_DIR=/run/user/$(id -u)

# Create launcher script (needed for hyprctl exec with complex commands)
cat > /tmp/mora_launch_skyrim.sh << LAUNCHER
#!/bin/bash
export WINEPREFIX="$GOG_PREFIX"
export DISPLAY=:1
cd "$GOG_SKYRIM"
"$PROTON/bin/wine64" skse64_loader.exe
LAUNCHER
chmod +x /tmp/mora_launch_skyrim.sh

# Find Hyprland instance for workspace dispatch
HYPR_SIG=$(ls /run/user/$(id -u)/hypr/ 2>/dev/null | head -1)
if [ -n "$HYPR_SIG" ]; then
    export HYPRLAND_INSTANCE_SIGNATURE="$HYPR_SIG"
    echo "Launching Skyrim on Hyprland workspace 10..."
    hyprctl dispatch exec "[workspace 10 silent]" /tmp/mora_launch_skyrim.sh >/dev/null 2>&1
else
    echo "Launching Skyrim..."
    /tmp/mora_launch_skyrim.sh 2>/dev/null &
fi

# Poll MoraRuntime.log for the "SKSE event hooks registered" message
# that our on_data_loaded handler writes at the very end. That line
# is proof that the entire DataLoaded path ran to completion without
# crashing — which is the ABI-compat test we actually care about.
# Whether patches applied or the DAG has nodes depends on what
# mora_patches.bin contained (a pure on/maintain rule set produces
# 0 static patches legitimately).
for i in $(seq 1 45); do
    sleep 2
    if [ -f "$MORA_LOG" ] && grep -q "SKSE event hooks registered\|No patches" "$MORA_LOG" 2>/dev/null; then
        echo ""
        echo "════════════════════════════════════════"
        cat "$MORA_LOG"
        echo "════════════════════════════════════════"
        echo ""
        "$PROTON/bin/wineserver" -k 2>/dev/null

        # ABI-compat pass criterion: the DataLoaded handler completed.
        if grep -q "SKSE event hooks registered" "$MORA_LOG"; then
            echo "PASS — plugin loaded, DataLoaded fired, on_data_loaded completed."
            # Bonus info: patches applied? (optional, not required for pass)
            if grep -qE "Applied [1-9][0-9]*" "$MORA_LOG"; then
                applied=$(grep -oE "Applied [0-9]+ patches" "$MORA_LOG" | head -1)
                echo "       $applied (mora_patches.bin contained static rules)"
            else
                echo "       (mora_patches.bin contained 0 static patches — data-only test, not a failure)"
            fi
            exit 0
        fi
        echo "FAIL — DataLoaded observed but on_data_loaded did not finish."
        exit 1
    fi
    echo -n "."
done

echo ""
echo "TIMEOUT — DataLoaded did not fire within 90s"
echo "SKSE log:"
cat "$SKSE_LOG" 2>/dev/null | tail -10
"$PROTON/bin/wineserver" -k 2>/dev/null
exit 1
