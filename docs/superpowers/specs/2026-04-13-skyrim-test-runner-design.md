# Skyrim Integration Test Runner — Design Spec

## Problem

Mora compiles .mora and INI rules into a native SKSE DLL (MoraRuntime.dll). Unit tests validate the compiler pipeline, but there's no automated way to verify that the generated DLL actually works inside Skyrim — that patches are applied correctly, keywords are added, names are changed, etc.

The existing TCP test harness (MoraTestHarness.dll, port 9742) is proven to work inside a running Skyrim instance but requires manual deployment and launch.

## Goal

A Docker image that can boot Skyrim SE (1.6.1170) headlessly via Wine with software rendering, load SKSE + test plugins, and expose the TCP harness for automated validation. Combined with a test runner, this enables regression testing of the full compile → runtime pipeline.

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Host (Unraid / Dev Machine / CI Runner)        │
│                                                 │
│  Skyrim game files (bind mount, read-only)      │
│  Test artifacts: MoraRuntime.dll, test configs  │
│                                                 │
│  ┌───────────────────────────────────────────┐  │
│  │  skyrim-docker container                  │  │
│  │                                           │  │
│  │  Wine 11.x + Mesa llvmpipe (sw render)    │  │
│  │  Xvfb :99 (virtual framebuffer)           │  │
│  │                                           │  │
│  │  ┌─────────────────────────────────┐      │  │
│  │  │  SkyrimSE.exe (via Wine)        │      │  │
│  │  │  ├── SKSE64 loader              │      │  │
│  │  │  ├── MoraTestHarness.dll        │      │  │
│  │  │  └── MoraRuntime.dll (under test)│     │  │
│  │  └──────────────┬──────────────────┘      │  │
│  │                 │ TCP :9742               │  │
│  │  ┌──────────────▼──────────────────┐      │  │
│  │  │  Entrypoint script              │      │  │
│  │  │  - Start Xvfb                   │      │  │
│  │  │  - Launch Skyrim via Wine       │      │  │
│  │  │  - Wait for TCP :9742 ready     │      │  │
│  │  │  - Expose port to host          │      │  │
│  │  └─────────────────────────────────┘      │  │
│  └───────────────────────────────────────────┘  │
│                                                 │
│  Test runner (on host or in CI):                │
│  - docker run skyrim-docker                     │
│  - Connect TCP :9742                            │
│  - Send commands (dump weapons, etc.)           │
│  - Compare output against expected values       │
│  - docker stop                                  │
└─────────────────────────────────────────────────┘
```

## Component 1: Docker Image (`skyrim-docker`)

**Repo:** `~/oss/skyrim-docker`

**Base:** `debian:bookworm-slim` (small, stable, Wine packages available)

**Installed packages:**
- `wine64` (or WineHQ stable from their repo)
- `xvfb` — virtual framebuffer
- `mesa-utils`, `libgl1-mesa-dri` — Mesa llvmpipe software renderer
- `netcat-openbsd` — for TCP readiness probing

**What's IN the image:**
- Wine runtime + Mesa + Xvfb
- Entrypoint script that orchestrates startup
- SKSE64 binaries (small, ~2MB, can be baked in or mounted)

**What's NOT in the image (bind-mounted at runtime):**
- Skyrim game files (~20GB) — mounted read-only at `/skyrim`
- Test artifacts (MoraRuntime.dll, MoraTestHarness.dll, test configs) — mounted at `/test`
- Wine prefix — either pre-initialized in the image or generated on first run and cached

**Entrypoint script (`entrypoint.sh`):**
```
1. Start Xvfb on :99
2. Export DISPLAY=:99, LIBGL_ALWAYS_SOFTWARE=1
3. Set up Wine prefix if not cached
4. Symlink/copy Skyrim files into Wine prefix (C:\Skyrim\)
5. Deploy SKSE + test DLLs into Data/SKSE/Plugins/
6. Launch SkyrimSE.exe via Wine
7. Poll TCP :9742 until harness responds (timeout: 120s)
8. Print "READY" to stdout
9. Wait for Wine process to exit
```

**Port exposure:** TCP 9742 mapped to host.

**Usage:**
```bash
docker run --rm \
  -v /path/to/skyrim:/skyrim:ro \
  -v /path/to/test-artifacts:/test:ro \
  -p 9742:9742 \
  skyrim-docker
```

## Component 2: Wine Prefix Strategy

Skyrim doesn't need a full Windows install — it needs:
- A Wine prefix with basic Windows DLLs
- SKSE64 loader (`skse64_loader.exe`, `skse64_1_6_1170.dll`)
- The game files accessible at a known path

Two approaches:

**A. Pre-baked prefix in image** — Run `wineboot` during `docker build`, save the prefix. Faster startup (~0 init overhead) but larger image (~500MB prefix).

**B. Ephemeral prefix** — Run `wineboot` in entrypoint. Adds ~5-10 seconds to startup but keeps the image smaller.

**Recommendation:** Pre-baked prefix (A). The image size increase is negligible compared to the 20GB game files we're already mounting. Faster cold start matters for test iteration.

## Component 3: SKSE Loading

SKSE loads by running `skse64_loader.exe` instead of `SkyrimSE.exe` directly. The loader injects the SKSE DLL into Skyrim's process.

The entrypoint launches:
```bash
wine skse64_loader.exe
```

SKSE then loads all DLLs from `Data/SKSE/Plugins/`:
- `MoraTestHarness.dll` — the TCP harness (always present)
- `MoraRuntime.dll` — the DLL under test (provided by the test suite)

## Component 4: Readiness Detection

The entrypoint polls for TCP 9742 to become available:
```bash
for i in $(seq 1 120); do
  nc -z localhost 9742 && echo "READY" && break
  sleep 1
done
```

The test runner on the host waits for "READY" on stdout (or polls the port directly if running detached).

## Component 5: Known Risks & Mitigations

**Risk: Skyrim crashes under Wine + llvmpipe**
- Skyrim SE runs well under Proton/Wine. Software rendering may trigger different codepaths.
- Mitigation: If the renderer crashes, we may need specific Wine DLL overrides (`WINEDLLOVERRIDES="d3d11=n"`) or DXVK with a null/software backend.
- Fallback: Use `PROTON_USE_WINED3D=1` which uses Wine's built-in D3D→OpenGL translation (works with llvmpipe).

**Risk: Skyrim hangs waiting for user input (EULA, first-run)**
- Mitigation: Pre-create registry entries in the Wine prefix that mark EULA as accepted and skip first-run dialogs.

**Risk: Address Library not found**
- The test harness uses Address Library for AE 1.6.1170. The versionlib .bin file must be deployed.
- Mitigation: Include the Address Library bin in the test artifacts mount.

**Risk: Skyrim tries to go online (Bethesda.net, Creation Club)**
- Mitigation: Block network access to the container (`--network none`) or provide a minimal `/etc/hosts` blacklist.

## Non-Goals (for now)

- Running with mods beyond the test DLLs
- GPU-accelerated rendering
- Multiple Skyrim versions (hardcode 1.6.1170 first)
- Automated game file downloading (manual mount)
- Test case format / test runner orchestration (separate spec, lives in Mora repo)

## Verification

The image is "done" when:
```bash
docker run --rm -v /path/to/skyrim:/skyrim:ro -p 9742:9742 skyrim-docker
# In another terminal:
echo "status" | nc localhost 9742
# Returns: {"ok":true,"forms_loaded":true,...}
```
