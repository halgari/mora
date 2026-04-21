# skse-rs-smoke

Pure-Rust SKSE plugin (built via `skse-rs`) must load successfully
into Skyrim SE and write "Hello from skse-rs" to its log file at
`<Documents>\My Games\Skyrim Special Edition\SKSE\SkseRsSmoke.log`.

No game state is read or modified — this validates only that a Rust
cdylib produced by `cargo xwin build -p skse-rs-smoke --target
x86_64-pc-windows-msvc` is a valid SKSE plugin and that all of Plan 2's
ABI work (exports, log directory resolution, plugin version data) is
wire-compatible with SKSE's loader.

**CI gate:** this case is not yet wired into the self-hosted
`skyrim-integration` job — Plan 2 leaves that job at `if: false`.
Run manually via `run-skyrim-test.sh` on a dev box, or enable it in
Plan 3 / M5 when adding the TCP-harness-driven cases.
