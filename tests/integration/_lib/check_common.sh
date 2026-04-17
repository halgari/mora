#!/usr/bin/env bash
# Shared helpers for integration-test hooks. Source, don't exec:
#
#   source "$(dirname "$0")/../_lib/check_common.sh"
#
# Provides:
#   wait_for_harness [retries] [delay_s]
#   send_harness_cmd <cmd...>                 → JSON on stdout
#   dump_form_type <type>                     → path to the written jsonl
#   jq_assert_all <jq-bool-expr> <jsonl>      → exits non-zero with diag on fail
#   quit_harness                              → fire-and-forget
#
# Every helper is designed to be called from a hook running inside the
# skyrim-runner container, where the harness DLL is loaded in Skyrim and
# listening on 127.0.0.1:9742. Paths like /tmp/skyrim/... refer to the
# merged overlay that `run-skyrim-test.sh` has set up as CWD.

set -uo pipefail

: "${HARNESS_HOST:=127.0.0.1}"
: "${HARNESS_PORT:=9742}"
: "${SKYRIM_ROOT:=/tmp/skyrim}"
: "${LOG_DIR:=/tmp/skyrim-logs}"

# Location of tools/test_harness.py inside the checked-out repo. CI sets
# GITHUB_WORKSPACE; when running manually, set REPO_ROOT or the default
# below is used.
: "${REPO_ROOT:=${GITHUB_WORKSPACE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}}"
TEST_HARNESS_PY="$REPO_ROOT/tools/test_harness.py"

# Both log and err route to stderr so helpers that echo a return value
# on stdout (dump_form_type echoes the dump path) can be safely captured
# with $(…) without pulling in diagnostic noise.
_log() { echo "[check] $*" >&2; }
_err() { echo "[check] ERROR: $*" >&2; }

# wait_for_harness — poll TCP 9742 for `status: ok` until success or retries
# exhaust. Skyrim cold-boot + form-load takes 40–90s; default allows 180s.
wait_for_harness() {
  local retries="${1:-180}"
  local delay="${2:-1}"
  local i resp
  for ((i = 1; i <= retries; i++)); do
    resp="$(python3 "$TEST_HARNESS_PY" --host "$HARNESS_HOST" --port "$HARNESS_PORT" status 2>/dev/null || true)"
    if echo "$resp" | jq -e '.ok == true and .forms_loaded == true' > /dev/null 2>&1; then
      _log "harness ready after ${i}s (forms_loaded=true)"
      return 0
    fi
    sleep "$delay"
  done
  _err "harness did not report forms_loaded=true after ${retries}s"
  return 1
}

# send_harness_cmd — send an arbitrary command; stdout = full JSON response.
# Usage: send_harness_cmd "dump npcs"
send_harness_cmd() {
  local cmd="$*"
  python3 - <<PY
import json, socket, sys
s = socket.socket(); s.settimeout(60)
s.connect(("$HARNESS_HOST", $HARNESS_PORT))
s.sendall(("""$cmd""" + "\n").encode())
data = b""
while b"\n" not in data:
    chunk = s.recv(4096)
    if not chunk: break
    data += chunk
sys.stdout.write(data.decode().strip())
PY
}

# dump_form_type <type>   e.g. dump_form_type npcs
#
# Issues `dump <type>` to the harness. On success echoes the local path
# to the jsonl file (under $SKYRIM_ROOT). On failure prints the error
# JSON to stderr and returns non-zero.
#
# The harness currently writes dumps to Data/MoraCache/dumps/<type>.jsonl
# relative to Skyrim's CWD at launch. We construct the path locally
# rather than trusting the JSON response's `file` field — older harness
# builds emit unescaped Windows paths (e.g. "Data\MoraCache\dumps\npcs.jsonl")
# whose backslash-n gets decoded to a literal newline by JSON parsers.
dump_form_type() {
  local type="$1"
  local resp
  resp="$(send_harness_cmd "dump $type")"
  if ! echo "$resp" | jq -e '.ok == true' > /dev/null; then
    _err "dump $type failed: $resp"
    return 1
  fi

  local file="$SKYRIM_ROOT/Data/MoraCache/dumps/${type}.jsonl"
  if [[ ! -s "$file" ]]; then
    _err "dump file missing or empty: $file"
    _err "harness response was: $resp"
    return 2
  fi
  _log "dump $type → $file ($(wc -l < "$file") lines)"
  echo "$file"
}

# jq_assert_all <jq-bool-expr> <jsonl-file>
#
# For each line in the jsonl, evaluate <jq-bool-expr>. Fail if any line
# returns false. On failure, copy the jsonl into $LOG_DIR for post-mortem
# and print a short diff-like summary.
#
# Example:
#   jq_assert_all '.name == "Nazeem"' "$npc_file"
jq_assert_all() {
  local expr="$1"
  local file="$2"

  if [[ ! -s "$file" ]]; then
    _err "FAIL: jq_assert_all expected a non-empty file, got: $file"
    return 4
  fi

  local total failed
  total=$(wc -l < "$file")
  # A zero-line dump is a silent success bug, not a real OK.
  if (( total == 0 )); then
    _err "FAIL: $file has 0 lines — nothing to assert over"
    return 4
  fi

  # Count lines where expr is NOT true. Missing / null / false all count
  # as failures.
  failed=$(jq -c "select((${expr}) | not)" "$file" | wc -l)

  if (( failed == 0 )); then
    _log "OK: $total/$total lines satisfy ${expr}"
    return 0
  fi

  _err "FAIL: $failed/$total lines violate ${expr}"
  mkdir -p "$LOG_DIR"
  cp "$file" "$LOG_DIR/$(basename "$file")"
  _err "copied $(basename "$file") → $LOG_DIR/ for inspection"

  # Show up to 10 offending lines.
  _err "first offending lines:"
  jq -c "select((${expr}) | not)" "$file" | head -10 | while IFS= read -r line; do
    _err "  $line"
  done
  return 3
}

# quit_harness — fire-and-forget (the harness closes the socket; we don't
# care about errors after quit).
quit_harness() {
  send_harness_cmd "quit" > /dev/null 2>&1 || true
}

# stash_runtime_logs — copy any SKSE / MoraRuntime logs into $LOG_DIR so
# the CI log-artifact upload includes them for post-mortem. Safe to call
# from the hook's EXIT trap; never fails the test.
stash_runtime_logs() {
  mkdir -p "$LOG_DIR" 2>/dev/null || return 0
  # My Games/<edition>/SKSE/*.log — path varies by edition (GOG vs Steam).
  local prefix="${STEAM_COMPAT_DATA_PATH:-/tmp/prefix}/pfx"
  local my_games="$prefix/drive_c/users/steamuser/Documents/My Games"
  if [[ -d "$my_games" ]]; then
    find "$my_games" -name "*.log" -type f 2>/dev/null | while read -r f; do
      local tag
      tag="$(dirname "$f" | sed "s|$my_games/||" | tr '/ ' '__')"
      cp "$f" "$LOG_DIR/${tag}__$(basename "$f")" 2>/dev/null || true
    done
  fi
  # Staged mora_patches.bin — size + presence tells us if the runtime
  # even had patches to apply.
  local patches="$SKYRIM_ROOT/Data/SKSE/Plugins/mora_patches.bin"
  if [[ -f "$patches" ]]; then
    cp "$patches" "$LOG_DIR/mora_patches.bin" 2>/dev/null || true
    _log "stashed mora_patches.bin ($(stat -c %s "$patches") bytes)"
  fi
  # Runtime-generated Plugins.txt / loadorder.txt. These are the
  # engine's authoritative load-order source; without them we have
  # to guess at compile time. Stashing them lets CI commit/inspect
  # the actual order when compile-time divergence shows up.
  local appdata="$prefix/drive_c/users/steamuser/AppData/Local"
  if [[ -d "$appdata" ]]; then
    find "$appdata" -maxdepth 3 -type f \
         \( -iname "Plugins.txt" -o -iname "loadorder.txt" \) 2>/dev/null \
    | while read -r f; do
        local tag
        tag="$(dirname "$f" | sed "s|$appdata/||" | tr '/ ' '__')"
        cp "$f" "$LOG_DIR/${tag}__$(basename "$f")" 2>/dev/null || true
      done
  fi
}
