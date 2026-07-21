# Vita/host build environment for Usagi PKGj.
#
# Usage:
#   docker build -t usagi-pkgj-build .
#   docker run --rm -v "$PWD":/work -v usagi-pkgj-conan:/root/.conan2 \
#     usagi-pkgj-build bash ./build.sh vita   # or: host

ARG PLATFORM=linux/amd64
FROM --platform=${PLATFORM} ubuntu:24.04

# VitaSDK toolchain is a prebuilt x86_64 binary, so this image must stay amd64
# (runs under Rosetta/QEMU on Apple Silicon).
ENV DEBIAN_FRONTEND=noninteractive \
    CC=gcc-12 \
    CXX=g++-12 \
    CI=true \
    PYTHON_KEYRING_BACKEND=keyring.backends.null.Keyring

# python3.11 (not Ubuntu's default 3.12) because ci/poetry.lock pins
# pyyaml==6.0, which has no cp312 wheel. libcurl/SDL2 are only needed by
# `build.sh host` (BUILD_SIM).
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      gcc-12 \
      g++-12 \
      ninja-build \
      cmake \
      pkg-config \
      software-properties-common \
      curl \
      ca-certificates \
      git \
      libcurl4-openssl-dev \
      libsdl2-dev \
      libsdl2-ttf-dev \
      libsdl2-image-dev \
    && add-apt-repository -y ppa:deadsnakes/ppa \
    && apt-get update \
    && apt-get install -y --no-install-recommends python3.11 python3.11-venv \
    && rm -rf /var/lib/apt/lists/*

# --ignore-installed: avoid pip trying to uninstall Debian-owned "packaging".
RUN curl -sS https://bootstrap.pypa.io/get-pip.py -o /tmp/get-pip.py \
    && python3.11 /tmp/get-pip.py --ignore-installed \
    && rm /tmp/get-pip.py \
    && python3.11 -m pip install --no-cache-dir --ignore-installed 'poetry<2.0'

WORKDIR /work

CMD ["bash", "./build.sh", "vita"]
