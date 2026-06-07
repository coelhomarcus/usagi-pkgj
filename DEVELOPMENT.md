# PKGj — Developer Guide (toaster-code/pkgj fork)

This document covers: what changed in this fork, how to build, how to test via the CLI simulator, and how to generate the final Vita binary (`.vpk`).

---

## Table of Contents

1. [What was changed and why](#1-what-was-changed-and-why)
2. [New feature: Personal Annotations & Flags](#2-new-feature-personal-annotations--flags)
3. [How to build (CLI / host simulator)](#3-how-to-build-cli--host-simulator)
4. [Testing with the CLI simulator](#4-testing-with-the-cli-simulator)
5. [How to generate the Vita binary](#5-how-to-generate-the-vita-binary)
6. [File change summary](#6-file-change-summary)

---

## 1. What was changed and why

This fork extends the original [blastrock/pkgj](https://github.com/blastrock/pkgj) with a **personal annotation system** that lets users attach a personal flag (e.g. "Broken", "Favorite", "Completed") and a free-text comment to any title in the list — directly from the GameView — without modifying any existing database.

The original application had no way to track personal notes about a game. The change adds this capability in a **non-breaking** way: a new SQLite file is created alongside the existing databases, and all existing data is untouched.

---

## 2. New feature: Personal Annotations & Flags

### What it does

- In the **title list**, any item with a flag set shows a short ASCII symbol prefix next to the name:

  | Symbol | Meaning       |
  |--------|---------------|
  | `[*]`  | Favorite      |
  | `[++]` | Very Good     |
  | `[+]`  | Good          |
  | `[-]`  | Bad           |
  | `[--]` | Very Bad      |
  | `[X]`  | Broken        |
  | `[/]`  | Completed     |
  | `[?]`  | Want to Play  |

- In the **GameView** (opened by pressing O/X on a game), a new **"Personal Notes"** section appears at the bottom of the window with:
  - **Flag picker** — one button per flag value; the active one is highlighted in green.
  - **Comment field** — multi-line free-text input.
  - **Save Notes** button — writes flag + comment to the database, immediately updates the list row.
  - **Clear Notes** button — removes the annotation entirely.

### Where data is stored

Annotations are stored in:
```
ux0:data/pkgj/annotations.db       (on Vita)
<configFolder>/annotations.db      (in simulator)
```

This is a standard SQLite3 file with one table:
```sql
CREATE TABLE annotations (
    titleid TEXT PRIMARY KEY NOT NULL,
    flag    INTEGER NOT NULL DEFAULT 0,
    comment TEXT    NOT NULL DEFAULT ''
);
```

Annotations survive database refreshes and app restarts. The file is created automatically on first launch.

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
| `pkgj.vpk` | Full installable package (includes `eboot.bin` + Live Area assets) |
| `pkgj.elf` | Unsigned ELF — useful for debugging with `gdb` or disassembly |

### Install on Vita via FTP

```bash
# With VitaShell FTP running on the Vita at $PSVITAIP:1337
curl -T ci/build/eboot.bin ftp://$PSVITAIP:1337/ux0:/app/PKGJ00000/
```

Or use the provided CMake target (builds and sends in one step):
```bash
cd ci/build
PSVITAIP=192.168.1.x cmake --build . --target send
```

---

## 6. File change summary

| File | Type | Description |
|------|------|-------------|
| `src/annotationdb.hpp` | **New** | `UserFlag` enum, `UserAnnotation` struct, `AnnotationDatabase` class declaration |
| `src/annotationdb.cpp` | **New** | SQLite CRUD: opens/creates `annotations.db`, implements `get()`, `set()`, `remove()` |
| `src/db.hpp` | **Modified** | Added `#include "annotationdb.hpp"`; added `user_flag` and `user_comment` fields to `DbItem` struct |
| `src/db.cpp` | **Modified** | Explicit initialization of new `DbItem` fields in `reload()` to silence compiler warnings |
| `src/gameview.hpp` | **Modified** | Added `AnnotationDatabase*` to constructor; added private members `_annotationDb`, `_annotation`, `_comment_buf[512]`, `_annotation_dirty` |
| `src/gameview.cpp` | **Modified** | Constructor loads saved annotation into working copy; new **Personal Notes** UI section in `render()` |
| `src/pkgi.cpp` | **Modified** | Added `annotation_db` global; `pkgi_apply_annotations()` called after every `configure_db()`; flag symbol prefix in list draw loop; `annotation_db` passed to `GameView` constructor; forward declaration of `pkgi_apply_annotations()` added for strict compiler compatibility |
| `src/workerpool.hpp` | **New** | Added single global worker slot abstraction; `try_submit(task_id, fn)` avoids duplicate/parallel fetch tasks |
| `src/imagefetcher.hpp` | **Modified** | Reworked to use `WorkerSlot::image_worker()`, state machine via `_submitted`, `_result`, and `ImageFetchResult` instead of per-instance thread + mutex/abort |
| `src/imagefetcher.cpp` | **Modified** | `ImageFetcher::_try_submit()` submits network+disk work to worker slot; `get_status()` and `get_texture()` process result asynchronously and safely on main thread |
| `cross.cmake` | **Modified** | Added `src/annotationdb.cpp` to the Vita executable source list |
| `host.cmake` | **Modified** | Added `src/annotationdb.cpp` to the `pkgj_cli` source list |
