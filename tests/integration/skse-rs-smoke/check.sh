#!/usr/bin/env bash
# skse-rs-smoke: the pure-Rust plugin must load and write its log line.
#
# This case does not use the TCP harness — it validates only that the
# plugin was loaded and ran its on_load. A later case (added by Plan 3)
# will exercise real game state via the TCP harness.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/../_lib/check_common.sh"

trap stash_runtime_logs EXIT

# Wait for Skyrim to reach the main menu.
wait_for_main_menu || exit $?

# Locate the plugin's log. Same My Games path the plugin uses.
LOG="$SKYRIM_PROFILE_DIR/SKSE/SkseRsSmoke.log"

if [[ ! -s "$LOG" ]]; then
    _err "skse-rs-smoke: SkseRsSmoke.log missing or empty at $LOG"
    exit 1
fi

if ! grep -q "^Hello from skse-rs$" "$LOG"; then
    _err "skse-rs-smoke: log does not contain expected greeting line"
    _err "log contents:"
    sed 's/^/  /' "$LOG" >&2
    exit 1
fi

echo "[check] skse-rs-smoke: PASS"
