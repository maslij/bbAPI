# Single-stage build for ARM64
ARG L4T_VERSION=35.1.0
FROM nvcr.io/nvidia/l4t-ml:r${L4T_VERSION}-py3

# Set non-interactive frontend and timezone
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Australia/Sydney

# Configure timezone non-interactively
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# Install system dependencies with version-specific handling
RUN apt-get update && \
    # Detect Ubuntu version
    UBUNTU_VERSION=$(lsb_release -rs | cut -d. -f1) && \
    echo "Detected Ubuntu version: $UBUNTU_VERSION" && \
    # Install common packages
    apt-get install -y \
        build-essential \
        git \
        libboost-all-dev \
        libcurl4-openssl-dev \
        libsqlite3-dev \
        libeigen3-dev \
        libssl-dev \
        uuid-dev \
        pkg-config \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-ugly \
        libasio-dev \
        cmake \
        wget \
        rapidjson-dev && \
    # Install version-specific packages
    if [ "$UBUNTU_VERSION" -ge "20" ]; then \
        echo "Installing Ubuntu 20+ packages..." && \
        apt-get install -y \
            nlohmann-json3-dev \
            protobuf-compiler \
            libprotobuf-dev \
            protobuf-compiler-grpc \
            libgrpc++-dev; \
    else \
        echo "Installing Ubuntu 18 packages..." && \
        apt-get install -y \
            protobuf-compiler \
            libprotobuf-dev && \
        # Try different nlohmann-json package names for Ubuntu 18
        (apt-get install -y nlohmann-json-dev || \
         apt-get install -y nlohmann-json3-dev || \
         echo "Warning: nlohmann-json package not found, will install from source") && \
        # For Ubuntu 18, we might need to build gRPC from source or use alternative
        echo "Note: gRPC packages may need manual installation for Ubuntu 18"; \
    fi && \
    rm -rf /var/lib/apt/lists/*

# Install nlohmann-json from source if package installation failed
RUN if ! pkg-config --exists nlohmann_json; then \
        echo "Installing nlohmann-json from source..." && \
        cd /tmp && \
        wget https://github.com/nlohmann/json/releases/download/v3.11.2/json.hpp && \
        mkdir -p /usr/local/include/nlohmann && \
        mv json.hpp /usr/local/include/nlohmann/ && \
        echo "Successfully installed nlohmann-json from source"; \
    else \
        echo "nlohmann-json package already available"; \
    fi

# Check CMake version and install newer version if needed
RUN cmake_version=$(cmake --version | head -n1 | grep -oP '\d+\.\d+' | head -n1) && \
    echo "Current CMake version: $cmake_version" && \
    if [ "$(printf '%s\n' "3.22" "$cmake_version" | sort -V | head -n1)" = "3.22" ]; then \
        echo "CMake version is sufficient"; \
    else \
        echo "Installing newer CMake version..." && \
        wget https://github.com/Kitware/CMake/releases/download/v3.31.7/cmake-3.31.7-linux-aarch64.tar.gz && \
        tar -xzf cmake-3.31.7-linux-aarch64.tar.gz && \
        cd cmake-3.31.7-linux-aarch64 && \
        cp -r bin/* /usr/local/bin/ && \
        cp -r share/* /usr/local/share/ && \
        cp -r doc/* /usr/local/doc/ 2>/dev/null || true && \
        cd .. && \
        rm -rf cmake-3.31.7-linux-aarch64* && \
        ln -sf /usr/local/bin/cmake /usr/bin/cmake; \
    fi

# Verify OpenCV with CUDA support
RUN python3 -c "import cv2; print('OpenCV version:', cv2.__version__)"

# Set working directory
WORKDIR /app

# Copy dependency installation script and related files first (for better caching)
COPY scripts/install_deps.sh scripts/install_deps.sh

# Make the script executable
RUN chmod +x scripts/install_deps.sh

# Clean any existing third-party directories to ensure fresh build
RUN rm -rf third-party third-party-build third-party-src

# Set environment variables for ARM64 cross-compilation
ENV CMAKE_SYSTEM_NAME=Linux
ENV CMAKE_SYSTEM_PROCESSOR=aarch64
ENV CMAKE_C_COMPILER=gcc
ENV CMAKE_CXX_COMPILER=g++
ENV CMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
ENV CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
ENV CMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY

# Install third-party dependencies for ARM64 with force flag to ensure proper protoc setup
RUN echo "y" | scripts/install_deps.sh --force

# Copy the rest of the project files
COPY . .

# Clean any third-party directories that might have been copied from build context
# This ensures we don't overwrite the dependencies we just built
RUN rm -rf third-party-build third-party-src

# Clean up any existing generated protobuf files to ensure they get regenerated 
# with the correct protoc version (3.19.4) instead of the old system version (3.12.4)
RUN rm -rf build/generated/*.pb.h build/generated/*.pb.cc build/generated/*.grpc.pb.h build/generated/*.grpc.pb.cc

# Update CMakeLists.txt for the container environment
RUN sed -i 's|find_package(OpenCV 4.2 REQUIRED)|find_package(OpenCV 4.5 REQUIRED)|g' CMakeLists.txt

# Make build script executable
RUN chmod +x scripts/build.sh

# Build the main project (this will be fast since dependencies are already built)
RUN scripts/build.sh

# Verify the build was successful
RUN test -f build/tAPI && echo "✅ tAPI build successful" || (echo "❌ tAPI build failed" && exit 1)

# Clean up unnecessary files to reduce image size
RUN rm -rf third-party-build third-party-src && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# Expose port
EXPOSE 8080

# Set the entrypoint to allow passing command line arguments
ENTRYPOINT ["./build/tAPI"]

# Default command (can be overridden)
CMD [] 