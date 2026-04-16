#!/usr/bin/env bash
# Integration test: form/name patching reaches every NPC.
#
# The compiled mora_patches.bin emits a `set form/name(NPC, "Nazeem")`
# for every NPC in the load order. Runtime applies those patches on
# DataLoaded; harness then dumps (formid, name) for every live NPC form.
# This hook asserts every dumped line has name == "Nazeem".

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$HERE/../_lib/check_common.sh"

wait_for_harness    || exit $?
npcs="$(dump_form_type npcs)" || exit $?

jq_assert_all '.name == "Nazeem"' "$npcs" || exit $?

quit_harness
echo "[check] nazeem: PASS"
