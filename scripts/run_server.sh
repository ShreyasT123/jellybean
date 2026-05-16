#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
SERVER_CONFIG="${SERVER_CONFIG:-server.config}"
LIBTORCH_ROOT="${LIBTORCH_ROOT:-/opt/libtorch}"

cmake -S . -B "$BUILD_DIR" -DENABLE_TORCH=ON -DLIBTORCH_ROOT="$LIBTORCH_ROOT"
cmake --build "$BUILD_DIR" --config Release --target jellybean_infer_server_demo
"./$BUILD_DIR/src/jellybean_infer_server_demo" "$SERVER_CONFIG"
