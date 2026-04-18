#!/usr/bin/env bash
# Integration test: every in-scope armor setter applied to Iron Helmet
# (0x00012E4D). Asserts each field against the harness's armors.jsonl dump.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$HERE/../_lib/check_common.sh"

trap stash_runtime_logs EXIT

wait_for_harness     || exit $?
armors="$(dump_form_type armors)" || exit $?

jq_assert_all '(.formid != "0x00012E4D") or (.value        == 777)'                  "$armors" || exit $?
jq_assert_all '(.formid != "0x00012E4D") or (.weight       == 8.25)'                 "$armors" || exit $?
jq_assert_all '(.formid != "0x00012E4D") or (.name         == "Modified Iron Helmet")' "$armors" || exit $?
jq_assert_all '(.formid != "0x00012E4D") or (.armor_rating == 2500)'                 "$armors" || exit $?
jq_assert_all '(.formid != "0x00012E4D") or ((.keywords | length) >= 2)'             "$armors" || exit $?

quit_harness
echo "[check] armor_setters: PASS"
