# BrinkByte Vision - tAPI Billing Integration

## 🎯 Project Overview

This implementation integrates the BrinkByte Vision pricing and licensing model into the tAPI edge service:

- **Base License**: $60/camera/month
- **Growth Packs**: Advanced Analytics, Intelligence, Industry Packs, Integration
- **Edge Device**: $50/month (basic) or $65/month (managed)
- **Usage Tracking**: API calls, LLM tokens, storage, SMS, agent executions
- **Trial**: 2 cameras free for 3 months

## 🏗️ Architecture

```
┌─────────────────────────────────────────┐
│           Edge Device                    │
│  ┌────────┐  ┌──────────┐  ┌─────────┐ │
│  │  tAPI  │──│PostgreSQL│  │  Redis  │ │
│  │  (C++) │  │ (local)  │  │ (cache) │ │
│  └────┬───┘  └──────────┘  └─────────┘ │
└───────┼─────────────────────────────────┘
        │ HTTP
        │
        ▼
┌─────────────────────────────────────────┐
│     Cloud Billing Service (Go)          │
│  - License Management                   │
│  - Usage Aggregation                    │
│  - Subscription Billing                 │
└─────────────────────────────────────────┘
```

## 📁 Project Structure

```
tAPI/
├── docker-compose.yml          # PostgreSQL + Redis services
├── CMakeLists.txt              # Build config with PostgreSQL/Redis
├── env.template                # Configuration template (66 vars)
│
├── scripts/
│   ├── install_system_deps.sh # Install libpq-dev, libhiredis-dev
│   ├── init-postgres.sql      # PostgreSQL initialization
│   └── migrations/
│       ├── 001_initial_schema.sql    # Core tables (20+ tables)
│       └── 002_billing_schema.sql    # Billing tables
│
├── include/
│   ├── database/
│   │   ├── postgres_connection.h    # Connection pool & prepared statements
│   │   └── redis_cache.h            # Redis operations & two-level cache
│   └── billing/
│       └── billing_config.h         # Configuration & growth pack mappings
│
├── src/
│   ├── database/              # TO BE IMPLEMENTED (Priorities 1-2)
│   │   ├── postgres_connection.cpp
│   │   ├── redis_cache.cpp
│   │   └── repository.cpp
│   └── billing/               # TO BE IMPLEMENTED (Priority 3)
│       ├── billing_config.cpp
│       ├── billing_client.cpp
│       ├── license_validator.cpp
│       ├── entitlement_manager.cpp
│       └── usage_tracker.cpp
│
├── BILLING_INTEGRATION.md     # Detailed status & next steps
├── IMPLEMENTATION_SUMMARY.md  # Quick reference guide
└── README_BILLING.md          # This file
```

## 🚀 Quick Start

### 1. Install System Dependencies

```bash
cd /home/alec/projects/brinkbyte/tAPI
chmod +x scripts/install_system_deps.sh
./scripts/install_system_deps.sh
```

This installs:
- `libpq-dev` (PostgreSQL client library)
- `libhiredis-dev` (Redis client library)
- Build tools (cmake, pkg-config, etc.)

### 2. Start Services

```bash
cd /home/alec/projects/brinkbyte
docker-compose up -d postgres redis

# Verify services are running
docker-compose ps
```

### 3. Run Database Migrations

```bash
export PGPASSWORD=tapi_dev_password

# Initialize PostgreSQL
psql -h localhost -U tapi_user -d tapi_edge -f tAPI/scripts/init-postgres.sql

# Create core schema
psql -h localhost -U tapi_user -d tapi_edge -f tAPI/scripts/migrations/001_initial_schema.sql

# Create billing schema
psql -h localhost -U tapi_user -d tapi_edge -f tAPI/scripts/migrations/002_billing_schema.sql

# Verify tables
psql -h localhost -U tapi_user -d tapi_edge -c "\dt"
```

Expected tables (20+):
- `cameras`, `camera_components`, `line_zones`, `polygon_zones`
- `telemetry_events` (partitioned), `frames`, `telemetry_aggregates`
- `edge_devices`, `camera_licenses`, `feature_entitlements`
- `usage_events` (partitioned), `billing_sync_status`
- `growth_pack_features` (pre-loaded with 30+ features)

### 4. Configure Environment

```bash
cp env.template .env

# Edit .env and set required variables:
# - TENANT_ID=<your tenant UUID>
# - BILLING_API_KEY=<your API key>
# - BILLING_SERVICE_URL=https://billing.brinkbyte.com/api/v1

# For local development:
# - MOCK_BILLING_SERVICE=true
```

### 5. Build tAPI (After C++ Implementation)

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

## 📊 Implementation Status

| Phase | Status | Progress |
|-------|--------|----------|
| 1. Infrastructure Setup | ✅ Complete | 100% |
| 2. PostgreSQL Schema | ✅ Complete | 100% |
| 3. Database Abstraction | 🟡 Headers Complete | 40% |
| 4. Billing HTTP Client | ⏳ Not Started | 0% |
| 5. License Validation | ⏳ Not Started | 0% |
| 6. Usage Tracking | ⏳ Not Started | 0% |
| 7. API Integration | ⏳ Not Started | 0% |
| 8. Testing | ⏳ Not Started | 0% |
| 9. Documentation | 🟡 In Progress | 60% |

**Overall Progress**: ~30% Complete

## 📋 Next Implementation Steps

### Priority 1: Database Layer C++ Implementation

1. **`src/database/postgres_connection.cpp`** (~600 lines)
   - Implement connection pool with libpq
   - Prepared statements with parameter binding
   - Result set parsing

2. **`src/database/redis_cache.cpp`** (~500 lines)
   - Implement Redis operations with hiredis
   - JSON serialization/deserialization
   - Two-level caching (memory + Redis)

3. **`src/database/repository.cpp`** (~700 lines)
   - Implement repository pattern
   - Camera, License, Usage, Entitlement, Telemetry repositories
   - CRUD operations with prepared statements

### Priority 2: SQLite Migration

4. **Modify `src/components/sink/database_sink.cpp`** (~500 lines modified)
   - Replace all `sqlite3_*` calls with PostgreSQL
   - Use prepared statements
   - Update SQL syntax

5. **Modify `src/config_manager.cpp`** (~200 lines modified)
   - Replace SQLite with PostgreSQL for config storage

### Priority 3: Billing Service Integration

6. **`src/billing/billing_config.cpp`** (~350 lines)
   - Load configuration from environment
   - Validate required fields
   - Initialize growth pack mappings

7. **`src/billing/billing_client.cpp`** (~500 lines)
   - HTTP client using CURL
   - License validation API calls
   - Usage reporting with batching
   - Heartbeat mechanism

8. **`src/billing/license_validator.cpp`** (~400 lines)
   - 3-tier validation: Redis → PostgreSQL → Billing Service
   - 1-hour cache TTL
   - Offline mode support

9. **`src/billing/entitlement_manager.cpp`** (~350 lines)
   - Feature access control
   - Growth pack mapping
   - Quota management

10. **`src/billing/usage_tracker.cpp`** (~500 lines)
    - Track all usage events
    - Background sync every 5 minutes
    - Batch upload (1000 events at a time)

## 🎓 Key Design Patterns

### Connection Pooling

```cpp
// Always use ConnectionGuard for automatic cleanup
auto conn_guard = postgres_pool_->getConnection();
if (conn_guard.isValid()) {
    PreparedStatement stmt(conn_guard.get(), "insert_event",
        "INSERT INTO usage_events (tenant_id, event_type, quantity) VALUES ($1, $2, $3)");
    stmt.bind(tenant_id).bind("api_call").bind(1);
    stmt.execute();
}
// Connection automatically returned to pool
```

### Two-Level Caching

```cpp
// Try memory cache → Redis → Database → Billing Service
nlohmann::json license_data;
if (two_level_cache_->get(cache_key, license_data)) {
    // Cache hit (fast!)
    return parseLicenseData(license_data);
}

// Cache miss - fetch from billing service
auto response = billing_client_->validateCameraLicense(request);
two_level_cache_->set(cache_key, serializeLicense(response), 3600);
return response;
```

### Graceful Degradation

```cpp
try {
    // Try billing service
    return billing_client_->validateCameraLicense(request);
} catch (const std::exception& e) {
    LOG_WARN("License", "Billing service unavailable, using cached license");
    
    // Fall back to cached license
    if (enable_offline_mode_) {
        return validateFromCache(camera_id, true);  // Allow expired cache
    }
    return ValidationResult{.is_valid = false};
}
```

## 🔍 Database Schema Highlights

### Camera Licenses Table

```sql
CREATE TABLE camera_licenses (
    id UUID PRIMARY KEY,
    camera_id VARCHAR(255) NOT NULL,
    tenant_id UUID NOT NULL,
    license_mode VARCHAR(50) DEFAULT 'trial',  -- trial, base, unlicensed
    is_trial BOOLEAN DEFAULT true,
    trial_end_date TIMESTAMP WITH TIME ZONE,
    enabled_growth_packs JSONB DEFAULT '[]',   -- ["advanced_analytics"]
    status VARCHAR(50) DEFAULT 'active',       -- active, suspended, expired
    last_validated TIMESTAMP WITH TIME ZONE,
    cached_until TIMESTAMP WITH TIME ZONE,
    UNIQUE(camera_id, tenant_id)
);
```

### Usage Events Table (Partitioned)

```sql
CREATE TABLE usage_events (
    id BIGSERIAL,
    tenant_id UUID NOT NULL,
    event_type VARCHAR(100) NOT NULL,          -- api_call, llm_token, storage_gb_day
    resource_id VARCHAR(255),                  -- Camera ID, agent ID, etc.
    quantity DECIMAL(15,5) NOT NULL DEFAULT 1,
    unit VARCHAR(50) NOT NULL,                 -- calls, tokens, GB-days
    synced_to_billing BOOLEAN DEFAULT false,
    event_time TIMESTAMP WITH TIME ZONE NOT NULL,
    PRIMARY KEY (id, event_time)
) PARTITION BY RANGE (event_time);
```

### Growth Pack Features (Pre-Loaded)

```sql
SELECT * FROM growth_pack_features WHERE growth_pack_name = 'base';
-- Results: person, car, van, truck, bus, motorcycle, detection, tracking, etc.

SELECT * FROM growth_pack_features WHERE growth_pack_name = 'advanced_analytics';
-- Results: near_miss, interaction_time, queue_counter, object_size
```

## 📚 Documentation

- **[BILLING_INTEGRATION.md](./BILLING_INTEGRATION.md)** - Detailed implementation status
- **[IMPLEMENTATION_SUMMARY.md](./IMPLEMENTATION_SUMMARY.md)** - Quick reference guide
- **[/tapi.plan.md](/tapi.plan.md)** - Complete CNL-style implementation plan
- **[scripts/migrations/](./scripts/migrations/)** - Database schema reference

## 🎯 Success Criteria

- ✅ Cache hit rate >95% for license validation
- ✅ Usage events sync within 5 minutes
- ✅ System operates offline for 1+ hours (cached licenses)
- ✅ API response time <100ms for license checks (cached)
- ✅ Support 100+ cameras on single edge device
- ✅ Zero data loss during SQLite → PostgreSQL migration
- ✅ Test coverage >85%
- ✅ Feature enforcement blocks unauthorized usage

## 🐛 Troubleshooting

### PostgreSQL connection fails

```bash
# Check if PostgreSQL is running
docker-compose ps postgres

# Check logs
docker-compose logs postgres

# Test connection
psql -h localhost -U tapi_user -d tapi_edge -c "SELECT version();"
```

### Redis connection fails

```bash
# Check if Redis is running
docker-compose ps redis

# Test connection
redis-cli -h localhost ping
```

### Build errors for PostgreSQL/Redis

```bash
# Verify libraries are installed
pkg-config --exists libpq && echo "PostgreSQL OK" || echo "PostgreSQL MISSING"
pkg-config --exists hiredis && echo "Redis OK" || echo "Redis MISSING"

# Reinstall if needed
./scripts/install_system_deps.sh
```

## 📞 Support

For implementation questions or issues:
1. Review documentation in `/tAPI/` directory
2. Check schema migrations in `/tAPI/scripts/migrations/`
3. Refer to header files in `/tAPI/include/` for interfaces
4. Review the plan document at `/tapi.plan.md`

---

**Last Updated**: October 20, 2025  
**Status**: Infrastructure Complete, Ready for C++ Implementation  
**Progress**: ~30% Complete  
**Next Milestone**: Complete Database Abstraction Layer (Phase 3)

