#!/bin/bash
# BrinkByte Vision - Environment Setup Script
# This script creates a .env file from the template with proper settings

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$SCRIPT_DIR/.env"
TEMPLATE_FILE="$SCRIPT_DIR/env.template"

echo "🔧 BrinkByte Vision - tAPI Environment Setup"
echo "=============================================="
echo ""

# Check if .env already exists
if [ -f "$ENV_FILE" ]; then
    echo "⚠️  .env file already exists at: $ENV_FILE"
    read -p "Do you want to overwrite it? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "❌ Setup cancelled. Keeping existing .env file."
        exit 1
    fi
    echo "🗑️  Removing existing .env file..."
    rm "$ENV_FILE"
fi

# Copy template to .env
if [ ! -f "$TEMPLATE_FILE" ]; then
    echo "❌ Template file not found: $TEMPLATE_FILE"
    exit 1
fi

echo "📝 Creating .env file from template..."
cp "$TEMPLATE_FILE" "$ENV_FILE"

# Update for local development
echo "🔄 Configuring for local development..."

# Update API_PORT to 8090 to match frontend expectations
sed -i 's/API_PORT=8080/API_PORT=8090/' "$ENV_FILE"

# Update BILLING_SERVICE_URL for local billing server
sed -i 's|BILLING_SERVICE_URL=https://billing.brinkbyte.com/api/v1|BILLING_SERVICE_URL=http://localhost:8081/api/v1|' "$ENV_FILE"

# Update BILLING_API_KEY for development
sed -i 's/BILLING_API_KEY=your_api_key_here/BILLING_API_KEY=dev-api-key-123/' "$ENV_FILE"

# Update TENANT_ID for development
sed -i 's/TENANT_ID=your_tenant_uuid_here/TENANT_ID=tenant-123/' "$ENV_FILE"

# Update DEBUG_MODE for development
sed -i 's/DEBUG_MODE=false/DEBUG_MODE=true/' "$ENV_FILE"

# Update LOG_TO_FILE for development
sed -i 's/LOG_TO_FILE=true/LOG_TO_FILE=false/' "$ENV_FILE"

# Update POSTGRES_PASSWORD for development
sed -i 's/POSTGRES_PASSWORD=change_me_in_production/POSTGRES_PASSWORD=tapi_dev_password/' "$ENV_FILE"

echo "✅ Environment file created successfully!"
echo ""
echo "📋 Configuration Summary:"
echo "  • API Port: 8090 (matches frontend expectations)"
echo "  • Billing Service: http://localhost:8081/api/v1"
echo "  • PostgreSQL: localhost:5432"
echo "  • Redis: localhost:6379"
echo "  • Debug Mode: Enabled"
echo ""
echo "🚀 You can now start tAPI with:"
echo "   cd build && ./tAPI"
echo ""
echo "💡 To customize your configuration, edit: $ENV_FILE"
echo ""

