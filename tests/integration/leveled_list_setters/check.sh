#!/usr/bin/env bash
# Integration test: set_chance_none on LItemEnchWeaponBowBlacksmith
# (0x00000EDE). Asserts the single-in-scope LVLI setter round-trips.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$HERE/../_lib/check_common.sh"

trap stash_runtime_logs EXIT

wait_for_harness                     || exit $?
lists="$(dump_form_type leveled_lists)" || exit $?

jq_assert_all '(.formid != "0x00000EDE") or (.chance_none == 42)' "$lists" || exit $?

quit_harness
echo "[check] leveled_list_setters: PASS"
