#!/bin/bash
# BrinkByte Vision - tAPI Startup Script with Environment Loading
# This script loads .env variables and starts tAPI

set -a  # Export all variables

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$SCRIPT_DIR/.env"

# Load .env file if it exists
if [ -f "$ENV_FILE" ]; then
    echo "📝 Loading environment from: $ENV_FILE"
    source "$ENV_FILE"
    echo "✅ Environment loaded successfully"
else
    echo "⚠️  Warning: .env file not found at $ENV_FILE"
    echo "   Using default configuration or system environment variables"
fi

set +a  # Stop exporting

# Display key configuration
echo ""
echo "🚀 Starting tAPI with configuration:"
echo "  • API Port: ${API_PORT:-8080} (from $([ -n "$API_PORT" ] && echo ".env" || echo "default"))"
echo "  • Billing URL: ${BILLING_SERVICE_URL:-not set}"
echo "  • PostgreSQL: ${POSTGRES_HOST:-localhost}:${POSTGRES_PORT:-5432}"
echo "  • Redis: ${REDIS_HOST:-localhost}:${REDIS_PORT:-6379}"
echo "  • Debug Mode: ${DEBUG_MODE:-false}"
echo ""

# Change to build directory
cd "$SCRIPT_DIR/build" || {
    echo "❌ Error: build directory not found at $SCRIPT_DIR/build"
    exit 1
}

# Prepare command line arguments
TAPI_ARGS=()

# Add port if specified in .env
if [ -n "$API_PORT" ]; then
    TAPI_ARGS+=(--port "$API_PORT")
fi

# Add any additional arguments passed to this script
TAPI_ARGS+=("$@")

# Start tAPI
echo "🎬 Starting tAPI..."
if [ ${#TAPI_ARGS[@]} -gt 0 ]; then
    echo "   Arguments: ${TAPI_ARGS[*]}"
fi
exec ./tAPI "${TAPI_ARGS[@]}"

