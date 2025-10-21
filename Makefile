# tAPI Makefile

# Default build directory
BUILD_DIR = build

# Default installation directory
INSTALL_DIR = /usr/local

# Default build type
BUILD_TYPE = Debug

# Default port
PORT = 8090

# Default AI server URL
AI_SERVER_URL = http://192.168.1.227:8000

# Default shared memory setting
USE_SHARED_MEMORY = true

.PHONY: all clean build install test run help

all: build

help:
	@echo "tAPI Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  build          - Build the project (default)"
	@echo "  clean          - Clean build artifacts"
	@echo "  install        - Install to $(INSTALL_DIR)"
	@echo "  test           - Run tests"
	@echo "  run            - Run the tAPI server"
	@echo "  test-config    - Test the GlobalConfig functionality"
	@echo ""
	@echo "Variables:"
	@echo "  BUILD_DIR      - Build directory (default: $(BUILD_DIR))"
	@echo "  INSTALL_DIR    - Installation directory (default: $(INSTALL_DIR))"
	@echo "  BUILD_TYPE     - Build type: Debug or Release (default: $(BUILD_TYPE))"
	@echo "  PORT           - Port number for tAPI server (default: $(PORT))"
	@echo "  AI_SERVER_URL  - URL for the AI server (default: $(AI_SERVER_URL))"
	@echo "  USE_SHARED_MEMORY - Use shared memory for communication (default: $(USE_SHARED_MEMORY))"

clean:
	rm -rf $(BUILD_DIR)

build:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cd $(BUILD_DIR) && make -j4

install: build
	cd $(BUILD_DIR) && make install

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure

run: build
	cd $(BUILD_DIR) && ./tAPI --port $(PORT) --ai-server-url $(AI_SERVER_URL) --use-shared-memory $(USE_SHARED_MEMORY)

test-config: build
	cd $(BUILD_DIR) && ./tAPI --port $(PORT) --ai-server-url $(AI_SERVER_URL) --use-shared-memory $(USE_SHARED_MEMORY) &
	@echo "Starting tAPI server on port $(PORT)..."
	@sleep 2
	./scripts/test_config.sh
	@echo "Stopping tAPI server..."
	@pkill -f './tAPI --port $(PORT)' || true 