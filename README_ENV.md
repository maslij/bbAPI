# tAPI Environment Configuration Guide

## Quick Start

For **local development**, run the setup script:

```bash
./setup-env.sh
```

This will create a `.env` file configured for local development with:
- API running on port **8090** (matches frontend)
- Local billing server at `http://localhost:8081`
- Local PostgreSQL and Redis
- Debug mode enabled

## Manual Setup

If you prefer to configure manually:

```bash
cp env.template .env
# Edit .env with your preferred settings
```

## Configuration Files

- **`.env`**: Your local configuration (gitignored, safe for secrets)
- **`.env.example`**: Example configuration with production defaults
- **`env.template`**: Template with all available options
- **`config.env`**: Legacy config file (deprecated, use `.env` instead)

## Key Configuration Options

### API Server

```bash
API_PORT=8090              # Port for tAPI to listen on
API_THREADS=4              # Number of worker threads
API_ENABLE_CORS=true       # Enable CORS for frontend
```

**Important**: The frontend expects tAPI on port **8090**. If you change this, you must also update the frontend's `VITE_API_URL` setting.

### Billing Service

```bash
BILLING_SERVICE_URL=http://localhost:8081/api/v1  # Local dev
# BILLING_SERVICE_URL=https://billing.brinkbyte.com/api/v1  # Production

BILLING_API_KEY=dev-api-key-123  # Your API key
BILLING_TIMEOUT_MS=5000          # Request timeout
BILLING_MAX_RETRIES=3            # Retry attempts
```

### Database & Cache

```bash
# PostgreSQL
POSTGRES_HOST=localhost
POSTGRES_PORT=5432
POSTGRES_DATABASE=tapi_edge
POSTGRES_USER=tapi_user
POSTGRES_PASSWORD=tapi_dev_password  # CHANGE IN PRODUCTION!

# Redis
REDIS_HOST=localhost
REDIS_PORT=6379
REDIS_PASSWORD=                      # Leave empty for local dev
```

### Device Identity

```bash
EDGE_DEVICE_ID=auto       # Auto-generate from hardware UUID
TENANT_ID=tenant-123      # Your tenant identifier
MANAGEMENT_TIER=basic     # Options: basic, managed
```

### License & Offline Mode

```bash
LICENSE_CACHE_TTL_SECONDS=3600      # Cache licenses for 1 hour
ENABLE_OFFLINE_MODE=true            # Allow offline operation
OFFLINE_GRACE_PERIOD_HOURS=24       # Grace period when offline
BYPASS_LICENSE_CHECK=false          # DANGEROUS: Dev only!
```

### Usage Tracking

```bash
TRACK_API_CALLS=true
TRACK_LLM_TOKENS=true
TRACK_STORAGE=true
TRACK_AGENT_EXECUTIONS=true
TRACK_SMS=true

USAGE_BATCH_SIZE=1000               # Batch size before sync
USAGE_SYNC_INTERVAL_MINUTES=5       # Sync frequency
```

### Logging

```bash
LOG_LEVEL=INFO                      # DEBUG, INFO, WARN, ERROR
LOG_TO_FILE=false                   # Enable file logging
LOG_FILE_PATH=/var/log/tapi/tapi.log
```

## Environment Loading

tAPI loads configuration in this order (highest precedence first):

1. **Environment variables** - System environment (highest priority)
2. **`.env` file** - Local development configuration
3. **Default values** - Hardcoded defaults in `billing_config.cpp`

This means you can override any `.env` setting by exporting it:

```bash
export API_PORT=9000
./tAPI  # Will use port 9000 instead of .env value
```

## Production Deployment

For production deployments:

1. **Copy the example file**:
   ```bash
   cp .env.example .env
   ```

2. **Update critical settings**:
   - Change `POSTGRES_PASSWORD` to a strong password
   - Set `BILLING_SERVICE_URL` to production URL
   - Update `BILLING_API_KEY` with your production key
   - Set `TENANT_ID` to your actual tenant UUID
   - Disable `DEBUG_MODE=false`
   - Enable `LOG_TO_FILE=true`

3. **Secure the file**:
   ```bash
   chmod 600 .env  # Only owner can read/write
   ```

4. **Use environment variables** for secrets in containerized deployments:
   ```bash
   docker run -e POSTGRES_PASSWORD="$SECURE_PASSWORD" \
              -e BILLING_API_KEY="$API_KEY" \
              tapi:latest
   ```

## Docker Compose

When using Docker Compose, environment variables are automatically loaded:

```yaml
services:
  tapi:
    image: tapi:latest
    env_file:
      - .env
    # Or override specific vars:
    environment:
      - API_PORT=8090
      - DEBUG_MODE=false
```

## Frontend Integration

The frontend expects tAPI on **port 8090** by default. If you change `API_PORT`:

1. Update tAPI's `.env`:
   ```bash
   API_PORT=9000
   ```

2. Update frontend's `tWeb/.env`:
   ```bash
   VITE_API_URL=http://localhost:9000
   ```

## Troubleshooting

### Port Already in Use

If port 8090 is already in use:

```bash
# Find what's using the port
lsof -i :8090

# Either kill that process or change tAPI's port
echo "API_PORT=8091" >> .env
```

### Cannot Connect to Services

Verify services are running:

```bash
# Check PostgreSQL
docker exec tapi-postgres pg_isready -U tapi_user -d tapi_edge

# Check Redis
docker exec tapi-redis redis-cli ping

# Check Billing Server
curl http://localhost:8081/health
```

### Environment Not Loading

1. Ensure `.env` is in the same directory as the tAPI binary:
   ```bash
   ls -la .env
   ```

2. Check file permissions:
   ```bash
   chmod 644 .env
   ```

3. Verify values are being loaded:
   ```bash
   # Enable debug logging
   export LOG_LEVEL=DEBUG
   ./tAPI
   # Check logs for "Loading billing configuration from environment"
   ```

## Security Best Practices

✅ **DO**:
- Keep `.env` files out of version control (gitignored)
- Use strong passwords in production
- Rotate API keys regularly
- Use environment variables for secrets in CI/CD
- Set restrictive file permissions (`chmod 600 .env`)

❌ **DON'T**:
- Commit `.env` files to git
- Share `.env` files in chat/email
- Use default passwords in production
- Enable `BYPASS_LICENSE_CHECK` in production
- Leave `DEBUG_MODE=true` in production

## Support

For questions or issues:
- Check the main README: `/tAPI/README.md`
- Review billing integration docs: `/tAPI/BILLING_INTEGRATION.md`
- Contact: support@brinkbyte.com

