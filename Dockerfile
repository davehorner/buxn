# Dockerfile for buxn
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Base tools + deps (incl. clang/llvm 18 directly from Ubuntu repos)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential git curl ca-certificates pkg-config \
    parallel libgl-dev libegl1-mesa-dev libgles2-mesa-dev xorg-dev libasound2-dev \
    clang-18 llvm-18 lld-18 \
    && rm -rf /var/lib/apt/lists/*

# Make clang-18 the default
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100 && \
    update-alternatives --install /usr/bin/lld lld /usr/bin/lld-18 100

# Install mold 2.37.1
RUN curl -fsSL https://github.com/rui314/mold/releases/download/v2.37.1/mold-2.37.1-x86_64-linux.tar.gz \
  | tar -xz -C /opt \
 && ln -s /opt/mold-2.37.1-x86_64-linux/bin/mold /usr/local/bin/mold \
 && ln -s /usr/local/bin/mold /usr/local/bin/ld.mold

# Favor clang + mold
ENV CC=clang \
    CXX=clang++ \
    LDFLAGS="-fuse-ld=mold"

WORKDIR /app

# Copy code and initialize submodules (if present)
COPY . .
RUN if [ -d .git ]; then git submodule update --init --recursive; fi

# Build env like your CI
ENV BUILD_TYPE=Release \
    PLATFORM=linux

# Build and test
RUN ./bootstrap
RUN chmod +x ./build ./test ./bootstrap || true
RUN ./build $BUILD_TYPE $PLATFORM
#RUN ./test
#CMD ["./test"]
