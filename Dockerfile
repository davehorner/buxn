# Dockerfile for buxn — uses GitHub mold tarball and auto-selects arch
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG TARGETARCH
ARG MOLD_VERSION=2.37.1

# Base tools + deps (clang/llvm 18 from Ubuntu)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential git curl ca-certificates pkg-config \
    parallel libgl-dev libegl1-mesa-dev libgles2-mesa-dev xorg-dev libasound2-dev \
    clang-18 llvm-18 lld-18 \
    && rm -rf /var/lib/apt/lists/*

# Make clang-18/lld the defaults
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100 && \
    update-alternatives --install /usr/bin/lld lld /usr/bin/lld-18 100

# Install mold from GitHub release tarball with arch detection
# (Fixes Rosetta/ELF loader mismatch on Apple Silicon)
RUN set -eux; \
    arch="${TARGETARCH:-$(dpkg --print-architecture)}"; \
    case "$arch" in \
      amd64|x86_64) mold_pkg_arch="x86_64-linux" ;; \
      arm64|aarch64) mold_pkg_arch="aarch64-linux" ;; \
      *) echo "Unsupported arch: ${arch}"; exit 1 ;; \
    esac; \
    url="https://github.com/rui314/mold/releases/download/v${MOLD_VERSION}/mold-${MOLD_VERSION}-${mold_pkg_arch}.tar.gz"; \
    curl -fsSL "$url" | tar -xz -C /opt; \
    ln -sf "/opt/mold-${MOLD_VERSION}-${mold_pkg_arch}/bin/mold" /usr/local/bin/mold; \
    ln -sf /usr/local/bin/mold /usr/local/bin/ld.mold; \
    mold --version

# Favor clang + mold; quiet GNU parallel & no TTY probing noise
ENV CC=clang \
    CXX=clang++ \
    LDFLAGS="-fuse-ld=mold" \
    PARALLEL="--plain --no-notice" \
    TERM=dumb

WORKDIR /app

# Copy source and init submodules if present
COPY . ./
RUN if [ -d .git ]; then git submodule update --init --recursive; fi

# Build env (override with --build-arg if desired)
ENV BUILD_TYPE=Release \
    PLATFORM=linux

# Build
RUN ./bootstrap
RUN chmod +x ./build ./test ./bootstrap || true
RUN ./build $BUILD_TYPE $PLATFORM
# RUN ./test
# CMD ["./test"]

