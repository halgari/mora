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

_log() { echo "[check] $*"; }
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
# Issues `dump <type>` to the harness. On success echoes the local path to
# the jsonl file (under $SKYRIM_ROOT). On failure prints the error JSON to
# stderr and returns non-zero.
dump_form_type() {
  local type="$1"
  local resp file
  resp="$(send_harness_cmd "dump $type")"
  if ! echo "$resp" | jq -e '.ok == true' > /dev/null; then
    _err "dump $type failed: $resp"
    return 1
  fi
  # Harness reports the path it wrote (relative "Data/MoraCache/..."
  # because Skyrim's CWD at launch is the install root). std::filesystem
  # on Windows may emit backslashes — flip them to slashes before we
  # touch the file from linux. Then prefix with the overlay root if
  # the path isn't already absolute.
  file="$(echo "$resp" | jq -r '.file' | tr '\\' '/')"
  case "$file" in
    /*)  : ;;                                   # already absolute (linux)
    [A-Za-z]:/*) file="$SKYRIM_ROOT/${file#*:}" ;;  # drive-letter absolute
    *)   file="$SKYRIM_ROOT/$file" ;;           # relative
  esac
  if [[ ! -s "$file" ]]; then
    _err "dump file missing or empty: $file"
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

  local total failed
  total=$(wc -l < "$file")
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
