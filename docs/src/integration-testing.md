# Integration testing against real Skyrim

Mora's integration tests drive the entire pipeline — compile → runtime →
live Skyrim — and read game state back out via an in-process SKSE
harness plugin. Each test is a `.mora` source plus a small bash hook
that asserts against the harness's dumped JSONL. The same tests run in
CI on a self-hosted runner pool and locally on any developer box that
can build the Windows DLLs and run Proton.

This page explains the moving parts, the directory contract for test
cases, and the local loop for iterating on one.

---

## What a run does

1. `mora compile rules.mora --data-dir <Skyrim>/Data --output <mod-dir>/Data/SKSE/Plugins`
   produces `mora_patches.bin`.
2. A **mod-dir** is staged with:
   ```
   Data/SKSE/Plugins/MoraRuntime.dll
   Data/SKSE/Plugins/MoraTestHarness.dll
   Data/SKSE/Plugins/mora_patches.bin
   ```
3. `run-skyrim-test.sh` (in the runner image; also runnable on a dev
   box) overlays the mod-dir onto a read-only vanilla Skyrim install
   plus an SKSE baseline, launches `skse64_loader.exe` under Proton with
   Xvfb/Xorg-dummy + DXVK, and execs a test hook once `SkyrimSE.exe` is
   alive.
4. The hook talks to `MoraTestHarness` on `127.0.0.1:9742`, asks for a
   dump of NPCs / weapons / armor / etc., parses the JSONL the harness
   wrote to `Data/MoraCache/dumps/<type>.jsonl`, and exits 0 if the
   invariant holds.
5. On exit, `$LOG_DIR` is uploaded as a CI artifact and the hook's
   `stash_runtime_logs` helper copies the SKSE log, the MoraRuntime
   log, and the compiled `mora_patches.bin` into it for post-mortem.

---

## The harness

`src/harness/harness_plugin.cpp` is a standalone SKSE plugin
(`MoraTestHarness.dll`) that opens a loopback-only TCP listener on
port 9742 once SKSE fires `kDataLoaded`. It's a separate DLL from
`MoraRuntime` because both ship their own `SKSEPlugin_Load` /
`SKSEPlugin_Version` / `DllMain` entry points.

Commands are newline-terminated, responses are single-line JSON:

| Command         | Response shape                                                                 |
|-----------------|--------------------------------------------------------------------------------|
| `status`        | `{"ok":true,"forms_loaded":true/false}`                                        |
| `dump weapons`  | `{"ok":true,"file":"Data/MoraCache/dumps/weapons.jsonl","count":N}`            |
| `dump npcs`     | `{"ok":true,"file":"Data/MoraCache/dumps/npcs.jsonl","count":N}`               |
| `lookup 0xFID`  | `{"ok":true,"formid":"…","found":true,"form_type":"…","name":"…"}`             |
| `quit`          | `{"ok":true}` (harness closes the socket)                                      |

Dump files are written with forward-slash paths (`fs::path::generic_string()`)
so they round-trip cleanly through JSON.

---

## Runner environment

CI runs land on an Unraid host-provided self-hosted runner pool. The
image source is at `root@10.20.77.32:/mnt/user/skyrim-runner/`; see that
repo's README for image build details. For local work you don't need to
reproduce the runner container — just run the pieces directly on your
dev box.

The image bakes everything a test job needs:

- **Linux toolchain** for the mora compiler: gcc-13, xmake v2.9.9,
  python3-yaml, XMAKE_ROOT=y.
- **Windows cross-compile toolchain** for the DLLs: clang-19, lld-19,
  xwin 0.8.0 (splatted sysroot at `/opt/xwin`, `XWIN_PATH` pre-set),
  fmt 12.1.0 + spdlog 1.17.0 headers at `/usr/include/`.
- **Proton-GE 10-34** at `/opt/proton/` with a warm-baked wine prefix
  at `/opt/warm-prefix/`.
- **Display + audio stubs**: Xorg with `xserver-xorg-video-dummy`,
  pulseaudio null sink, DXVK via nvidia-container-toolkit.
- **Skyrim payload** bind-mounted read-only:
  - `/skyrim-base/` — vanilla GOG Skyrim SE 1.6.x + all CC content.
  - `/skyrim-baseline/` — SKSE + Address Library `.bin` files.
- **Orchestrator script** `/usr/local/bin/run-skyrim-test.sh`.
- **Env helper** `/usr/local/bin/skyrim-env.sh` (idempotent: brings up
  Xorg/pulse/prefix; safe to source from multiple steps).

Runner labels: `self-hosted, linux, unraid, skyrim`. The CI job guards
fork PRs out of this pool (fork code on a self-hosted runner = arbitrary
code execution).

---

## Test-case layout

```
tests/integration/
  README.md
  _lib/check_common.sh        sourced helpers (see below)
  <case>/
    rules.mora                the mora source under test
    check.sh                  hook — exit 0 = pass, nonzero = fail
    README.md                 one-line description of the invariant
```

Each subdirectory under `tests/integration/` is one matrix entry in
`.github/workflows/ci.yml` (`skyrim-integration` job, `matrix.case`
list). Adding a new case is drop-a-dir + add to `case:` — no workflow
changes.

### `check.sh` contract

Inherited env (from `run-skyrim-test.sh`):

- `DISPLAY=:99`, `PULSE_SERVER=…`, `PATH` includes Proton tools.
- cwd = `/tmp/skyrim` (the merged overlay).
- `$LOG_DIR` is writable; artifact upload picks it up on job exit.
- `$SKYRIM_ROOT` = `/tmp/skyrim`.
- `$GITHUB_WORKSPACE` is the repo checkout (CI only; helpers fall back
  to a relative lookup otherwise).

Exit 0 = pass. Non-zero propagates through `run-skyrim-test.sh`,
fails the CI step, and triggers the log-artifact upload.

### Shared helpers (`_lib/check_common.sh`)

Minimal hooks look like:

```bash
#!/usr/bin/env bash
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/../_lib/check_common.sh"

trap stash_runtime_logs EXIT

wait_for_harness           || exit $?
file="$(dump_form_type weapons)" || exit $?
jq_assert_all '.damage == 99' "$file" || exit $?

quit_harness
echo "[check] my_case: PASS"
```

| Helper                      | Purpose                                                             |
|-----------------------------|---------------------------------------------------------------------|
| `wait_for_harness [N] [d]`  | Poll `status` up to N times (default 180) @ d seconds apart until `forms_loaded=true`. |
| `send_harness_cmd "<cmd>"`  | Send arbitrary command; stdout = raw JSON response.                 |
| `dump_form_type <type>`     | Send `dump <type>`; echoes the resolved local path to the JSONL.    |
| `jq_assert_all '<expr>' F`  | Fail if any line in `F` doesn't satisfy the jq boolean expression; copies the file into `$LOG_DIR` on mismatch and prints the first 10 offending lines. Also fails on empty / missing file. |
| `quit_harness`              | Fire-and-forget `quit` (harness tears down its TCP thread).         |
| `stash_runtime_logs`        | Copy SKSE log, MoraRuntime log, and `mora_patches.bin` into `$LOG_DIR`. Safe from an EXIT trap; never fails. |

`_log` and `_err` both route to stderr so stdout captures in `$(…)` get
only the intended return value.

---

## Running one locally

Two options: (a) reproduce on your dev box, (b) `docker exec` into one
of the skyrim-runner containers. (a) is the usual loop; (b) is useful
for one-off debugging once CI has already staged everything.

### (a) Dev box

**Prerequisites:**

- The clang-cl / xwin cross-compile toolchain — see
  [Cross-compile (Windows build)](cross-compile-windows.md) for the
  one-time setup.
- A writable Skyrim install (GOG is cleanest per
  `docs/cross-compile-windows.md` traps). `scripts/test_runtime.sh`
  assumes the paths documented there.
- Proton-GE (any recent 10.x) at `/opt/proton` or adapt the env.
- `xvfb-run` or a real X session; `jq`, `python3`, `nc` in `$PATH`.

**One-shot loop** (from the repo root):

```bash
# 1. Build everything.
xmake build mora                        # native Linux compiler
xmake f -p windows -a x64 -m release -y # configure cross-compile
xmake build mora_runtime mora_test_harness

# 2. Stage a mod-dir for Skyrim.
STAGE=/tmp/mora-test
CASE=weapon_damage
rm -rf "$STAGE" && mkdir -p "$STAGE/Data/SKSE/Plugins"
cp build/windows/x64/release/MoraRuntime.dll       "$STAGE/Data/SKSE/Plugins/"
cp build/windows/x64/release/MoraTestHarness.dll   "$STAGE/Data/SKSE/Plugins/"

# 3. Compile the rule against your local Skyrim Data dir.
./build/linux/x86_64/release/mora compile \
    tests/integration/${CASE}/rules.mora \
    --data-dir ~/.local/share/Steam/steamapps/common/Skyrim\ Special\ Edition/Data \
    --output  "$STAGE/Data/SKSE/Plugins"

# 4. Deploy into your Skyrim install and launch SKSE.
scripts/deploy_runtime.sh "$STAGE/Data/SKSE/Plugins/mora_patches.bin"
scripts/test_runtime.sh   # launches under Proton; see its header

# 5. In another terminal, run the hook against the live game.
GITHUB_WORKSPACE="$PWD" SKYRIM_ROOT="$PWD/skyrim-proxy" \
  LOG_DIR=/tmp/mora-logs \
  tests/integration/${CASE}/check.sh
```

The `--data-dir` at compile time and the Skyrim install at runtime
should be the same load order. Mismatches between them surface as
out-of-range FormIDs (see
[issue #5](https://github.com/halgari/mora/issues/5)).

### (b) SSH into a runner container

When CI has just failed and you want to reproduce with the same
artifacts:

```bash
ssh root@10.20.77.32
docker exec -it unraid-runner-1 bash

# Inside the container: the repo checkout from the last run lives at
# /_work/unraid-runner-1/mora/mora and the build artifacts are still
# in place. Rebuild if needed (toolchain is baked), then:
CASE=weapon_damage
TEST_HOOK=$(pwd)/tests/integration/${CASE}/check.sh \
  /usr/local/bin/run-skyrim-test.sh $(pwd)/mod-under-test
```

`run-skyrim-test.sh` logs to `/tmp/skyrim-logs/` by default. Set
`ENABLE_VNC=1` and publish port 5900 on the container if you want to
watch the game boot.

---

## Adding a new case

1. `mkdir tests/integration/<case>/`.
2. Write `rules.mora` — the mora source you want to exercise.
3. Write `check.sh` — source `_lib/check_common.sh`, send one or two
   harness commands, assert, exit. Use `trap stash_runtime_logs EXIT`
   so failures upload useful logs.
4. Write a one-line `README.md` describing the invariant.
5. Append `<case>` to the `case:` matrix list in
   `.github/workflows/ci.yml` under the `skyrim-integration` job.

`tests/integration/weapon_damage/` is the canonical example. Keep hooks
small (10–20 lines); the helpers handle TCP plumbing, JSONL path
resolution, log capture, and failure diagnostics.

---

## Known limitations

- **Load-order alignment with `plugins.txt`** —
  [#5](https://github.com/halgari/mora/issues/5). The compiler walks the
  `Data/` dir in filesystem order; runtime uses `plugins.txt`. Today
  only Skyrim.esm's forms survive the translation, so the
  `weapon_damage` case scopes its assertion to `formid` values whose
  high byte is `0x00`. Once the compiler honors `plugins.txt`, loosen
  the filter.
- **Parallelism** — the runner pool has 3 ephemeral containers, so the
  matrix fans out up to 3-wide. Growing past that means adding runners
  to the Unraid compose stack.
- **Fork PRs** — the self-hosted pool is off-limits to fork PRs (`if:`
  guard in the workflow). Members have to push to a branch on this repo
  to get CI for their changes.
