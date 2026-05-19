#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
LIBTORCH_ROOT="${LIBTORCH_ROOT:-/opt/libtorch}"
GTEST_FILTER="${GTEST_FILTER:-PedagogicalWalkthroughTest.*}"

cmake -S . -B "$BUILD_DIR" -DENABLE_TORCH=ON -DLIBTORCH_ROOT="$LIBTORCH_ROOT"
cmake --build "$BUILD_DIR" --config Release --target unit_tests
"./$BUILD_DIR/tests/unit/unit_tests" --gtest_filter="$GTEST_FILTER"
