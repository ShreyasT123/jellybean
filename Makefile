BUILD_DIR ?= build
CONFIG ?= Release
SERVER_CONFIG ?= server.config

.PHONY: configure build server client

configure:
	cmake -S . -B $(BUILD_DIR) -DENABLE_TORCH=ON -DLIBTORCH_ROOT=/opt/libtorch

build:
	cmake --build $(BUILD_DIR) --config $(CONFIG) --target jellybean_infer_server_demo

server: build
	./$(BUILD_DIR)/src/jellybean_infer_server_demo $(SERVER_CONFIG)

client:
	python3 tools/infer_client.py --host 127.0.0.1 --port 9000 --shape 1,128,512 --requests 40
