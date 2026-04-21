# Unraid Runner Image Refresh (M0)

The self-hosted Skyrim runner image at
`root@10.20.77.32:/mnt/user/skyrim-runner/` was baked for the C++
pipeline (xmake + clang-cl + xwin + Proton). The Rust pivot replaces
the C++ cross-compile chain with `cargo-xwin`; this doc captures the
exact delta.

## What stays

- `/skyrim-base/` — vanilla GOG Skyrim SE 1.6.x + all CC content (bind-mount, read-only).
- `/skyrim-baseline/` — SKSE + Address Library `.bin` files (bind-mount, read-only).
- Proton-GE 10-34 at `/opt/proton/` + warm wine prefix at `/opt/warm-prefix/`.
- Display + audio stubs: Xorg with `xserver-xorg-video-dummy`, pulseaudio null sink, DXVK via nvidia-container-toolkit.
- `/usr/local/bin/run-skyrim-test.sh` orchestrator (language-agnostic; no changes needed).
- `/usr/local/bin/skyrim-env.sh` env helper (idempotent; no changes needed).
- Runner labels: `self-hosted, linux, unraid, skyrim`.

## What to remove

```bash
# Xmake toolchain
apt-get remove --purge -y xmake
rm -rf /opt/xmake ~/.xmake

# C++ cross-compile toolchain
apt-get remove --purge -y gcc-13 g++-13 clang-19 lld-19
rm -rf /opt/xwin

# C++ headers previously installed for the xmake builds
rm -rf /usr/include/fmt /usr/include/spdlog
```

## What to add

```bash
# Rust toolchain (rustup-managed, Rust 1.85).
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
  sh -s -- -y --default-toolchain 1.85 \
    --profile minimal \
    --component rustfmt,clippy
source "$HOME/.cargo/env"
rustup target add x86_64-pc-windows-msvc

# cargo-xwin for MSVC cross-compile from Linux.
cargo install cargo-xwin --locked

# Sanity: warm the xwin cache so the first real build doesn't
# download ~2GB of SDK.
cargo xwin --help >/dev/null
```

## Verification

From inside the running container:

```bash
rustc --version                          # expect: rustc 1.85.x (…)
cargo --version                          # expect: cargo 1.85.x (…)
cargo xwin --version                     # expect: cargo-xwin 0.x.y
rustup target list --installed           # expect: x86_64-pc-windows-msvc listed
```

Then clone the mora repo and run:

```bash
cd /tmp
git clone https://github.com/halgari/mora.git
cd mora
cargo check --workspace
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Both should succeed.

## Rollout

1. Stop the existing runner containers on Unraid.
2. Apply the "remove" + "add" steps above to the image Dockerfile (or
   whichever bake process is in use).
3. Rebuild and redeploy the containers.
4. Dispatch the Mora `ci` workflow manually (`workflow_dispatch`) and
   confirm the `test` / `clippy` / `fmt` / `windows-cross` jobs pick up
   the new image cleanly. The `skyrim-integration` job stays skipped
   until M5.

The image refresh is a prerequisite for M5 (it's where the Skyrim
integration pipeline actually runs), but doing it now keeps the infra
delta bundled with the rest of the M0 toolchain churn.
