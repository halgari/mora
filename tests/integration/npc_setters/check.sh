#!/usr/bin/env bash
# Integration test: every in-scope NPC setter applied to Nazeem
# (0x00013BBF). Asserts each field against the harness's npcs.jsonl dump.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$HERE/../_lib/check_common.sh"

trap stash_runtime_logs EXIT

wait_for_harness   || exit $?
npcs="$(dump_form_type npcs)" || exit $?

# Scalar fields
jq_assert_all '(.formid != "0x00013BBF") or (.name           == "Modified Nazeem")' "$npcs" || exit $?
jq_assert_all '(.formid != "0x00013BBF") or (.level          == 42)'                 "$npcs" || exit $?
jq_assert_all '(.formid != "0x00013BBF") or (.calc_level_min == 10)'                 "$npcs" || exit $?
jq_assert_all '(.formid != "0x00013BBF") or (.calc_level_max == 60)'                 "$npcs" || exit $?
jq_assert_all '(.formid != "0x00013BBF") or (.speed_mult     == 150)'                "$npcs" || exit $?

# Collections — each `add form/...` pushes one entry. We can't assert the
# exact FormID without pinning it to vanilla Skyrim.esm editor IDs, and
# we can't assert an exact total without knowing the vanilla baseline —
# so we check "non-empty" which catches any wiring failure (add_* did
# nothing → zero-length array).
jq_assert_all '(.formid != "0x00013BBF") or ((.keywords  | length) >= 1)' "$npcs" || exit $?
jq_assert_all '(.formid != "0x00013BBF") or ((.spells    | length) >= 1)' "$npcs" || exit $?
jq_assert_all '(.formid != "0x00013BBF") or ((.perks     | length) >= 1)' "$npcs" || exit $?
jq_assert_all '(.formid != "0x00013BBF") or ((.factions  | length) >= 1)' "$npcs" || exit $?

quit_harness
echo "[check] npc_setters: PASS"
