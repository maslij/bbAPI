# tAPI Build Instructions

This project uses a two-step build process to separate third-party dependency management from the main project build.

## Quick Start

1. **Install third-party dependencies** (only needed once):
   ```bash
   ./scripts/install_deps.sh
   ```

2. **Build the project**:
   ```bash
   ./scripts/build.sh
   ```

3. **Run the application**:
   ```bash
   ./build/tAPI
   ```

## Detailed Instructions

### Step 1: Install Third-Party Dependencies

The first step installs protobuf, gRPC, c-ares, and Abseil libraries:

```bash
./scripts/install_deps.sh [options]
```

**Options:**
- `--force`: Force rebuild even if libraries already exist
- `--debug`: Build dependencies in debug mode
- `--help`: Show help

**What it does:**
- Downloads the Triton third-party repository
- Builds protobuf, gRPC, c-ares, and Abseil from source
- Installs them to `./third-party/` directory
- Only needs to be run once (unless you want to update/rebuild dependencies)

**Example:**
```bash
# First time setup
./scripts/install_deps.sh

# Force rebuild in debug mode
./scripts/install_deps.sh --force --debug
```

### Step 2: Build the Main Project

Once dependencies are installed, build the main project:

```bash
./scripts/build.sh [options]
```

**Options:**
- `--debug`: Build in debug mode
- `--tests`: Build with tests enabled
- `--clean`: Clean build artifacts
- `--help`: Show help

**What it does:**
- Checks that third-party dependencies exist
- Generates protobuf code from `.proto` files
- Compiles the main tAPI application
- Links against the pre-built third-party libraries

**Examples:**
```bash
# Release build
./scripts/build.sh

# Debug build with tests
./scripts/build.sh --debug --tests

# Clean previous build
./scripts/build.sh --clean
```

## File Structure

After running the install script, your directory structure will look like:

```
tAPI/
├── scripts/
│   ├── install_deps.sh     # Install third-party dependencies
│   └── build.sh            # Build main project
├── third-party/            # Third-party libraries (created by install_deps.sh)
│   ├── protobuf/
│   ├── grpc/
│   ├── c-ares/
│   └── absl/
├── build/                  # Build artifacts (created by build.sh)
│   └── tAPI                # Final executable
└── ...
```

## Troubleshooting

### Dependencies not found
If you see an error like "Third-party dependencies not found", run:
```bash
./scripts/install_deps.sh
```

### Build fails
1. Make sure dependencies are installed first
2. Try cleaning and rebuilding:
   ```bash
   ./scripts/build.sh --clean
   ./scripts/build.sh
   ```

### Force rebuild everything
To completely rebuild everything from scratch:
```bash
./scripts/install_deps.sh --force
./scripts/build.sh --clean
./scripts/build.sh
```

## Benefits of This Approach

1. **Faster builds**: Third-party dependencies only built once
2. **Cleaner separation**: Dependency management separate from project build
3. **Better caching**: Dependencies can be cached across builds
4. **Easier debugging**: Can rebuild just the main project without dependencies
5. **More predictable**: Know exactly when dependencies are being built 