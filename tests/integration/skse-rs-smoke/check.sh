#!/usr/bin/env bash
# skse-rs-smoke end-to-end:
#   1. Plugin loaded and opened its log.
#   2. Address Library resolved.
#   3. kDataLoaded listener fired.
#   4. Iron Sword + WeapMaterialIron looked up.
#   5. add_keyword succeeded (added or already-present).
#   6. Verify readback logged.
#   7. Plugin wrote "smoke OK".

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/../_lib/check_common.sh"

trap stash_runtime_logs EXIT

wait_for_main_menu || exit $?

LOG="$SKYRIM_PROFILE_DIR/SKSE/SkseRsSmoke.log"

if [[ ! -s "$LOG" ]]; then
    _err "skse-rs-smoke: SkseRsSmoke.log missing or empty at $LOG"
    exit 1
fi

# Required lines, in order, as grep -F -q matches.
REQUIRED=(
    "Hello from skse-rs"
    "SKSE runtime: 0x"
    "Address Library loaded"
    "kDataLoaded received"
    "Iron Sword lookup: 0x00012EB7"
    "WeapMaterialIron lookup: 0x0001E718"
    "AddKeyword result: "
    "verify readback: num_keywords ="
    "smoke OK"
)

for line in "${REQUIRED[@]}"; do
    if ! grep -F -q "$line" "$LOG"; then
        _err "skse-rs-smoke: missing required line in log: $line"
        _err "log contents:"
        sed 's/^/  /' "$LOG" >&2
        exit 1
    fi
done

echo "[check] skse-rs-smoke: PASS"
