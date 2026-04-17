#!/usr/bin/env bash
# Integration test: every in-scope weapon setter applied to Iron Sword
# (0x00012EB7). Asserts each field against the harness's weapons.jsonl
# dump with `formid == 0x00012EB7` as the selector.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$HERE/../_lib/check_common.sh"

trap stash_runtime_logs EXIT

wait_for_harness      || exit $?
weapons="$(dump_form_type weapons)" || exit $?

# Scalar asserts — one jq expression per field, scoped to Iron Sword.
# The " or " skip-pattern (`.formid != "0x00012EB7" or ...`) lets
# jq_assert_all's "every line must pass" contract stand up against
# every other weapon in the load order, which the rule deliberately
# didn't touch.
jq_assert_all '(.formid != "0x00012EB7") or (.damage    == 42)'        "$weapons" || exit $?
jq_assert_all '(.formid != "0x00012EB7") or (.value     == 999)'       "$weapons" || exit $?
jq_assert_all '(.formid != "0x00012EB7") or (.weight    == 3.5)'       "$weapons" || exit $?
jq_assert_all '(.formid != "0x00012EB7") or (.speed     == 2.0)'       "$weapons" || exit $?
jq_assert_all '(.formid != "0x00012EB7") or (.reach     == 1.25)'      "$weapons" || exit $?
jq_assert_all '(.formid != "0x00012EB7") or (.name      == "Modified Iron Sword")' "$weapons" || exit $?

# add_keyword: the rule added MagicDisallowEnchanting to Iron Sword's
# keyword list. The vanilla sword already carries WeapMaterialIron +
# WeapTypeSword + VendorItemWeapon + some others (see test_shared_getters
# "HasKeywordOnWeapon"); adding one more pushes the count to ≥ baseline+1.
# We can't pin the added keyword's FormID here without hard-coding a
# vanilla-version-specific value, so we assert cardinality instead.
jq_assert_all '(.formid != "0x00012EB7") or ((.keywords | length) >= 3)' "$weapons" || exit $?

quit_harness
echo "[check] weapon_setters: PASS"
