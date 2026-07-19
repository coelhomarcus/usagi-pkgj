# Usagi PKGj — Developer Guide

This document covers: what changed in this fork, how to build, how to test via the CLI simulator, and how to generate the final Vita binary (`.vpk`).

---

## Table of Contents

1. [What was changed and why](#1-what-was-changed-and-why)
2. [Feature: cover-art grid view](#2-feature-cover-art-grid-view)
3. [How to build (CLI / host simulator)](#3-how-to-build-cli--host-simulator)
4. [Testing with the CLI simulator](#4-testing-with-the-cli-simulator)
5. [How to generate the Vita binary](#5-how-to-generate-the-vita-binary)
6. [File change summary](#6-file-change-summary)

---

## 1. What was changed and why

Usagi PKGj is a fork of [blastrock/pkgj](https://github.com/blastrock/pkgj). Its main addition is a **cover-art grid view** for the PS Vita games list: an alternative to the plain-text list, showing each game's box art in a scrollable grid. Covers are fetched on demand and cached locally, with an optional one-time bulk sync for players who'd rather not see them pop in while scrolling.

This fork also removes a few features that existed on top of upstream at various points (personal annotations/flags, per-title comments, in-game screenshots, the GameView PS Store description panel) to keep the game detail screen focused and its focus/navigation model simpler — GameView now goes straight to "Install Game" instead of requiring a left/right panel pick first.

---

## 2. Feature: cover-art grid view

### What it does

- Toggle **"Grid view (games)"** in the triangle options menu to switch the PS Vita games list between the classic text list and a cover-art grid (3 columns × 2 rows per page).
- Selecting a cell and pressing X opens the same GameView detail screen the list uses.
- Triangle still opens the options menu from the grid; L1/R1 still jump alphabetically by name group, same as the list.

### Where covers come from

Implemented in `ImageFetcher` (`src/imagefetcher.{hpp,cpp}`), shared by GameView (single cover) and the grid (one fetcher per visible cell). For each title it tries an ordered list of sources, falling back on a 404/failure:

1. **[HexFlow-Covers](https://github.com/Andiweli/HexFlow-Covers)** — vertical PS Vita box art (PNG), the default source.
2. **PlayStation Store** — JPEG cover, used as a fallback for titles HexFlow doesn't have.

Setting `thumbnail_url` in `config.txt` overrides both with a custom single source (`{thumbnail_url}/{titleid}.jpg`) — matches the pre-existing custom-thumbnail behavior.

Downloads are serialized through a single global `WorkerSlot` (`src/workerpool.hpp`) — only one cover downloads at a time, on-device or in the simulator — and cached to disk (`thumbnail_folder`, default `ux0:usagi-pkgj/cover`) so a title's cover is only ever fetched once.

### Pagination / memory

The grid only ever keeps `ImageFetcher` instances (and therefore GPU textures) for the currently visible page (`GridImageCache` in `src/gridview.cpp`) — as you scroll, cells that leave the visible window are evicted and their textures freed. This is the "pagination" that keeps VRAM usage bounded regardless of how large the games list is.

### Placeholder art

Cells without a cached cover yet show `assets/covers/loading.png` (while fetching) or `assets/covers/noimage.png` (fetch failed / no source has this title) instead of a plain colored box. Embedded into the `.vpk` the same way as every other bundled Vita UI asset (see `cross.cmake`'s `add_assets`); the host simulator has no such embedding step and falls back to a plain rect+text placeholder there.

---

## 3. How to build (CLI / host simulator)

### Prerequisites

- Linux (Debian/Ubuntu recommended)
- `gcc-12` and `g++-12`
- Python 3.6+ with [Poetry](https://python-poetry.org/)
- Internet access (Conan downloads dependencies on first build)

### Steps

```bash
# 1. Enter the ci/ directory
cd /path/to/pkgj/ci

# 2. Install Conan via Poetry and configure profiles (first time only)
./setup_conan.sh

# 3. Install host dependencies
export CC=gcc-12 CXX=g++-12
mkdir buildhost
poetry run conan install .. \
  --build missing \
  -s build_type=RelWithDebInfo \
  -s compiler=gcc \
  -s compiler.version=12 \
  -s compiler.libcxx=libstdc++11 \
  --output-folder buildhost

# 4. Compile
poetry run conan build .. \
  -s build_type=RelWithDebInfo \
  -s compiler=gcc \
  -s compiler.version=12 \
  -s compiler.libcxx=libstdc++11 \
  --output-folder buildhost
```

The resulting binary is at `ci/buildhost/pkgj_cli`.

> **Note:** Steps 3 and 4 also configure and build via CMake internally. After the first build, you can rebuild faster using:
> ```bash
> cd ci/buildhost && source conanbuild.sh && cmake --build . --target pkgj_cli
> ```
>
> To also build the graphical simulator (`pkgj_sim`, needs `libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libcurl-dev`), add `-DBUILD_SIM=ON` when invoking cmake directly against `ci/buildhost` (see `host.cmake`).

---

## 4. Testing with the CLI simulator

The `pkgj_cli` binary uses `simulator.cpp` to replace all Vita-specific syscalls (file I/O, memory, time) with standard POSIX equivalents, allowing the database, download, and extraction logic to be tested on a regular Linux machine.

> **Important:** `FileHttp` in the simulator reads **local files**, not real HTTP URLs. To test with online data, download the file first with `curl` and pass the local path.

### Available subcommands

```
pkgj_cli refreshlist    <MODE> <tsv_file>
pkgj_cli filedownload   <local_file_or_url>
pkgj_cli extractzip     <zip_file>
pkgj_cli refreshcomppack <local_file>
pkgj_cli extract        <pkg_file> <zrif> <sha256>
pkgj_cli patchinfo      <xml_file> <titleid>
```

---

### Test 1 — Parse a real TSV database

Download the community PSVita game list and parse it:

```bash
cd ci/buildhost

# Download the TSV
curl -L "https://raw.githubusercontent.com/txy7795679/PSVITA-PKGJ-DATADB/refs/heads/master/PSV_GAMES.tsv" \
     -o PSV_GAMES.tsv

# Parse and display titles sorted by size
./pkgj_cli refreshlist PSVGAMES PSV_GAMES.tsv | head -20
```

Expected output (titles sorted descending by file size):
```
The Lost Child (3.61+!) [3.65]: 3537465536
The Sly Trilogy: 3526375296
...
Persona 4: The Golden (PlayStation Vita the Best): 3335541664
```

> Note: `TitleDatabase::reload()`, used by the app itself (and by the grid/GameView cover lookups), expects the TSV's rows to be CRLF-terminated — matches the format community title databases ship in. LF-only test fixtures will parse as a single row.

---

### Test 2 — Extract a zip (compatibility pack simulation)

The `extractzip` command simulates how PKGj extracts compatibility packs on the Vita. It extracts to `./tmp/`.

Create a test zip (Python, since `zip` may not be installed):

```bash
cd ci/buildhost

python3 - <<'EOF'
import zipfile
with zipfile.ZipFile('test_pack.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    # Directory entries must come before their files (required by extractzip)
    z.mkdir('sce_sys')
    z.writestr('sce_sys/param.sfo', b'\x00PSF' + b'fake PARAM.SFO data')
    z.writestr('eboot.bin', b'fake eboot.bin data')
    z.writestr('data.bin', b'fake comppack data')
print("Created test_pack.zip")
EOF

mkdir -p tmp
./pkgj_cli extractzip test_pack.zip
find tmp/ -type f
```

Expected output:
```
tmp/eboot.bin
tmp/sce_sys/param.sfo
tmp/data.bin
```

> **Note:** `extractzip` requires that **directory entries** (names ending in `/`) appear in the zip before the files inside them. When creating zips programmatically, use `z.mkdir()` or add an explicit `ZipInfo` entry for each directory.

---

### Test 3 — File download simulation

```bash
# Simulate downloading a local file (FileHttp reads local paths as if they were URLs)
./pkgj_cli filedownload PSV_GAMES.tsv
# Output written to ./tmp/
```

---

## 5. How to generate the Vita binary

### Additional prerequisites

- [VitaSDK](https://vitasdk.org/) installed — see install steps below
- VitaSDK in PATH: `export VITASDK=~/vitasdk && export PATH=$VITASDK/bin:$PATH`

> VitaSDK's prebuilt packages are x86_64-only. On Apple Silicon / arm64 Linux you'll need x86_64 emulation (Rosetta on macOS, QEMU on Linux) to run the toolchain — the GitHub Actions workflow (`.github/workflows/build.yml`) builds natively on x86_64 and is the easiest way to get a `.vpk` without local emulation.

### Install VitaSDK (if not present)

Without this the Vita build will fail immediately with `arm-vita-eabi-gcc: command not found`.

`bootstrap-vitasdk.sh` installs to the path in `$VITASDK`. If that variable is unset, it defaults to `/usr/local/vitasdk` (requires `sudo`). To install without root, point it somewhere in your home directory first:

```bash
# Option A — system-wide (requires sudo, installs to /usr/local/vitasdk)
git clone https://github.com/vitasdk/vdpm /tmp/vdpm
cd /tmp/vdpm
sudo ./bootstrap-vitasdk.sh

# Option B — user-local (no sudo, installs to ~/vitasdk)
export VITASDK=~/vitasdk
git clone https://github.com/vitasdk/vdpm /tmp/vdpm
cd /tmp/vdpm
./bootstrap-vitasdk.sh
```

After installation, add this to `~/.bashrc` (adjust path if you used Option B):

```bash
export VITASDK=/usr/local/vitasdk   # or ~/vitasdk for Option B
export PATH=$VITASDK/bin:$PATH
```

Verify:

```bash
arm-vita-eabi-gcc --version
```

### Build the VPK

> ⚠️ **This will fail without VitaSDK installed.** Do not skip the prerequisite steps above.

```bash
cd /path/to/pkgj/ci

export CC=gcc-12 CXX=g++-12

# Run setup once if you haven't already (exports conan packages, copies vita profile)
./setup_conan.sh

mkdir build && cd build

poetry run conan install ../.. \
  --build missing \
  -s build_type=RelWithDebInfo \
  --profile:host vita \
  --output-folder .

poetry run conan build ../.. \
  -s build_type=RelWithDebInfo \
  --profile:host vita \
  --output-folder .

# Optional: copy the unsigned ELF (includes debug symbols)
cp pkgj pkgj.elf
```

Output files in `ci/build/`:
| File | Description |
|------|-------------|
| `eboot.bin` | Signed SELF — the actual executable loaded by the Vita |
| `UsagiPKGJ.vpk` | Full installable package (includes `eboot.bin` + Live Area assets) |
| `pkgj.elf` | Unsigned ELF — useful for debugging with `gdb` or disassembly |

### Install on Vita via FTP

```bash
# With VitaShell FTP running on the Vita at $PSVITAIP:1337
curl -T ci/build/eboot.bin ftp://$PSVITAIP:1337/ux0:/app/USAG00001/
```

Or use the provided CMake target (builds and sends in one step):
```bash
cd ci/build
PSVITAIP=192.168.1.x cmake --build . --target send
```

### GitHub Actions

`.github/workflows/build.yml` runs on every push: builds `pkgj_cli` and `UsagiPKGJ.vpk` on a native x86_64 Ubuntu runner and uploads both as workflow artifacts (Actions tab → the run → **Artifacts**). `.github/workflows/release.yml` publishes `UsagiPKGJ.vpk` as a GitHub release when a version tag is pushed. Actions is disabled by default on forks — enable it once under the repo's Actions tab before pushing.

---

## 6. File change summary

Non-exhaustive list of what differs from [blastrock/pkgj](https://github.com/blastrock/pkgj):

| Area | Files | Description |
|------|-------|-------------|
| Grid view | `src/gridview.{hpp,cpp}` (new) | Cover-art grid rendering, input, and per-visible-cell texture cache (`GridImageCache`) for `ModeGames`/`ModePspGames`/`ModePsxGames`, default-on |
| Cover fetching | `src/imagefetcher.{hpp,cpp}` | Reworked to try an ordered list of sources per title (HexFlow PNG → PS Store JPEG fallback) instead of a single URL, mode-aware HexFlow folder (PSVita/PSP/PS1); PNG decode goes through `vita2d_load_PNG_buffer` |
| Cover/flag assets | `assets/covers/*.png`, `assets/flags/*.png` | Grid/GameView placeholder art per content type (Vita/PSP vertical, PSX square) and region flag badges, embedded like other bundled UI assets |
| Config | `src/config.{hpp,cpp}` | Added `grid_view` |
| Menu | `src/menu.{hpp,cpp}` | Added "Grid view (games)" toggle |
| Main loop | `src/pkgi.{hpp,cpp}` | Dispatches to the grid or list renderer based on `config.grid_view`; alphabetical name-group-jump helpers and the OK/cancel button-label helpers moved out of the file's anonymous namespace so other translation units can call them |
| GameView | `src/gameview.{hpp,cpp}` | Removed the PS Store description panel and the View→Panel→SubItem focus hierarchy (the left cover column has nothing interactive left to "enter"); opening the view now focuses "Install Game" directly |
| Removed | `src/annotationdb.{hpp,cpp}`, `src/screenshotfetcher.{hpp,cpp}`, `src/descriptionfetcher.{hpp,cpp}` | Personal flags/comments, GameView screenshot strip, and PS Store description — all dropped |
