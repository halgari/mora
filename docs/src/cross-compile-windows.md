# Cross-compiling the Windows build on Linux

Mora's Windows artifacts — `mora.exe`, `MoraRuntime.dll`, and
`MoraTestHarness.dll` — are built on Linux via Clang targeting
`x86_64-pc-windows-msvc`. Every release and every CI run uses the same
path: no Visual Studio, no wine-MSVC, no Windows runner.

This page explains the toolchain, documents the non-obvious traps we
hit while getting it working, and describes the test loop for
validating the runtime against real Skyrim under Proton.

---

## Toolchain

| Tool      | Role                                           |
|-----------|------------------------------------------------|
| `clang-cl`  | C++ compiler, MSVC-compatible driver         |
| `lld-link`  | PE linker (creates `.dll` / `.exe`)          |
| `llvm-lib`  | Static-lib archiver (`.lib`)                 |
| `xwin`    | Pulls MSVC CRT + Windows SDK as a sysroot      |
| `xmake`   | Build system; custom `xwin-clang-cl` toolchain |

LLVM 18+ (Arch: `clang lld llvm`; Debian/Ubuntu: `clang lld llvm-dev`).

**xwin sysroot**, one-time setup:

```bash
XWIN_VERSION=0.8.0
curl -fsSL -o /tmp/xwin.tar.gz \
  "https://github.com/Jake-Shadle/xwin/releases/download/${XWIN_VERSION}/xwin-${XWIN_VERSION}-x86_64-unknown-linux-musl.tar.gz"
tar -xzf /tmp/xwin.tar.gz -C /tmp --strip-components=1
sudo mv /tmp/xwin /usr/local/bin/xwin

xwin --accept-license splat --output "$HOME/.xwin"
```

Override the location by exporting `XWIN_PATH`; the toolchain in
`xmake.lua` reads it.

---

## One-shot build

```bash
xmake f -p windows -a x64 --toolchain=xwin-clang-cl -m release -y
xmake build mora mora_runtime mora_test_harness
```

Outputs land in `build/windows/x64/release/`:

| Target              | Kind   | Purpose                              |
|---------------------|--------|--------------------------------------|
| `mora.exe`          | exe    | CLI compiler (1.2 MB)                |
| `MoraRuntime.dll`   | SKSE   | Runtime plugin, applies patches      |
| `MoraTestHarness.dll` | SKSE | Integration-test plugin (dumpers, TCP) |

Full-from-scratch time on a 17-core workstation: ~90 s (CommonLibSSE-NG
dominates — 477 TUs). Incremental: ~4 s.

---

## The `xwin-clang-cl` toolchain

Defined at the top of `xmake.lua`. The interesting parts:

### `set_kind("standalone")`

Makes `--toolchain=xwin-clang-cl` skip xmake's MSVC detection. Without
this, `-p windows` forces xmake to look for a Visual Studio install
before reading project config.

### `-imsvc<path>` (fused, no space)

MSVC-compat system includes need `-imsvc` — **not** `add_sysincludedirs`,
which xmake's `cl.lua` translates to `-external:I`. With `-external:I`,
xwin's MSVC intrinsic headers (`xmmintrin.h` et al.) win the include-order
race over clang's own, and the MSVC versions only declare intrinsics
as `extern` (expecting MSVC to provide them as compiler builtins). At
link time that's unresolved externs like `_mm_set_ps1`, `_mm_shuffle_ps`.

The path has to be fused to the flag (`-imsvc/home/user/.xwin/crt/include`)
because xmake deduplicates identical tokens in its command-line assembly
and would otherwise emit `-imsvc` once followed by four paths, making
clang-cl read the trailing paths as source files.

### `ar = lld-link`

`lld-link -lib` creates static libs. We route `ar` to `lld-link`
rather than `llvm-lib` because xmake has no tool driver for `llvm-lib`,
but its `link.lua` driver emits `-lib` for `targetkind == "static"`,
which lld-link handles natively.

---

## Traps we hit, and how to recognize them

Keep this section bookmarked — each of these cost a compile cycle or
more to find, and all of them surface in unrelated-looking ways.

### 1. `_mm_set_ps1` / `_mm_shuffle_ps` / `_mm_cvtss_f32` unresolved

**Symptom:** `lld-link: error: undefined symbol: _mm_set_ps1 — referenced
by Actor.cpp.obj:(hkVector4::hkVector4)`

**Cause:** SSE intrinsics are being resolved to MSVC's
`xmmintrin.h` (declaration-only) instead of clang's own (inline
bodies). Means `-external:I` is being used where `-imsvc` was needed
— usually because someone switched the toolchain's `sysincludedirs`
path to `add_sysincludedirs` thinking it's cleaner.

**Fix:** use `-imsvc` (fused) in toolchain `cxflags`/`cflags`.

### 2. Plugin loads, then `SKSE::Init` AVs at 0xC0000005 inside MSVCP140

**Symptom:** SKSE log says
`plugin MoraRuntime.dll ... disabled, fatal error occurred while loading
plugin`. A vectored exception handler shows the fault inside `MSVCP140.dll`
at a null pointer dereference, typically near the `_Mtx_destroy`/`_Mtx_lock`
export cluster. The plugin's `SKSEPlugin_Load` was called and ran its
first log line, then hit the AV on the first `std::mutex::lock()`.

**Cause:** C++ static constructors never ran. `lld-link` picked the
DLL's exported `DllMain` as the entry point directly, bypassing
vcruntime's `_DllMainCRTStartup` wrapper and its
`_initterm(__xc_a, __xc_z)` walk. `static inline std::mutex` members
(e.g., `REL::Module::_initLock` in CommonLibSSE-NG) stayed
zero-initialized; the first lock deref'd a null SRWLock pointer.

**Fix:** `/ENTRY:_DllMainCRTStartup` in the DLL's link flags. Baked
into `mora_runtime`'s `add_shflags` in `xmake.lua`.

### 3. Plugin loads, then AVs at 0xC0000005 inside MSVCP140 *despite* step 2

!!! note "Historical — static-CRT builds don't hit this"
    We now static-link the MSVC CRT on Windows (`set_runtimes("MT")`),
    so `msvcp140.dll` isn't even in the import list. This trap is
    preserved here for archaeology and for anyone who flips back to
    `/MD`.

**Symptom:** Same fault signature as #2 but with CRT init demonstrably
running (you can observe static ctors firing in a debugger, and non-mutex
statics are initialized).

**Cause:** the Wine prefix has an outdated `msvcp140.dll`. Prefixes
seeded with the 2016-era VS2015 redistributable ship a pre-SRWLock
`std::mutex` implementation. Modern MSVC STL zero-initializes
`std::mutex::_Mymtx` and relies on `_Mtx_lock` to lazy-init via
SRWLock semantics on first use; the old runtime doesn't do that, so
the lock immediately dereferences a null `_InternalHandle`.

**Fix (if still on /MD):** replace `drive_c/windows/system32/msvcp140.dll`
in the prefix with the modern Wine-bundled copy from your Proton install:
`$PROTON/lib/wine/x86_64-windows/msvcp140.dll`.
`scripts/test_runtime.sh` still does this automatically and idempotently —
harmless on static-CRT builds.

**Better fix:** static-link the CRT. We do this via `set_runtimes("MT")`
in `xmake.lua`, which statically links `libcmt.lib` + `libcpmt.lib` +
`libvcruntime.lib` + `libucrt.lib`. The resulting binaries import only
`kernel32.dll` plus whatever Windows SDK DLLs they use directly —
**zero** dependency on `msvcp140`, `vcruntime140`, `vcruntime140_1`,
`ucrtbase`, or any `api-ms-win-crt-*`. See "Static-CRT build" below.

### 4. `-Werror` turns clang-cl warnings into hard errors

**Symptom:** `error: private field 'pool_' is not used
[-Werror,-Wunused-private-field]` on Windows, passes on Linux.

**Cause:** clang-cl's `-W3` fires warnings that MSVC itself doesn't
emit (unused-private-field, unused-but-set-variable,
missing-field-initializers). The project's `set_warnings("all", "error")`
turns them into errors under `/WX`.

**Fix:** `set_warnings("all")` (drop `"error"`) when `is_plat("windows")`.
Already applied in `xmake.lua`.

### 5. Static-CRT + xrepo zlib: `/opt:ref: no such file or directory`

**Symptom:** `xmake build mora` fails at the zlib package install step
with `lld-link: error: lib failed` and the stderr shows
`/opt:ref: no such file or directory`.

**Cause:** xmake's `link.lua` tool driver implements `nf_strip` for
release mode, which emits `/opt:ref /opt:icf`. Those are linker flags,
not archiver flags, and `lld-link -lib` rejects them. The zlib xmake-repo
package uses `add_rules("mode.release")` which triggers `strip=all`,
so its static-archive step inherits these linker-only flags and dies.

**Fix in Mora:** `extern/zlib` is a git submodule, and `xmake.lua`
defines a local `zlib` static-lib target on windows that compiles it
in-tree with our own flags. `mora_lib` depends on this target directly.
Linux still pulls `zlib` from xrepo (no collision there — the unix `ar`
driver doesn't auto-append linker flags).

### 6. xmake's package manager tries to run MSVC's CMake generator

**Symptom:** `error: install fmt 12.1.0 .. failed` with `vs not found!`
even with `--toolchain=xwin-clang-cl`.

**Cause:** `package.tools.cmake` hardcodes the "Visual Studio"
generator when the target platform is `windows`, regardless of the
requested toolchain. No consumer-side override.

**Fix in Mora:** don't route CommonLibSSE-NG through xmake's package
manager. Compile its 477 `.cpp` files in-tree with our own
`commonlibsse_ng` static target, using the spdlog/fmt shim at
`extern/spdlog-shim` and a POD SimpleMath shim at `extern/simplemath-shim`.
fmt itself is `add_requires("fmt", {configs = {header_only = true}})`
on Windows — header-only fmt skips the CMake build entirely.

---

## Static-CRT build (no external C++ runtime deps)

All three Windows targets static-link the MSVC CRT. The full set of
DLLs each binary imports after a release build:

| Binary                    | Imports                                                  |
|---------------------------|----------------------------------------------------------|
| `mora.exe`                | `kernel32.dll`                                            |
| `MoraRuntime.dll`         | `kernel32.dll`, `ole32.dll`, `shell32.dll`, `user32.dll`, `version.dll` |
| `MoraTestHarness.dll`     | `kernel32.dll`, `user32.dll`, `version.dll`, `ws2_32.dll` |

No `msvcp140.dll`. No `vcruntime140.dll`. No `vcruntime140_1.dll`. No
`ucrtbase.dll`. No `api-ms-win-crt-*.dll`. Everything runs on bare
Windows / Wine without a VC++ redistributable installed, and prefix
compatibility traps like #3 above simply don't apply.

The setting is `set_runtimes("MT")` inside the `is_plat("windows")`
block at the top of `xmake.lua`. Paired with:

- `set_policy("build.release.strip", false)` — the `/opt:ref /opt:icf`
  flags that `nf_strip` produces are what trigger trap #5; disabling
  the strip policy keeps our own targets clean (xrepo packages are a
  separate story — see trap #5).
- Vendored `extern/zlib` (submodule) so we don't route zlib through
  the xrepo build pipeline that collides with static-CRT archiving.

Size cost: binaries grow by roughly 400-800 KB each (the statically
linked portions of libcmt/libcpmt/libucrt/libvcruntime, minus dead-code
elimination). Trivial compared to eliminating an entire class of
runtime-dependency problems.

---

## Testing the runtime end-to-end

The `scripts/test_runtime.sh` flow targets **GOG Skyrim AE under
GE-Proton** — no Steam DRM in the load path, fastest turnaround, known
to hit `DataLoaded` in ~15–20 seconds.

Prerequisites, all paths hardcoded in the script:

| What                              | Where                                                                                              |
|-----------------------------------|----------------------------------------------------------------------------------------------------|
| GOG Skyrim AE 1.6.1179            | `~/Games/gog/the-elder-scrolls-v-skyrim-special-edition/drive_c/GOG Games/Skyrim Anniversary Edition/` |
| GOG prefix                        | `~/Games/gog/the-elder-scrolls-v-skyrim-special-edition/`                                          |
| GOG-specific SKSE 2.2.6           | `skse64_2_02_06_gog.7z` from <https://skse.silverlock.org/beta/>                                   |
| Address Library 1.6.1179          | `versionlib-1-6-1179-0.bin` in `Data/SKSE/Plugins/`                                                |
| GE-Proton 10-34                   | `~/.local/share/Steam/compatibilitytools.d/GE-Proton10-34/`                                        |
| Hyprland w/ XWayland on `:1`      | (for spawning Skyrim on a dedicated workspace without stealing focus)                              |

Run:

```bash
scripts/test_runtime.sh [path/to/mora_patches.bin]
```

The script:

1. Refreshes the prefix's `msvcp140.dll` from GE-Proton's Wine copy if
   newer (trap #3 above).
2. Copies `build/windows/x64/release/MoraRuntime.dll` into the GOG
   plugin folder.
3. Optionally copies a `mora_patches.bin` argument over the current one.
4. Clears old `MoraRuntime.log` / `skse64.log`.
5. Spawns `skse64_loader.exe` on Hyprland workspace 10 (silent).
6. Polls `MoraRuntime.log` for `SKSE event hooks registered` or
   `No patches`, then kills wineserver for that prefix and reports
   PASS/FAIL based on whether any patches applied or the DAG loaded.

A healthy run produces:

```
[Mora] DLL directory: C:\GOG Games\...\SKSE\Plugins
[Mora] Looking for patches at: ...\mora_patches.bin
[Mora] Loaded 2565877 static patches
[Mora] Applied 2565877 patches in 16.12 ms
[Mora] DAG loaded: N nodes
[Mora] SKSE event hooks registered (N needed)
```

If any message type in `{0, 1, 6, 8}` fires in `skse64.log` and our
log records `DataLoaded` / `DLL directory`, the MSVC C++ ABI is
verified compatible at runtime (vtables, name-mangled exports,
std::mutex, `<format>`, `std::filesystem`, CommonLibSSE-NG's REL
machinery — everything got exercised).

---

## CI

`.github/workflows/ci.yml` has three jobs:

1. **`linux`** — native build + `xmake test` (93 tests).
2. **`extension`** — builds + smoke-tests the VS Code extension.
3. **`windows-xcompile`** — Ubuntu runner, installs `clang`/`lld`/`llvm`,
   downloads+caches xwin 0.8.0, splats the sysroot, then runs
   `xmake build mora mora_runtime mora_test_harness`.

No Windows runner. The release zip is packaged directly from the
Ubuntu cross-compile. Publishing to Nexus Mods happens from the same
job on tag pushes.

Cache keys:

- `xmake-xcompile-${{ hashFiles('xmake.lua') }}` — xmake package cache
- `xwin-sysroot-v0.8.0` — xwin sysroot splat; bump the key when you
  bump xwin's version.
