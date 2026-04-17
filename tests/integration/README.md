# Mora Skyrim-in-the-loop integration tests

Each subdirectory here is **one test case**. CI runs them as a matrix on the
self-hosted skyrim-runner pool (see `.github/workflows/ci.yml`,
`skyrim-integration` job).

## Layout

```
tests/integration/
  _lib/check_common.sh        shared bash helpers — sourced from check.sh
  <case>/rules.mora           mora source under test
  <case>/check.sh             hook — exit 0 = pass, non-zero = fail
  <case>/README.md            one-line description of the invariant
```

## How one run works

1. CI downloads `Mora-windows` — the release archive contains `mora.exe`,
   `MoraRuntime.dll`, and `MoraTestHarness.dll`.
2. Runs `mora.exe compile rules.mora --data-dir /skyrim-base/Data --output
   staged/Data/MoraCache` under Proton to produce `mora_patches.bin`.
3. Stages `Data/SKSE/Plugins/{MoraRuntime,MoraTestHarness}.dll` + the
   patches into a mod-dir.
4. Calls `/usr/local/bin/run-skyrim-test.sh <mod-dir>` with
   `TEST_HOOK=<case>/check.sh`. The runner script overlays the mod-dir onto
   the vanilla Skyrim + SKSE baseline, launches SKSE/Skyrim under Proton,
   and invokes the hook once `SkyrimSE.exe` is alive.
5. The hook talks to the harness on `127.0.0.1:9742`, asks it to dump
   whatever game state is relevant, asserts, exits.

## Writing a new case

Minimum effort:

1. Create `tests/integration/<case>/`.
2. Write `rules.mora` — the mora rule you want to exercise.
3. Write `check.sh` — source `_lib/check_common.sh`, call helpers, assert.
   A typical hook is ~15 lines.
4. Add `<case>` to the `case:` matrix list in `ci.yml`.

## Hook environment

The hook is invoked inside the runner container after `SkyrimSE.exe` is
alive. It inherits:

- `DISPLAY=:99` — Xorg with `xserver-xorg-video-dummy`, 1920×1080.
- `PULSE_SERVER=…` — pulseaudio null sink.
- cwd = `/tmp/skyrim` — the merged overlay (vanilla + SKSE baseline + the
  staged mod-dir).
- `$LOG_DIR` — writable; everything dropped here is uploaded as a CI
  artifact on failure.
- Harness on `127.0.0.1:9742`.
- Dump files written by the harness land at
  `/tmp/skyrim/Data/MoraCache/dumps/<type>.jsonl`.

Exit 0 = pass. Any non-zero = fail; the exit code propagates through
`run-skyrim-test.sh` and becomes the CI step status.

## Local debugging

Runs happen on the self-hosted runners at `root@10.20.77.32`. To iterate
on a hook manually:

```bash
# On the Unraid host:
docker exec -it unraid-runner-1 bash
# Inside the container:
cd /path/to/your/staged/mod-dir
TEST_HOOK=/path/to/check.sh /usr/local/bin/run-skyrim-test.sh "$(pwd)"
```

Log tails:
- `$LOG_DIR/proton.stdout.log` / `proton.stderr.log` — SKSE/Skyrim output.
- `$LOG_DIR/hook.stdout.log` / `hook.stderr.log` — your hook's output.
