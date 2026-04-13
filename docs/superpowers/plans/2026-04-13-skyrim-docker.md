# Skyrim Docker Test Runner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Docker image that boots Skyrim SE 1.6.1170 headlessly via Wine with software rendering, loads SKSE + test plugins, and exposes TCP port 9742 for automated integration testing.

**Architecture:** Debian bookworm + WineHQ + Xvfb + Mesa llvmpipe in a single Docker image. Game files are bind-mounted read-only. A pre-baked Wine prefix avoids startup overhead. The entrypoint launches SKSE, polls for TCP readiness, and exposes the harness port.

**Tech Stack:** Docker, Wine 11.x (WineHQ), Xvfb, Mesa llvmpipe, bash

---

## File Structure

```
~/oss/skyrim-docker/
├── Dockerfile                 # Image definition
├── entrypoint.sh              # Container startup orchestration
├── setup-prefix.sh            # Wine prefix initialization (runs during docker build)
├── skyrim.ini                 # Minimal Skyrim.ini for headless operation
├── skyrimprefs.ini            # Minimal SkyrimPrefs.ini (lowest graphics settings)
├── skse.ini                   # SKSE config (enable minidumps)
├── hosts-blocklist            # Block Bethesda.net / Creation Club DNS
├── README.md                  # Usage documentation
└── .dockerignore              # Exclude unnecessary files from build context
```

---

### Task 1: Initialize Repository

**Files:**
- Create: `~/oss/skyrim-docker/.gitignore`
- Create: `~/oss/skyrim-docker/.dockerignore`
- Create: `~/oss/skyrim-docker/README.md`

- [ ] **Step 1: Create the repo**

```bash
mkdir -p ~/oss/skyrim-docker
cd ~/oss/skyrim-docker
git init
```

- [ ] **Step 2: Create .gitignore**

Write `~/oss/skyrim-docker/.gitignore`:
```
*.dll
*.exe
*.bin
*.bsa
*.esm
*.esp
*.esl
```

- [ ] **Step 3: Create .dockerignore**

Write `~/oss/skyrim-docker/.dockerignore`:
```
.git
README.md
```

- [ ] **Step 4: Create README.md**

Write `~/oss/skyrim-docker/README.md`:
```markdown
# skyrim-docker

Docker image for running Skyrim SE 1.6.1170 headlessly via Wine + software rendering.
Designed for automated integration testing of SKSE plugins.

## Usage

```bash
docker build -t skyrim-docker .

docker run --rm \
  -v /path/to/skyrim:/skyrim:ro \
  -v /path/to/test-artifacts:/test:ro \
  -p 9742:9742 \
  skyrim-docker
```

### Volumes

- `/skyrim` (read-only) — Skyrim SE game directory containing `SkyrimSE.exe`, `Data/`, SKSE loader files
- `/test` (read-only) — Test artifacts: SKSE plugin DLLs, Address Library `.bin` files, `MoraCache/`

### Ports

- `9742` — TCP harness port for sending commands (`status`, `dump weapons`, `lookup <formid>`, `quit`)

### Requirements

- Skyrim SE 1.6.1170 (Anniversary Edition)
- SKSE64 for 1.6.1170 (`skse64_loader.exe`, `skse64_1_6_1170.dll`) in the Skyrim directory
- Address Library (`versionlib-1-6-1170-0.bin`) in the test artifacts

### Environment Variables

- `SKYRIM_TIMEOUT` — Seconds to wait for TCP readiness (default: 120)
- `SKYRIM_PORT` — TCP harness port (default: 9742)
```

- [ ] **Step 5: Commit**

```bash
cd ~/oss/skyrim-docker
git add -A
git commit -m "Initial repo structure with README"
```

---

### Task 2: Create Skyrim Configuration Files

**Files:**
- Create: `~/oss/skyrim-docker/skyrim.ini`
- Create: `~/oss/skyrim-docker/skyrimprefs.ini`
- Create: `~/oss/skyrim-docker/skse.ini`
- Create: `~/oss/skyrim-docker/hosts-blocklist`

- [ ] **Step 1: Create minimal Skyrim.ini**

Write `~/oss/skyrim-docker/skyrim.ini`:
```ini
[General]
sLanguage=ENGLISH
sTestFile1=Dawnguard.esm
sTestFile2=HearthFires.esm
sTestFile3=Dragonborn.esm
uExterior Cell Buffer=36
bBorderRegionsEnabled=0

[Display]
iShadowMapResolutionPrimary=256
bAllowScreenshot=0
bFull Screen=0

[Interface]
bShowTutorials=0
```

- [ ] **Step 2: Create minimal SkyrimPrefs.ini**

These settings minimize rendering work for software rendering:
Write `~/oss/skyrim-docker/skyrimprefs.ini`:
```ini
[Display]
bFull Screen=0
iSize H=480
iSize W=640
iShadowMapResolution=256
fShadowDistance=0.0000
bUseTAA=0
bSAOEnable=0
bScreenSpaceReflectionEnabled=0
bVolumetricLightingEnable=0
bEnableImprovedSnow=0
bFXAAEnabled=0
bIBLFEnable=0
bUse64bitsHDRRenderTarget=0
iMultiSample=0
iMaxAnisotropy=0
bTreesReceiveShadows=0
bDrawLandShadows=0
bShadowsOnGrass=0

[General]
fDefaultWorldFOV=90
bIntroSequence=0
bModManagerMenuEnabled=0

[Launcher]
bEnableFileSelection=1

[MAIN]
bGamepadEnable=0
```

- [ ] **Step 3: Create SKSE config**

Write `~/oss/skyrim-docker/skse.ini`:
```ini
[Debug]
WriteMinidumps=1
```

- [ ] **Step 4: Create hosts blocklist**

Write `~/oss/skyrim-docker/hosts-blocklist`:
```
127.0.0.1 accounts.bethesda.net
127.0.0.1 api.bethesda.net
127.0.0.1 content.bethesda.net
127.0.0.1 creationclub.bethesda.net
127.0.0.1 logs.bethesda.net
```

- [ ] **Step 5: Commit**

```bash
cd ~/oss/skyrim-docker
git add -A
git commit -m "Add Skyrim/SKSE config files for headless operation"
```

---

### Task 3: Create Wine Prefix Setup Script

**Files:**
- Create: `~/oss/skyrim-docker/setup-prefix.sh`

- [ ] **Step 1: Write prefix setup script**

Write `~/oss/skyrim-docker/setup-prefix.sh`:
```bash
#!/bin/bash
set -euo pipefail

# Initialize a minimal Wine prefix for Skyrim SE.
# This runs during docker build to pre-bake the prefix.

export WINEPREFIX=/opt/skyrim-prefix
export WINEARCH=win64
export WINEDEBUG=-all
export DISPLAY=:99

echo "=== Initializing Wine prefix at $WINEPREFIX ==="
wineboot --init
wineserver --wait

# Set Windows version to Windows 10 (Skyrim SE requirement)
wine reg add "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion" \
    /v CurrentBuildNumber /t REG_SZ /d 19041 /f
wine reg add "HKLM\\Software\\Microsoft\\Windows NT\\CurrentVersion" \
    /v ProductName /t REG_SZ /d "Windows 10" /f

# Pre-register Bethesda Softworks path (Skyrim expects this)
wine reg add "HKLM\\Software\\Wow6432Node\\Bethesda Softworks\\Skyrim Special Edition" \
    /v "Installed Path" /t REG_SZ /d "Z:\\skyrim\\" /f

# Enable Wine virtual desktop (renders to internal buffer, no real display needed)
wine reg add "HKCU\\Software\\Wine\\Explorer\\Desktops" \
    /v Default /t REG_SZ /d 640x480 /f

wineserver --wait
echo "=== Wine prefix ready ==="
```

- [ ] **Step 2: Make executable**

```bash
chmod +x ~/oss/skyrim-docker/setup-prefix.sh
```

- [ ] **Step 3: Commit**

```bash
cd ~/oss/skyrim-docker
git add setup-prefix.sh
git commit -m "Add Wine prefix setup script"
```

---

### Task 4: Create Entrypoint Script

**Files:**
- Create: `~/oss/skyrim-docker/entrypoint.sh`

- [ ] **Step 1: Write entrypoint script**

Write `~/oss/skyrim-docker/entrypoint.sh`:
```bash
#!/bin/bash
set -euo pipefail

SKYRIM_DIR="/skyrim"
TEST_DIR="/test"
WINEPREFIX="/opt/skyrim-prefix"
TIMEOUT="${SKYRIM_TIMEOUT:-120}"
PORT="${SKYRIM_PORT:-9742}"

export WINEPREFIX
export WINEARCH=win64
export WINEDEBUG=-all
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_GL_VERSION_OVERRIDE=4.5

# ── Validate mounts ─────────────────────────────────────────────
if [ ! -f "$SKYRIM_DIR/SkyrimSE.exe" ]; then
    echo "ERROR: SkyrimSE.exe not found at $SKYRIM_DIR/SkyrimSE.exe"
    echo "Mount your Skyrim SE directory to /skyrim"
    exit 1
fi

if [ ! -f "$SKYRIM_DIR/skse64_loader.exe" ]; then
    echo "ERROR: skse64_loader.exe not found. SKSE must be installed in the Skyrim directory."
    exit 1
fi

# ── Start virtual framebuffer ───────────────────────────────────
echo "Starting Xvfb..."
Xvfb :99 -screen 0 640x480x24 -nolisten tcp &
XVFB_PID=$!
export DISPLAY=:99
sleep 1

# ── Create game directory structure ─────────────────────────────
# Skyrim needs a writable Data/ directory for logs and dumps.
# We overlay the read-only mount with writable layers.
GAME_DIR="/opt/skyrim-game"
mkdir -p "$GAME_DIR"

# Symlink top-level game files
for f in "$SKYRIM_DIR"/*; do
    fname=$(basename "$f")
    if [ "$fname" != "Data" ]; then
        ln -sf "$f" "$GAME_DIR/$fname"
    fi
done

# Create writable Data directory with symlinks to read-only content
mkdir -p "$GAME_DIR/Data"
for f in "$SKYRIM_DIR/Data"/*; do
    fname=$(basename "$f")
    if [ "$fname" != "SKSE" ]; then
        ln -sf "$f" "$GAME_DIR/Data/$fname"
    fi
done

# Create writable SKSE directory structure
mkdir -p "$GAME_DIR/Data/SKSE/Plugins"

# Copy SKSE config
cp /opt/config/skse.ini "$GAME_DIR/Data/SKSE/skse.ini"

# Link existing SKSE plugins from the game directory (Address Library, etc.)
if [ -d "$SKYRIM_DIR/Data/SKSE/Plugins" ]; then
    for f in "$SKYRIM_DIR/Data/SKSE/Plugins"/*; do
        fname=$(basename "$f")
        ln -sf "$f" "$GAME_DIR/Data/SKSE/Plugins/$fname"
    done
fi

# ── Deploy test artifacts ───────────────────────────────────────
if [ -d "$TEST_DIR" ]; then
    echo "Deploying test artifacts from $TEST_DIR..."
    # Copy DLLs (overrides any symlinked versions)
    for dll in "$TEST_DIR"/*.dll; do
        [ -f "$dll" ] && cp "$dll" "$GAME_DIR/Data/SKSE/Plugins/"
    done
    # Copy Address Library bins
    for bin in "$TEST_DIR"/*.bin; do
        [ -f "$bin" ] && cp "$bin" "$GAME_DIR/Data/SKSE/Plugins/"
    done
    # Copy MoraCache if present
    if [ -d "$TEST_DIR/MoraCache" ]; then
        cp -r "$TEST_DIR/MoraCache" "$GAME_DIR/Data/MoraCache"
    fi
fi

# Create writable dump output directory
mkdir -p "$GAME_DIR/Data/MoraCache/dumps"

# ── Set up Skyrim INI files ─────────────────────────────────────
DOCS_DIR="$WINEPREFIX/drive_c/users/$(whoami)/Documents/My Games/Skyrim Special Edition"
mkdir -p "$DOCS_DIR/SKSE"
cp /opt/config/skyrim.ini "$DOCS_DIR/Skyrim.ini"
cp /opt/config/skyrimprefs.ini "$DOCS_DIR/SkyrimPrefs.ini"

# ── Apply hosts blocklist ───────────────────────────────────────
cat /opt/config/hosts-blocklist >> /etc/hosts 2>/dev/null || true

# ── Launch Skyrim via SKSE ──────────────────────────────────────
echo "Launching Skyrim SE via SKSE..."
cd "$GAME_DIR"
wine skse64_loader.exe &
WINE_PID=$!

# ── Wait for TCP harness readiness ──────────────────────────────
echo "Waiting for TCP harness on port $PORT (timeout: ${TIMEOUT}s)..."
READY=0
for i in $(seq 1 "$TIMEOUT"); do
    if nc -z localhost "$PORT" 2>/dev/null; then
        READY=1
        break
    fi
    # Check if Wine process died
    if ! kill -0 "$WINE_PID" 2>/dev/null; then
        echo "ERROR: Skyrim process exited prematurely"
        # Print any Wine error output
        cat "$DOCS_DIR/SKSE/skse64.log" 2>/dev/null || true
        kill "$XVFB_PID" 2>/dev/null || true
        exit 1
    fi
    sleep 1
done

if [ "$READY" -eq 0 ]; then
    echo "ERROR: Timed out waiting for TCP harness on port $PORT"
    kill "$WINE_PID" 2>/dev/null || true
    kill "$XVFB_PID" 2>/dev/null || true
    exit 1
fi

echo "READY"

# ── Keep running until Wine exits or we receive SIGTERM ─────────
cleanup() {
    echo "Shutting down..."
    # Try graceful shutdown via harness
    echo "quit" | nc -w 2 localhost "$PORT" 2>/dev/null || true
    sleep 2
    kill "$WINE_PID" 2>/dev/null || true
    wineserver -k 2>/dev/null || true
    kill "$XVFB_PID" 2>/dev/null || true
}
trap cleanup SIGTERM SIGINT

wait "$WINE_PID" 2>/dev/null || true
cleanup
```

- [ ] **Step 2: Make executable**

```bash
chmod +x ~/oss/skyrim-docker/entrypoint.sh
```

- [ ] **Step 3: Commit**

```bash
cd ~/oss/skyrim-docker
git add entrypoint.sh
git commit -m "Add container entrypoint with Xvfb, Wine, and readiness polling"
```

---

### Task 5: Create Dockerfile

**Files:**
- Create: `~/oss/skyrim-docker/Dockerfile`

- [ ] **Step 1: Write Dockerfile**

Write `~/oss/skyrim-docker/Dockerfile`:
```dockerfile
FROM debian:bookworm-slim

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# ── Install Wine from WineHQ ────────────────────────────────────
RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        wget \
        gnupg2 && \
    # Add WineHQ repository
    mkdir -pm755 /etc/apt/keyrings && \
    wget -O /etc/apt/keyrings/winehq-archive.key \
        https://dl.winehq.org/wine-builds/winehq.key && \
    wget -NP /etc/apt/sources.list.d/ \
        https://dl.winehq.org/wine-builds/debian/dists/bookworm/winehq-bookworm.sources && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        winehq-stable && \
    # Clean up wget/gnupg (no longer needed)
    apt-get purge -y wget gnupg2 && \
    apt-get autoremove -y && \
    rm -rf /var/lib/apt/lists/*

# ── Install Xvfb, Mesa (software renderer), and utilities ──────
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        xvfb \
        libgl1-mesa-dri \
        mesa-utils \
        netcat-openbsd \
        procps && \
    rm -rf /var/lib/apt/lists/*

# ── Copy config files ──────────────────────────────────────────
COPY skyrim.ini skyrimprefs.ini skse.ini hosts-blocklist /opt/config/

# ── Pre-bake Wine prefix ───────────────────────────────────────
COPY setup-prefix.sh /opt/setup-prefix.sh
# Need a temporary Xvfb for wineboot during build
RUN Xvfb :99 -screen 0 640x480x24 -nolisten tcp & \
    sleep 1 && \
    /opt/setup-prefix.sh && \
    rm -f /tmp/.X99-lock

# ── Copy entrypoint ───────────────────────────────────────────
COPY entrypoint.sh /opt/entrypoint.sh

# ── Runtime configuration ─────────────────────────────────────
ENV WINEPREFIX=/opt/skyrim-prefix
ENV WINEARCH=win64
ENV WINEDEBUG=-all
ENV LIBGL_ALWAYS_SOFTWARE=1
ENV MESA_GL_VERSION_OVERRIDE=4.5

EXPOSE 9742

ENTRYPOINT ["/opt/entrypoint.sh"]
```

- [ ] **Step 2: Commit**

```bash
cd ~/oss/skyrim-docker
git add Dockerfile
git commit -m "Add Dockerfile with Wine, Xvfb, Mesa, and pre-baked prefix"
```

---

### Task 6: Build and Test the Image

- [ ] **Step 1: Build the Docker image**

```bash
cd ~/oss/skyrim-docker
docker build -t skyrim-docker .
```

This will take several minutes (Wine download + prefix initialization). Expected output ends with:
```
=== Wine prefix ready ===
Successfully built <hash>
Successfully tagged skyrim-docker:latest
```

- [ ] **Step 2: Run a smoke test**

Set the Skyrim path variable for convenience:
```bash
SKYRIM_DIR="$HOME/.local/share/Steam/steamapps/common/Skyrim Special Edition"
```

Run the container:
```bash
docker run --rm \
  -v "$SKYRIM_DIR:/skyrim:ro" \
  -p 9742:9742 \
  --name skyrim-test \
  skyrim-docker
```

Expected output (in ~30-90 seconds):
```
Starting Xvfb...
Launching Skyrim SE via SKSE...
Waiting for TCP harness on port 9742 (timeout: 120s)...
READY
```

- [ ] **Step 3: Test TCP harness from host**

In a separate terminal:
```bash
echo "status" | nc localhost 9742
```

Expected response:
```json
{"ok":true,"forms_loaded":true,"capacity":...,"entries_ptr":"0x...","sentinel_ptr":"0x..."}
```

- [ ] **Step 4: Test weapon dump**

```bash
echo "dump weapons" | nc localhost 9742
```

Expected: the command returns and a JSONL file is created inside the container at `Data/MoraCache/dumps/weapons.jsonl`.

- [ ] **Step 5: Shut down cleanly**

```bash
echo "quit" | nc localhost 9742
```

Or from the first terminal: `Ctrl+C` (sends SIGTERM, triggers cleanup).

- [ ] **Step 6: Commit any fixes**

If any changes were needed to make the build or test pass, commit them:
```bash
cd ~/oss/skyrim-docker
git add -A
git commit -m "Fix: <describe what was fixed>"
```

---

### Task 7: Troubleshooting Fallbacks

If Skyrim fails to start under plain Wine + llvmpipe, try these modifications to `entrypoint.sh` and rebuild:

- [ ] **Fallback A: WineD3D instead of default D3D**

Add before the `wine skse64_loader.exe` line:
```bash
export PROTON_USE_WINED3D=1
```

This forces Wine's built-in D3D→OpenGL translation which is known to work with llvmpipe.

- [ ] **Fallback B: D3D DLL overrides**

Add before the `wine skse64_loader.exe` line:
```bash
export WINEDLLOVERRIDES="d3d11=b;dxgi=b"
```

This forces Wine's built-in D3D11 and DXGI implementations over any native overrides.

- [ ] **Fallback C: Use DXVK with null device**

If Wine's D3D fails entirely, install DXVK in the prefix during build:
```bash
# In setup-prefix.sh, add:
wget https://github.com/doitsujin/dxvk/releases/download/v2.5.3/dxvk-2.5.3.tar.gz
tar xf dxvk-2.5.3.tar.gz
cd dxvk-2.5.3
./setup_dxvk.sh install
```

Then in entrypoint.sh:
```bash
export DXVK_CONFIG="dxgi.nvapiHack = False"
```

- [ ] **Commit whichever fallback works**

```bash
cd ~/oss/skyrim-docker
git add -A
git commit -m "Fix rendering: <describe which approach worked>"
```

---

### Task 8: Document Final Working Configuration

- [ ] **Step 1: Update README with verified usage**

After confirming the image works, update `~/oss/skyrim-docker/README.md` with:
- The exact `docker run` command that works
- Any Wine DLL overrides or environment variables required
- Known startup time on the test machine
- How to retrieve dump files from the container

- [ ] **Step 2: Final commit**

```bash
cd ~/oss/skyrim-docker
git add -A
git commit -m "docs: document verified working configuration"
```
