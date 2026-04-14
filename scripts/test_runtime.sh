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

# Deploy latest DLL
DLL="$PROJECT_DIR/build/rt-clang/MoraRuntime.dll"
[ -f "$DLL" ] || { echo "Error: MoraRuntime.dll not built. Run the clang-cl build first."; exit 1; }
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

# Wait for patches to be applied (typically ~20s for DataLoaded)
for i in $(seq 1 45); do
    sleep 2
    if [ -f "$MORA_LOG" ] && grep -q "Applied\|No patches" "$MORA_LOG" 2>/dev/null; then
        echo ""
        echo "════════════════════════════════════════"
        cat "$MORA_LOG"
        echo "════════════════════════════════════════"
        echo ""
        "$PROTON/bin/wineserver" -k 2>/dev/null
        # Check for success
        if grep -q "Applied.*patches" "$MORA_LOG"; then
            echo "PASS"
            exit 0
        else
            echo "FAIL — patches not applied"
            exit 1
        fi
    fi
    echo -n "."
done

echo ""
echo "TIMEOUT — DataLoaded did not fire within 90s"
echo "SKSE log:"
cat "$SKSE_LOG" 2>/dev/null | tail -10
"$PROTON/bin/wineserver" -k 2>/dev/null
exit 1
