# Usagi PKGj - Developer Guide

Usagi PKGj is a fork of [blastrock/pkgj](https://github.com/blastrock/pkgj).
This document tracks the current fork-specific behavior, build workflow, and
release process. For base PKGj architecture, TSV setup, and inherited options,
prefer the upstream repository.

Current release version: `0.72`.

## Table of Contents

1. [Fork scope](#1-fork-scope)
2. [Browse, grid, and cover pipeline](#2-browse-grid-and-cover-pipeline)
3. [Details and install flows](#3-details-and-install-flows)
4. [Local builds](#4-local-builds)
5. [Testing with host tools](#5-testing-with-host-tools)
6. [Vita install/debug cycle](#6-vita-installdebug-cycle)
7. [CI and releases](#7-ci-and-releases)
8. [File change summary](#8-file-change-summary)

## 1. Fork scope

The fork is currently centered on a cleaner browse experience, cover-art grid
navigation, and stability on Vita hardware/Vita3K.

Notable differences from upstream:

- Rebranded app: `Usagi PKGj`, title ID `USAG00001`, VPK name
  `UsagiPKGJ.vpk`.
- Isolated data/config folder: `ux0:usagi-pkgj` or `ur0:usagi-pkgj`.
- Home screen groups content into clearer PlayStation sections.
- Grid view is enabled by default for PS Vita, PlayStation Portable, and
  PlayStation 1 game lists.
- Covers are fetched on demand and cached locally; the removed one-time bulk
  cover sync is no longer part of the app.
- Region flag badges are rendered with bundled flag art.
- Footer hints render PlayStation button glyphs with button colors.
- Details pages expose direct actions in the footer instead of requiring focus
  movement to an install button.
- PSP direct downloads can be cancelled from the in-app progress view.
- PSP LiveArea PBP queueing is available when the `NoPspEmuDrm_kern` plugin is
  present.
- LiveArea queueing shows feedback before the synchronous BGDL call begins, so
  the Vita does not look frozen after pressing install.
- Stability work includes a persistent grid texture pool, held-navigation cover
  throttling, serialized curl transfers, and OpenSSL 1.0.x locking callbacks.

## 2. Browse, grid, and cover pipeline

### Supported grid modes

The grid renderer lives in `src/gridview.{hpp,cpp}` and is enabled only for:

- `ModeGames` - PS Vita games
- `ModePspGames` - PlayStation Portable games
- `ModePsxGames` - PlayStation 1 games

Other modes keep the classic text list. The menu option is still named
`Grid view (games)` and is controlled by `config.grid_view` (default: on).

### Cover sources and cache

`ImageFetcher` (`src/imagefetcher.{hpp,cpp}`) builds an ordered source list per
title:

1. HexFlow-Covers PNG art, using this fork's mirror:
   `https://raw.githubusercontent.com/coelhomarcus/HexFlow-Covers/main/Covers/...`
2. PlayStation Store JPEG cover as a fallback.

The HexFlow folder is mode-aware:

- `PSVita` for PS Vita games
- `PSP` for PlayStation Portable games
- `PS1` for PlayStation 1 games

Covers are cached in `thumbnail_folder`; when unset, the default is
`ux0:usagi-pkgj/cover`.

### Worker and curl limits

Cover fetching uses `WorkerPool::image_workers()`, currently limited to one
slot. In addition, `CurlHttp::start()` serializes every curl/OpenSSL transfer
process-wide.

That is deliberate. VitaSDK ships OpenSSL 1.0.x, and concurrent TLS work was a
major source of Vita3K crashes while cover downloads were being stressed. The
current design favors predictable stability over theoretical parallel cover
download speed.

### Texture lifetime

The grid does not let each visible cell own and free its own texture. Instead,
it uses `GridTexturePool` (`src/gridtexturepool.{hpp,cpp}`), a fixed pool of 24
persistent texture slots sized `256x320`.

`ImageFetcher::take_decoded_cover()` decodes into CPU-side RGBA pixels, and the
grid copies those pixels into a reusable texture-pool slot. Texture objects stay
alive for the process lifetime, which avoids Vita3K render-thread uploads
touching freed texture memory during fast scrolling.

### Scroll throttling

Held Up/Down navigation is throttled inside `src/gridview.cpp`:

- Movement is reduced to one step every 6 frames during a held direction.
- After 1 second of continuous held navigation, new cover fetch/decode work is
  paused until the direction is released.
- Covers already resident in `GridTexturePool` still draw immediately.

This keeps fast scanning responsive without overwhelming the cover pipeline.

### Placeholder and flag assets

Cover placeholders are platform-specific and live in `assets/covers/`:

- `vita_loading.png`, `vita_noimage.png`
- `psp_loading.png`, `psp_noimage.png`
- `ps1_loading.png`, `ps1_noimage.png`

Region flags live in `assets/flags/` and are loaded through
`src/regionflag.{hpp,cpp}`.

## 3. Details and install flows

`GameView` (`src/gameview.{hpp,cpp}`) is now action-oriented:

- Cross installs or cancels the active direct download.
- Circle closes the details page.
- Triangle queues PSP LiveArea PBP installs when `NoPspEmuDrm_kern` is present.
- Footer hints render colored PlayStation glyphs instead of plain `[X]` text.

PS Vita, DLC, themes, and PSM still use the BGDL/LiveArea path. The helper
`pkgi_queue_livearea_install()` in `src/pkgi.cpp` enqueues work, shows a
`Queueing ... in LiveArea...` message, waits two frames, then starts the
synchronous BGDL call. This lets the user see that the install action was
accepted before the system call blocks.

PSP and PlayStation 1 direct downloads use `Downloader`/`Download` and the
in-app progress bar. PSP direct downloads now expose cancellation.

## 4. Local builds

### Docker (recommended)

The `Dockerfile` at the repo root packages the whole toolchain — g++-12,
ninja, cmake, Python 3.11/Poetry, and the SDL2/curl dev headers `host`
needs — so there is nothing to install locally beyond Docker itself.

```bash
docker build -t usagi-pkgj-build .

# Vita release VPK -> ci/build/UsagiPKGJ.vpk
docker run --rm -v "$PWD":/work -v usagi-pkgj-conan:/root/.conan2 \
  usagi-pkgj-build bash ./build.sh vita

# Host CLI/simulator -> ci/buildhost/pkgj_cli, pkgj_sim
docker run --rm -v "$PWD":/work -v usagi-pkgj-conan:/root/.conan2 \
  usagi-pkgj-build bash ./build.sh host
```

Notes:

- The image is `linux/amd64` on purpose: the VitaSDK toolchain
  (`ci/conan-vitasdk`) is a prebuilt x86_64 Linux release, so it must run
  under emulation (Rosetta/QEMU) on Apple Silicon. Docker Desktop handles this
  automatically; expect the first build of each target to be noticeably
  slower than a native Linux x86_64 run.
- The named volume `usagi-pkgj-conan` caches `~/.conan2` (the downloaded
  VitaSDK toolchain plus every built dependency) across container runs, so
  only the first build per target pays the full Conan cost.
- The repo is bind-mounted at `/work`, so edits on the host are picked up
  immediately — no image rebuild needed unless `Dockerfile` itself changes.
- `--clean` and `--fake-version` work the same as in the native flow, e.g.
  `usagi-pkgj-build bash ./build.sh vita --clean`.

Targets:

| Target | Output | Notes |
| --- | --- | --- |
| `host` | `ci/buildhost/pkgj_cli`, `ci/buildhost/pkgj_sim` | CLI and SDL simulator |
| `vita` | `ci/build/UsagiPKGJ.vpk` | Release title ID `USAG00001` |
| `vita-test` | `ci/buildtest/UsagiPKGJ-Test.vpk` | Side-by-side test title ID `USAG00099` |

### Native build (Linux x86_64 only, without Docker)

Only viable directly on a compatible x86_64 Linux machine — on macOS/Apple
Silicon, use the Docker flow above or GitHub Actions instead. The convenience
script is the shortest path:

```bash
./build.sh host
./build.sh vita
./build.sh vita-test
```

Useful options:

```bash
./build.sh vita --clean
./build.sh vita --fake-version 0.71
```

`--fake-version` overrides both the runtime version string and Vita
`APP_VER`, which is useful for update-flow testing.

#### Manual host build

```bash
cd ci
./setup_conan.sh
mkdir -p buildhost
cd buildhost

poetry run conan install ../.. \
  --build missing \
  -s build_type=RelWithDebInfo \
  -s compiler=gcc \
  -s compiler.version=12 \
  -s compiler.libcxx=libstdc++11 \
  --output-folder .

cmake ../.. \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DHOST_BUILD=ON \
  -DBUILD_SIM=ON \
  -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake

ninja pkgj_cli pkgj_sim
```

#### Manual Vita build

```bash
cd ci
./setup_conan.sh
mkdir -p build
cd build

poetry run conan install ../.. \
  --build missing \
  -s build_type=RelWithDebInfo \
  --profile:host vita \
  --output-folder .

poetry run conan build ../.. \
  -s build_type=RelWithDebInfo \
  --profile:host vita \
  --output-folder .

cp pkgj pkgj.elf
```

Vita output files are written to `ci/build/`:

| File | Description |
| --- | --- |
| `UsagiPKGJ.vpk` | Full installable package |
| `eboot.bin` | Signed SELF loaded by the Vita |
| `pkgj.elf` | Unsigned ELF with debug symbols |

## 5. Testing with host tools

`pkgj_cli` uses `src/simulator.cpp` for POSIX replacements of Vita syscalls.
It is useful for parsing databases and testing extraction/download helpers.

Available subcommands:

```text
pkgj_cli refreshlist     <MODE> <tsv_file>
pkgj_cli filedownload    <local_file_or_url>
pkgj_cli extractzip      <zip_file>
pkgj_cli refreshcomppack <local_file>
pkgj_cli extract         <pkg_file> <zrif> <sha256>
pkgj_cli patchinfo       <xml_file> <titleid>
```

Example TSV parse:

```bash
cd ci/buildhost
curl -L "https://raw.githubusercontent.com/txy7795679/PSVITA-PKGJ-DATADB/refs/heads/master/PSV_GAMES.tsv" \
  -o PSV_GAMES.tsv
./pkgj_cli refreshlist PSVGAMES PSV_GAMES.tsv
```

`pkgj_sim` is the SDL graphical simulator. It is useful for UI layout work, but
BGDL, installation, update checks, and Vita-only kernel/plugin behavior are
stubbed.

## 6. Vita install/debug cycle

Install the VPK with VitaShell, or send just the executable to an already
installed app during development:

```bash
curl -T ci/build/eboot.bin ftp://$PSVITAIP:1337/ux0:/app/USAG00001/
```

The CMake `send` target can also send the executable:

```bash
cd ci/build
PSVITAIP=192.168.1.x cmake --build . --target send
```

Runtime folders worth checking while debugging:

| Path | Purpose |
| --- | --- |
| `ux0:usagi-pkgj/config.txt` | User config |
| `ux0:usagi-pkgj/cover` | Cover cache |
| `ux0:usagi-pkgj/<contentid>` | Direct download/extract work folders |
| `ux0:bgdl/t/` | LiveArea/BGDL queued downloads |

## 7. CI and releases

`.github/workflows/build.yml` is the regular push build. It builds:

- `pkgj_cli`
- `UsagiPKGJ.vpk`

`.github/workflows/release.yml` is the canonical release workflow. Push a tag
like `0.72` and it will:

- build `UsagiPKGJ.vpk`
- build `pkgj_cli`
- build `pkgj_sim`
- publish a GitHub Release with versioned asset names

Tags containing `alpha`, `beta`, or `rc` are marked as pre-releases.

Version values live in `CMakeLists.txt`:

- `VITA_VERSION`
- `PKGI_DISPLAY_VERSION`

Update both before tagging a release.

## 8. File change summary

Non-exhaustive fork-specific areas:

| Area | Files | Description |
| --- | --- | --- |
| Branding/version | `CMakeLists.txt`, `assets/sce_sys/**`, `src/update.cpp`, `src/vita.cpp` | Usagi name/title ID/VPK/data-folder/release API |
| Browse home | `src/browserview.{hpp,cpp}` | Grouped Home sections and labels |
| Grid view | `src/gridview.{hpp,cpp}` | Cover grid for PS Vita/PSP/PS1, input, footer hints, throttling |
| Texture pool | `src/gridtexturepool.{hpp,cpp}` | Persistent cover texture slots for Vita3K stability |
| Cover fetching | `src/imagefetcher.{hpp,cpp}`, `src/workerpool.hpp`, `src/curlhttp.cpp` | Mode-aware cover sources, cache, worker/curl limits |
| Cover/flag assets | `assets/covers/*.png`, `assets/flags/*.png` | Placeholder art and region badges |
| Details page | `src/gameview.{hpp,cpp}` | Direct action footer, PSP LiveArea PBP action, cover panel |
| Download flow | `src/pkgi.cpp`, `src/downloader.cpp`, `src/download.cpp` | LiveArea queue feedback and cancellable direct PSP downloads |
| Removed fork extras | `src/annotationdb.*`, `src/screenshotfetcher.*`, `src/descriptionfetcher.*` | Personal annotations, screenshots, and PS Store description panel were dropped |
