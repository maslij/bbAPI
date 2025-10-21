# Docker Build Instructions

This document explains how to build and deploy the tAPI Docker image, which uses an optimized two-step build process with multi-stage builds for cross-compilation support.

## Quick Start

### Local Development
```bash
# Build the image locally
docker build -t tapi .

# Run the container
docker run --rm -it --gpus all -p 8080:8080 tapi
```

### Deploy to ECR (ARM64)
```bash
# Build and deploy to AWS ECR for NVIDIA Jetson
./deploy_to_ecr.sh
```

## Docker Build Optimization

The Dockerfile uses a multi-stage build optimized for Docker layer caching and cross-compilation:

### Multi-Stage Build Structure

**Stage 1: protoc-builder (x86_64)**
- Builds protoc and grpc_cpp_plugin for the build machine architecture
- Runs natively during the build process
- Cached independently of the main build

**Stage 2: Main build (ARM64)**
- Uses ARM64 NVIDIA L4T base image
- Copies native protoc tools from stage 1
- Builds ARM64 dependencies and application
- Links against ARM64 libraries

### Layer Structure (Stage 2)
1. **System dependencies** - Rarely changes, good for caching
2. **CMake installation** - Rarely changes, good for caching  
3. **Native protoc tools** - Copied from stage 1, cached unless stage 1 changes
4. **Dependency installation script** - Only changes when script is modified
5. **Third-party dependencies build** - Cached unless script changes (saves 10+ minutes)
6. **Project source code** - Changes frequently, but builds quickly
7. **Main project build** - Fast since dependencies are pre-built

### Caching Benefits

**First build:**
- Stage 1: Builds native protoc tools (~5-10 minutes)
- Stage 2: Downloads and compiles ARM64 dependencies (~15-20 minutes)
- Stage 2: Builds main project (~2-3 minutes)
- Total: ~25+ minutes

**Subsequent builds (with only source code changes):**
- Stage 1: Cached (instant)
- Stage 2: Reuses cached dependencies (instant)
- Stage 2: Only rebuilds main project (~2-3 minutes)
- Total: ~3 minutes

**When dependency script changes:**
- Stage 1: Cached (instant)
- Stage 2: Rebuilds ARM64 dependencies (~15-20 minutes)
- Stage 2: Rebuilds main project (~2-3 minutes)
- Total: ~20+ minutes

## Cross-Compilation Details

### The Problem
When building for ARM64 (NVIDIA Jetson), the protoc compiler and gRPC plugin need to:
- **Run** on the build machine (typically x86_64)
- **Generate code** for the target architecture (ARM64)

### The Solution
**Multi-stage build approach:**

```dockerfile
# Stage 1: Build native tools
FROM ubuntu:20.04 AS protoc-builder
RUN cmake --build . --target protobuf grpc

# Stage 2: Use native tools, build ARM64 libraries
FROM nvcr.io/nvidia/l4t-ml:r35.1.0-py3
COPY --from=protoc-builder /protoc-native/protobuf/bin/protoc /usr/local/bin/protoc-native
COPY --from=protoc-builder /protoc-native/grpc/bin/grpc_cpp_plugin /usr/local/bin/grpc_cpp_plugin-native
```

This ensures:
- ✅ Protoc runs natively on build machine
- ✅ Generated code is architecture-neutral
- ✅ Libraries are built for ARM64
- ✅ Final executable is ARM64

## Build Process Details

### Stage 1: Build Native Tools
```dockerfile
# Build protoc and grpc_cpp_plugin for build machine (x86_64)
RUN cmake --build . --target protobuf grpc -- -j$(nproc)
```

### Stage 2: Install ARM64 Dependencies
```dockerfile
# Install ARM64 dependencies (cached)
RUN echo "y" | scripts/install_deps.sh
```

### Stage 2: Build Main Project
```dockerfile
# Use native protoc, link ARM64 libraries
RUN scripts/build.sh
```

## File Structure in Container

```
/app/
├── /usr/local/bin/
│   ├── protoc-native          # x86_64 protoc (build tool)
│   └── grpc_cpp_plugin-native # x86_64 gRPC plugin (build tool)
├── scripts/
│   ├── install_deps.sh        # Dependency installation
│   └── build.sh               # Main build script
├── third-party/               # ARM64 libraries (cached)
│   ├── protobuf/lib/          # ARM64 protobuf libraries
│   ├── grpc/lib/              # ARM64 gRPC libraries
│   ├── c-ares/lib/            # ARM64 c-ares libraries
│   └── absl/lib/              # ARM64 Abseil libraries
├── build/                     # Build artifacts
│   └── tAPI                   # ARM64 executable
└── ...
```

## ECR Deployment

The `deploy_to_ecr.sh` script builds for ARM64 (NVIDIA Jetson) and pushes to AWS ECR:

```bash
./deploy_to_ecr.sh
```

This will:
1. Use Docker Buildx for cross-compilation
2. Build native protoc tools (Stage 1)
3. Build ARM64 application (Stage 2)
4. Push with timestamp and latest tags
5. Provide pull instructions for Jetson devices

### ECR Configuration
- **Region**: ap-southeast-2
- **Registry**: 246261010633.dkr.ecr.ap-southeast-2.amazonaws.com
- **Repository**: bbapi-jetson
- **Platforms**: linux/arm64 (NVIDIA Jetson)

## Development Workflow

### For Source Code Changes
```bash
# Just rebuild the image (will use cached dependencies and native tools)
docker build -t tapi .
```

### For Dependency Changes
```bash
# Force rebuild of dependencies (native tools still cached)
docker build --no-cache --target "" -t tapi .
```

### For Complete Rebuild
```bash
# Force rebuild everything including native tools
docker build --no-cache -t tapi .
```

## Troubleshooting

### Cross-Compilation Issues
1. **"Invalid ELF image"**: Native tools are being used correctly
2. **Build machine architecture**: Stage 1 builds for build machine (x86_64)
3. **Target architecture**: Stage 2 builds for target (ARM64)

### Slow Builds
If builds are taking too long, check:
1. Are you using `--no-cache`? (only use when necessary)
2. Did the `install_deps.sh` script change? (invalidates Stage 2 cache)
3. Are you building from scratch? (first build is always slow)

### Build Failures
1. **Stage 1 failures**: Check native tool build logs
2. **Stage 2 failures**: Check ARM64 dependency build logs
3. **Protoc failures**: Verify native tools are being used correctly

### Cache Not Working
1. **Stage 1**: Ensure multi-stage cache is enabled
2. **Stage 2**: Ensure `install_deps.sh` hasn't changed
3. **Docker storage**: Check that Docker has enough space for layers

## Performance Tips

1. **Multi-stage caching**: Both stages cache independently
2. **Native tools cache**: Stage 1 rarely needs rebuilding
3. **Dependency cache**: Stage 2 dependencies cache well
4. **Build context**: Use `.dockerignore` to reduce context size
5. **Layer ordering**: Most stable layers first, most changing layers last 