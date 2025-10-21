#!/bin/bash
# BrinkByte Vision - tAPI System Dependencies Installation
# This script installs system packages required for PostgreSQL and Redis integration
# Run this script before building tAPI with the new billing integration

set -e

echo "================================================"
echo "tAPI System Dependencies Installation"
echo "================================================"
echo ""

# Detect architecture
ARCH=$(uname -m)
echo "Detected architecture: $ARCH"

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    echo "Detected OS: $OS"
else
    echo "Cannot detect OS. Assuming Ubuntu/Debian."
    OS="ubuntu"
fi

# Function to install on Ubuntu/Debian
install_ubuntu_debian() {
    echo "Installing dependencies for Ubuntu/Debian..."
    
    sudo apt-get update
    
    # PostgreSQL client library
    echo "Installing PostgreSQL client library (libpq-dev)..."
    sudo apt-get install -y libpq-dev postgresql-client
    
    # Redis client library
    echo "Installing Redis client library (libhiredis-dev)..."
    sudo apt-get install -y libhiredis-dev redis-tools
    
    # Additional development tools (if not already installed)
    sudo apt-get install -y \
        build-essential \
        cmake \
        pkg-config \
        git \
        curl \
        wget
    
    echo "Ubuntu/Debian dependencies installed successfully!"
}

# Function to install on Jetson (Ubuntu-based)
install_jetson() {
    echo "Installing dependencies for NVIDIA Jetson..."
    
    # Jetson uses Ubuntu, so same as Ubuntu/Debian
    install_ubuntu_debian
    
    echo "Jetson dependencies installed successfully!"
}

# Install based on OS/Architecture
case "$OS" in
    ubuntu|debian)
        install_ubuntu_debian
        ;;
    *)
        if [ "$ARCH" = "aarch64" ]; then
            echo "Assuming Jetson device..."
            install_jetson
        else
            echo "Unsupported OS: $OS"
            echo "Please manually install: libpq-dev, libhiredis-dev"
            exit 1
        fi
        ;;
esac

echo ""
echo "================================================"
echo "Verifying Installation"
echo "================================================"

# Verify PostgreSQL client
if pkg-config --exists libpq; then
    echo "✓ PostgreSQL client library (libpq) found"
    pkg-config --modversion libpq
else
    echo "✗ PostgreSQL client library (libpq) NOT found"
    exit 1
fi

# Verify Redis client
if pkg-config --exists hiredis; then
    echo "✓ Redis client library (hiredis) found"
    pkg-config --modversion hiredis
else
    echo "✗ Redis client library (hiredis) NOT found"
    exit 1
fi

echo ""
echo "================================================"
echo "Installation Complete!"
echo "================================================"
echo ""
echo "Next steps:"
echo "1. Start PostgreSQL and Redis services:"
echo "   docker-compose up -d postgres redis"
echo ""
echo "2. Run database migrations:"
echo "   psql -h localhost -U tapi_user -d tapi_edge -f scripts/init-postgres.sql"
echo "   psql -h localhost -U tapi_user -d tapi_edge -f scripts/migrations/001_initial_schema.sql"
echo "   psql -h localhost -U tapi_user -d tapi_edge -f scripts/migrations/002_billing_schema.sql"
echo ""
echo "3. Configure environment:"
echo "   cp env.template .env"
echo "   # Edit .env with your tenant_id and billing_api_key"
echo ""
echo "4. Build tAPI:"
echo "   mkdir -p build && cd build"
echo "   cmake .."
echo "   make -j\$(nproc)"
echo ""

