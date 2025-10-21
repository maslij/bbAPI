#!/bin/bash
set -e

# Configuration
TRITON_TAG="r22.10"  # Default tag, can be overridden with --tag option
BUILD_TYPE=${BUILD_TYPE:-Release}

# Get script directory and project root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_DIR="$( dirname "$SCRIPT_DIR" )"

# Set installation paths
THIRD_PARTY_DIR="$PROJECT_DIR/third-party"
THIRD_PARTY_BUILD_DIR="$PROJECT_DIR/third-party-build"
THIRD_PARTY_SRC_DIR="$PROJECT_DIR/third-party-src"

echo "Installing third-party dependencies..."
echo "Project directory: $PROJECT_DIR"
echo "Installation directory: $THIRD_PARTY_DIR"
echo "Build type: $BUILD_TYPE"

# Function to check if libraries are already built
check_existing_libs() {
    local protobuf_lib="$THIRD_PARTY_DIR/protobuf/lib/libprotobuf.a"
    local grpc_cpp_lib="$THIRD_PARTY_DIR/grpc/lib/libgrpc++.a"
    local grpc_lib="$THIRD_PARTY_DIR/grpc/lib/libgrpc.a"
    local triton_common_header="$THIRD_PARTY_DIR/triton-common/include/triton/common/triton_json.h"
    
    if [[ -f "$protobuf_lib" && -f "$grpc_cpp_lib" && -f "$grpc_lib" && -f "$triton_common_header" ]]; then
        echo "Third-party libraries already exist. Use --force to rebuild."
        return 0
    else
        return 1
    fi
}

# Parse arguments
FORCE_REBUILD=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --force)
            FORCE_REBUILD=true
            shift
            ;;
        --debug)
            BUILD_TYPE=Debug
            shift
            ;;
        --tag)
            if [[ -n "$2" && ! "$2" =~ ^-- ]]; then
                TRITON_TAG="$2"
                shift 2
            else
                echo "Error: --tag requires a value (e.g., --tag r22.10)"
                exit 1
            fi
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --force          Force rebuild even if libraries exist"
            echo "  --debug          Build in debug mode"
            echo "  --tag <version>  Set Triton tag version (default: r22.10)"
            echo "  --help           Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Check if we need to build
if ! $FORCE_REBUILD && check_existing_libs; then
    echo "Third-party dependencies are already installed."
    echo "Libraries found in: $THIRD_PARTY_DIR"
    echo "Protoc executable: $THIRD_PARTY_DIR/protobuf/bin/protoc"
    echo "gRPC C++ plugin: $THIRD_PARTY_DIR/grpc/bin/grpc_cpp_plugin"
    exit 0
fi

# Clean up previous builds if force rebuild
if $FORCE_REBUILD; then
    echo "Force rebuild requested, cleaning previous builds..."
    rm -rf "$THIRD_PARTY_DIR" "$THIRD_PARTY_BUILD_DIR" "$THIRD_PARTY_SRC_DIR"
fi

# Create directories
mkdir -p "$THIRD_PARTY_BUILD_DIR" "$THIRD_PARTY_SRC_DIR"

# Clone triton third-party repository
echo "Cloning Triton third-party repository..."
if [[ ! -d "$THIRD_PARTY_SRC_DIR/.git" ]]; then
    git clone --depth 1 --branch "$TRITON_TAG" \
        https://github.com/triton-inference-server/third_party.git \
        "$THIRD_PARTY_SRC_DIR"
else
    echo "Repository already exists, pulling latest changes..."
    cd "$THIRD_PARTY_SRC_DIR"
    git fetch --depth 1 origin "$TRITON_TAG"
    git checkout "$TRITON_TAG"
    cd "$PROJECT_DIR"
fi

# Configure third-party build
echo "Configuring third-party dependencies..."
cd "$THIRD_PARTY_BUILD_DIR"

# Detect if we're running in Docker and set appropriate CMAKE flags
CMAKE_EXTRA_FLAGS=""
IS_CROSS_COMPILING=false
if [ -f "/.dockerenv" ]; then
    echo "Detected Docker environment, configuring for ARM64 cross-compilation..."
    CMAKE_EXTRA_FLAGS="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY"
    IS_CROSS_COMPILING=true
fi

cmake -S "$THIRD_PARTY_SRC_DIR" -B . \
    -DTRITON_THIRD_PARTY_INSTALL_PREFIX="$THIRD_PARTY_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    $CMAKE_EXTRA_FLAGS

# Build the required targets
echo "Building third-party dependencies..."
echo "This may take several minutes..."

# Build in parallel using all available cores
NPROC=$(nproc)
echo "Using $NPROC parallel jobs"

cmake --build . --target protobuf grpc c-ares absl -- -j$NPROC

# Build Triton Common library
echo "Building Triton Common library..."
TRITON_COMMON_SRC_DIR="$THIRD_PARTY_SRC_DIR/triton-common"
TRITON_COMMON_BUILD_DIR="$THIRD_PARTY_BUILD_DIR/triton-common"

# Clone Triton Common repository
if [[ ! -d "$TRITON_COMMON_SRC_DIR/.git" ]]; then
    echo "Cloning Triton Common repository..."
    git clone --depth 1 --branch "$TRITON_TAG" \
        https://github.com/triton-inference-server/common.git \
        "$TRITON_COMMON_SRC_DIR"
else
    echo "Triton Common repository already exists, pulling latest changes..."
    cd "$TRITON_COMMON_SRC_DIR"
    git fetch --depth 1 origin "$TRITON_TAG"
    git checkout "$TRITON_TAG"
    cd "$PROJECT_DIR"
fi

# Build Triton Common
mkdir -p "$TRITON_COMMON_BUILD_DIR"
cd "$TRITON_COMMON_BUILD_DIR"

cmake -S "$TRITON_COMMON_SRC_DIR" -B . \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX="$THIRD_PARTY_DIR/triton-common" \
    $CMAKE_EXTRA_FLAGS

cmake --build . -- -j$NPROC
cmake --install .

echo "✓ Triton Common library built successfully"

# Return to third-party build directory for subsequent steps
cd "$THIRD_PARTY_BUILD_DIR"

# Handle protoc binaries for cross-compilation
echo "Setting up protoc binaries..."
PROTOC_PATH="$THIRD_PARTY_DIR/protobuf/bin/protoc"
GRPC_PLUGIN_PATH="$THIRD_PARTY_DIR/grpc/bin/grpc_cpp_plugin"

# Create bin directories
mkdir -p "$THIRD_PARTY_DIR/protobuf/bin"
mkdir -p "$THIRD_PARTY_DIR/grpc/bin"

if $IS_CROSS_COMPILING; then
    echo "Cross-compilation detected, downloading compatible x86_64 protoc binaries..."
    
    # Download protoc 3.19.4 (matches Triton r22.10) for x86_64 host
    PROTOC_VERSION="3.19.4"
    PROTOC_ZIP="protoc-${PROTOC_VERSION}-linux-x86_64.zip"
    PROTOC_URL="https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOC_VERSION}/${PROTOC_ZIP}"
    
    cd /tmp
    echo "Downloading protoc ${PROTOC_VERSION} for x86_64..."
    wget -q "$PROTOC_URL" || { echo "Failed to download protoc"; exit 1; }
    
    echo "Extracting protoc..."
    unzip -q "$PROTOC_ZIP" -d protoc-temp
    
    # Copy the x86_64 protoc binary to our third-party location
    cp protoc-temp/bin/protoc "$PROTOC_PATH"
    chmod +x "$PROTOC_PATH"
    
    # Clean up protoc download
    rm -rf /tmp/protoc-temp /tmp/"$PROTOC_ZIP"
    cd "$PROJECT_DIR"
    
    # For grpc_cpp_plugin, try built version first, then fall back to system
    echo "Setting up gRPC C++ plugin..."
    
    # Try to use the built grpc_cpp_plugin if it exists and works
    if [[ -f "$THIRD_PARTY_DIR/grpc/bin/grpc_cpp_plugin" ]] && "$THIRD_PARTY_DIR/grpc/bin/grpc_cpp_plugin" --help >/dev/null 2>&1; then
        echo "✓ Using built grpc_cpp_plugin (compatible with host)"
    else
        echo "Built grpc_cpp_plugin not available or incompatible with host, using system version..."
        
        # Use system plugin with compatibility warning
        ln -sf /usr/bin/grpc_cpp_plugin "$GRPC_PLUGIN_PATH"
        
        # Check system grpc_cpp_plugin version for better diagnostics
        SYSTEM_GRPC_VERSION=$(grpc_cpp_plugin --version 2>/dev/null | grep -oP '\d+\.\d+\.\d+' | head -n1 || echo "unknown")
        echo "System grpc_cpp_plugin version: $SYSTEM_GRPC_VERSION"
        
        if [[ "$SYSTEM_GRPC_VERSION" == "unknown" ]]; then
            echo "⚠️  Warning: Could not determine system gRPC plugin version"
        elif [[ "$SYSTEM_GRPC_VERSION" < "1.40.0" ]]; then
            echo "⚠️  Warning: System gRPC plugin version ($SYSTEM_GRPC_VERSION) may be incompatible"
            echo "    with protobuf $PROTOC_VERSION. If you encounter gRPC compilation errors,"
            echo "    consider updating the system gRPC packages or building gRPC tools manually."
        else
            echo "✓ System gRPC plugin version ($SYSTEM_GRPC_VERSION) should be compatible"
        fi
    fi
    
    echo "✓ Using downloaded protoc ${PROTOC_VERSION} for x86_64 host"
    echo "✓ Protoc path: $PROTOC_PATH"
    echo "✓ gRPC plugin path: $GRPC_PLUGIN_PATH"
else
    # In native builds, the compiled binaries should work fine
    if [[ -f "$PROTOC_PATH" && -f "$GRPC_PLUGIN_PATH" ]]; then
        echo "✓ Built protoc found: $PROTOC_PATH"
        echo "✓ Built grpc_cpp_plugin found: $GRPC_PLUGIN_PATH"
    else
        echo "❌ Built protoc binaries not found, falling back to system binaries..."
        ln -sf /usr/bin/protoc "$PROTOC_PATH"
        ln -sf /usr/bin/grpc_cpp_plugin "$GRPC_PLUGIN_PATH"
    fi
fi

# Verify installation
echo "Verifying installation..."

if [[ -f "$PROTOC_PATH" && -f "$GRPC_PLUGIN_PATH" ]]; then
    echo "✓ protoc found: $PROTOC_PATH"
    echo "✓ grpc_cpp_plugin found: $GRPC_PLUGIN_PATH"
    
    # Test protoc
    "$PROTOC_PATH" --version
    
    echo ""
    echo "Third-party dependencies installed successfully!"
    echo "Installation directory: $THIRD_PARTY_DIR"
    echo "Triton Common headers: $THIRD_PARTY_DIR/triton-common/include"
    echo ""
    echo "You can now run the main build with:"
    echo "  cd $PROJECT_DIR"
    echo "  ./scripts/build.sh"
else
    echo "❌ Installation verification failed!"
    echo "Expected files not found:"
    echo "  protoc: $PROTOC_PATH"
    echo "  grpc_cpp_plugin: $GRPC_PLUGIN_PATH"
    exit 1
fi

# Clean up build directory to save space (optional)
# In Docker builds, always clean up automatically
if [ -f "/.dockerenv" ]; then
    echo "Docker environment detected, cleaning up build directory automatically..."
    rm -rf "$THIRD_PARTY_BUILD_DIR"
    echo "Build directory cleaned."
else
    read -p "Clean up build directory to save space? [y/N]: " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Cleaning up build directory..."
        rm -rf "$THIRD_PARTY_BUILD_DIR"
        echo "Build directory cleaned."
    fi
fi

echo "Setup complete!" 