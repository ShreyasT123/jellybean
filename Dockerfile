FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# --- build deps ---
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    curl \
    wget \
    unzip \
    tar \
    pkg-config \
    liburing-dev \
    python3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# --- LibTorch ---
ARG LIBTORCH_URL=https://download.pytorch.org/libtorch/cpu/libtorch-shared-with-deps-2.12.0%2Bcpu.zip
ARG LIBTORCH_DIR=/opt/libtorch

RUN wget -q "${LIBTORCH_URL}" -O /tmp/libtorch.zip \
    && unzip -q /tmp/libtorch.zip -d /opt \
    && rm /tmp/libtorch.zip \
    && ([ -d /opt/libtorch ] || mv /opt/libtorch-* /opt/libtorch)

WORKDIR /workspace
COPY . .

# --- IMPORTANT FIX ---
# Disable tests unless explicitly required in CI
ARG BUILD_TESTING=OFF

RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=20 \
    -DENABLE_TORCH=ON \
    -DLIBTORCH_ROOT=${LIBTORCH_DIR} \
    -DBUILD_TESTING=${BUILD_TESTING} \
    -DBUILD_BENCHMARKS=OFF \
    -DCMAKE_PREFIX_PATH=${LIBTORCH_DIR} \
    -DCMAKE_CXX_FLAGS="-O3 -march=x86-64 -Wall" \
    .

RUN cmake --build build --parallel $(nproc)


# ---------------------------
# Runtime stage
# ---------------------------
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libgomp1 \
    liburing2 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# --- LibTorch runtime libs only ---
COPY --from=builder /opt/libtorch/lib/*.so* /usr/local/lib/
RUN ldconfig

# --- Jellybean server binary ---
COPY --from=builder /workspace/build/src/jellybean_server /app/jellybean_server

# --- Config and model ---
COPY --from=builder /workspace/configs /app/configs
COPY --from=builder /workspace/model.pt /app/model.pt

WORKDIR /app

EXPOSE 9000 9001 9002

ENTRYPOINT ["/app/jellybean_server"]