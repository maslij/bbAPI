#!/bin/bash
set -e

# Default build type
BUILD_TYPE=Release
BUILD_TESTS=OFF
CLEAN=OFF

# Parse arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --debug)
      BUILD_TYPE=Debug
      shift
      ;;
    --tests)
      BUILD_TESTS=ON
      shift
      ;;
    --clean)
      CLEAN=ON
      shift
      ;;
    --help)
      echo "Usage: $0 [options]"
      echo "Options:"
      echo "  --debug    Build in debug mode"
      echo "  --tests    Build tests"
      echo "  --clean    Clean build artifacts"
      echo "  --help     Show this help"
      echo ""
      echo "Prerequisites:"
      echo "  Before running this build script, you must install third-party dependencies:"
      echo "    ./scripts/install_deps.sh"
      echo ""
      echo "  This only needs to be done once (unless you want to force rebuild them)."
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

# Get project directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_DIR="$( dirname "$SCRIPT_DIR" )"

# Check if third-party dependencies exist
THIRD_PARTY_DIR="$PROJECT_DIR/third-party"
if [[ ! -f "$THIRD_PARTY_DIR/protobuf/lib/libprotobuf.a" ]]; then
  echo "❌ Third-party dependencies not found!"
  echo ""
  echo "Please install third-party dependencies first:"
  echo "  cd $PROJECT_DIR"
  echo "  ./scripts/install_deps.sh"
  echo ""
  echo "Then re-run this build script."
  exit 1
fi

echo "✓ Third-party dependencies found"

# Handle clean option
if [[ "$CLEAN" == "ON" ]]; then
  echo "Cleaning build directory..."
  rm -rf "$PROJECT_DIR/build"
  echo "Build directory cleaned."
  exit 0
fi

# Create build directory
mkdir -p "$PROJECT_DIR/build"
cd "$PROJECT_DIR/build"

echo "Building tAPI..."
echo "Build type: $BUILD_TYPE"
echo "Build tests: $BUILD_TESTS"

# Configure and build
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DBUILD_TESTS=$BUILD_TESTS ..
cmake --build . -- -j$(nproc)

echo ""
echo "✅ Build completed successfully!"
if [[ "$BUILD_TESTS" == "ON" ]]; then
  echo "Run tests with: cd build && ctest"
fi

echo "Run tAPI with: ./build/tAPI" 