#!/usr/bin/env bash
# Integration test: every Skyrim.esm NPC is renamed "Nazeem".
#
# rules.mora emits `set form/name(N, "Nazeem")` for every `form/npc(N)`.
# The runtime applies those patches on DataLoaded by writing the
# BSFixedString into each TESNPC's TESFullName component. The harness
# then dumps (formid, name) for every NPC and this hook asserts every
# Skyrim.esm NPC has name == "Nazeem".
#
# Regression test for #4.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$HERE/../_lib/check_common.sh"

trap stash_runtime_logs EXIT

wait_for_harness || exit $?
npcs="$(dump_form_type npcs)" || exit $?

# Scope to Skyrim.esm NPCs (load-index byte = 0x00) for the same reason
# weapon_damage does: cross-plugin load-order alignment between
# compile-time and runtime isn't reliable yet.
jq_assert_all '(.formid | startswith("0x00") | not) or .name == "Nazeem"' "$npcs" || exit $?

quit_harness
echo "[check] nazeem: PASS"
