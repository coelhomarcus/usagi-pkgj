#!/usr/bin/env bash
# build.sh — convenience build script for Usagi PKGj
#
# Usage:
#   ./build.sh [target] [--clean] [--fake-version VERSION]
#
# Targets:
#   host          Build host tools        (output: ci/buildhost/pkgj_cli, pkgj_sim)
#   vita          Build PS Vita release   (output: ci/build/UsagiPKGJ.vpk)       [default]
#   vita-release  Same as vita
#   vita-test     Build PS Vita test      (output: ci/buildtest/UsagiPKGJ-Test.vpk)
#                   Title ID : USAG00099
#                   App name : "Usagi PKGj TEST"
#                   (Safe to install alongside the release build on the same Vita)
#
# Options:
#   --clean              Remove the build directory before building (full rebuild)
#   --fake-version VER   Override PKGI_VERSION with VER so the running app thinks it
#                        is an older version and triggers the auto-update check.
#                        Example: ./build.sh vita --fake-version 0.71
#                        (install that VPK on the Vita, open Usagi PKGj → it should offer
#                         to download the real latest release from GitHub)

set -euo pipefail

# ── Resolve project root ──
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TARGET="vita"
CLEAN=0
FAKE_VERSION=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        host|vita|vita-release|vita-test) TARGET="$1" ;;
        --clean) CLEAN=1 ;;
        --fake-version)
            shift
            FAKE_VERSION="${1:-}"
            if [[ -z "$FAKE_VERSION" ]]; then
                echo "--fake-version requires a version argument (e.g. 0.71)"
                exit 1
            fi
            ;;
        *)
            echo "Unknown argument: $1"
            echo "Usage: $0 [host|vita|vita-release|vita-test] [--clean] [--fake-version VERSION]"
            exit 1
            ;;
    esac
    shift
done

# Build the CMake -D flag for the fake version (empty string = use real version)
VERSION_OVERRIDE=""
if [[ -n "$FAKE_VERSION" ]]; then
    # Override both the runtime version (PKGI_VERSION) and the param.sfo version (LiveArea)
    VERSION_OVERRIDE="-DPKGI_DISPLAY_VERSION=${FAKE_VERSION} -DVITA_VERSION=${FAKE_VERSION}"
    echo "==> Fake version mode: PKGI_VERSION + param.sfo APP_VER will be \"${FAKE_VERSION}\" (for update testing)"
fi

[[ "$TARGET" == "vita-release" ]] && TARGET="vita"

# ── Compilers ──
export CC="${CC:-gcc-12}"
export CXX="${CXX:-g++-12}"

# ── VitaSDK ──
export VITASDK="${VITASDK:-/root/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

# ── Must run conan from inside ci/ so Poetry finds pyproject.toml ──
cd "$SCRIPT_DIR/ci"

echo "==> Preparing Conan profiles and local package recipes"
./setup_conan.sh

# ── Helper: clean build dir ──
maybe_clean() {
    local dir="$1"
    if [[ $CLEAN -eq 1 && -d "$dir" ]]; then
        echo "==> Removing $dir"
        rm -rf "$dir"
    fi
    mkdir -p "$dir"
}

# ── Build ──
case "$TARGET" in

    # ------------------------------------------------------------------
    host)
        echo "==> Building host simulator"
        maybe_clean buildhost
        cd buildhost

        poetry run conan install ../.. \
            -s build_type=RelWithDebInfo \
            -s compiler=gcc \
            -s compiler.version=12 \
            -s compiler.libcxx=libstdc++11 \
            --build missing \
            --output-folder .

        cmake ../.. \
            -G Ninja \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DHOST_BUILD=ON \
            -DBUILD_SIM=ON \
            -DCMAKE_TOOLCHAIN_FILE=./conan_toolchain.cmake

        ninja pkgj_cli pkgj_sim

        echo ""
        echo "==> Done.  Binaries: ci/buildhost/pkgj_cli  ci/buildhost/pkgj_sim"
        ;;

    # ------------------------------------------------------------------
    vita)
        echo "==> Building Vita release  (USAG00001 / Usagi PKGj)"
        maybe_clean build
        cd build

        poetry run conan install ../.. \
            -s build_type=RelWithDebInfo \
            --profile:host vita \
            --build missing \
            --output-folder .

        if [[ -n "$FAKE_VERSION" ]]; then
            # cmake directly so we can pass the version override at configure time
            export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
            export DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH:-}"
            [[ -f conanbuildenv-relwithdebinfo-armv7.sh ]] && \
                source conanbuildenv-relwithdebinfo-armv7.sh

            cmake ../.. \
                -G Ninja \
                -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake \
                -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                ${VERSION_OVERRIDE}

            cmake --build .
        else
            poetry run conan build ../.. \
                -s build_type=RelWithDebInfo \
                --profile:host vita \
                --output-folder .
        fi

        # Keep ELF with debug symbols alongside the stripped eboot
        [[ -f pkgj ]] && cp pkgj pkgj.elf

        echo ""
        echo "==> Done.  Package: ci/build/UsagiPKGJ.vpk"
        [[ -n "$FAKE_VERSION" ]] && echo "    Built with fake version \"${FAKE_VERSION}\" — auto-update test build."
        ;;

    # ------------------------------------------------------------------
    vita-test)
        echo "==> Building Vita TEST  (USAG00099 / Usagi PKGj TEST)"
        maybe_clean buildtest
        cd buildtest

        # Step 1: conan install — resolves deps, generates conan_toolchain.cmake
        poetry run conan install ../.. \
            -s build_type=RelWithDebInfo \
            --profile:host vita \
            --build missing \
            --output-folder .

        # Source the generated cross-compile env (sets CC/CXX/AR/STRIP/etc)
        # Pre-initialize variables that the generated file appends to, so that
        # set -u (nounset) does not abort when they are not already in the
        # environment (LD_LIBRARY_PATH / DYLD_LIBRARY_PATH are often absent).
        export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
        export DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH:-}"
        # shellcheck disable=SC1091
        [[ -f conanbuildenv-relwithdebinfo-armv7.sh ]] && \
            source conanbuildenv-relwithdebinfo-armv7.sh

        # Step 2: cmake configure — override Title ID, App Name, and VPK name for the test build
        #         VITA_TITLEID and VITA_APP_NAME are CACHE variables in CMakeLists.txt
        #         so -D flags here take precedence over the defaults.
        cmake ../.. \
            -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DVITA_TITLEID="USAG00099" \
            -DVITA_APP_NAME="Usagi PKGj TEST" \
            -DVITA_VPK_NAME="UsagiPKGJ-Test" \
            ${VERSION_OVERRIDE:-}

        # Step 3: cmake build
        cmake --build .

        [[ -f pkgj ]] && cp pkgj pkgj.elf

        echo ""
        echo "==> Done.  Package: ci/buildtest/UsagiPKGJ-Test.vpk"
        echo "    Title ID USAG00099 — safe to install alongside the release build."
        [[ -n "$FAKE_VERSION" ]] && echo "    Built with fake version \"${FAKE_VERSION}\" — auto-update test build."
        ;;
esac
