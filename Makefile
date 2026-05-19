BUILD_DIR ?= build
CONFIG ?= Release
LIBTORCH_ROOT ?= /opt/libtorch
GTEST_FILTER ?= PedagogicalWalkthroughTest.*

.PHONY: configure build test demo-tests server

configure:
	cmake -S . -B $(BUILD_DIR) -DENABLE_TORCH=ON -DLIBTORCH_ROOT=$(LIBTORCH_ROOT)

build:
	cmake --build $(BUILD_DIR) --config $(CONFIG)

test:
	ctest --test-dir $(BUILD_DIR) --output-on-failure

server: build
	./$(BUILD_DIR)/src/jellybean_server

demo-tests: build
	./$(BUILD_DIR)/tests/unit/unit_tests --gtest_filter="$(GTEST_FILTER)"
