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

# Assert damage==99 across every weapon in the load order, including
# DLC/CC/ESL forms. Cross-plugin load-order alignment now flows from
# plugins.txt into the compiler (issue #5), so 0x02… / 0x04… / 0xFE…
# form IDs produced at compile time match their runtime counterparts.
jq_assert_all '.damage == 99' "$weapons" || exit $?

quit_harness
echo "[check] weapon_damage: PASS"
