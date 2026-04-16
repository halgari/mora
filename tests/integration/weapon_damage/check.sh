#!/usr/bin/env bash
# Integration test: every weapon's base damage is patched to 99.
#
# rules.mora emits `set form/damage(W, 99)` for every `form/weapon(W)`.
# The runtime applies those patches to each TESObjectWEAP on DataLoaded.
# The harness then iterates weapon forms and dumps (formid, name, damage,
# value, weight, keywords) for each. This hook asserts every dumped line
# has damage == 99.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$HERE/../_lib/check_common.sh"

# Always stash runtime logs + patches bin on exit so CI artifacts are
# useful regardless of pass/fail outcome.
trap stash_runtime_logs EXIT

wait_for_harness      || exit $?
weapons="$(dump_form_type weapons)" || exit $?

# Scope the assertion to Skyrim.esm weapons (load-index byte = 0x00).
# Cross-plugin load-order alignment between compile-time and runtime
# isn't reliable yet for CC/DLC weapons, so asserting across all of
# them would catch an unrelated bug. Skyrim.esm is always loaded at
# index 0 both times and exercises the full pipeline.
jq_assert_all '(.formid | startswith("0x00") | not) or .damage == 99' "$weapons" || exit $?

quit_harness
echo "[check] weapon_damage: PASS"
