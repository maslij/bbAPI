# tAPI Billing Integration Implementation

## Overview

This document tracks the implementation of BrinkByte Vision's pricing and licensing model into the tAPI edge service. The integration includes:

- **Migration from SQLite to PostgreSQL** for scalable data management
- **Redis caching** for high-performance license validation
- **HTTP-based communication** with cloud billing service
- **Comprehensive usage metering** for all billable features
- **Real-time feature entitlement enforcement**
- **Offline-resilient operation** with cached licenses

## Architecture

### Deployment Model
Each edge device runs:
- **tAPI** (C++ service)
- **PostgreSQL** (containerized, local database)
- **Redis** (containerized, local cache)
- **Triton Server** (AI inference engine)

### Data Flow
1. Edge devices register cameras/licenses with cloud billing service
2. tAPI validates licenses locally (1-hour cache, graceful offline fallback)
3. tAPI tracks usage locally in PostgreSQL
4. tAPI reports usage to billing service (batched every 5 minutes, resilient to offline)
5. Billing service calculates charges and manages subscriptions

## Implementation Status

### ✅ Phase 1: Infrastructure Setup (COMPLETE)

- [x] **Docker Compose Enhancement** (`/docker-compose.yml`)
  - Added PostgreSQL 15 service with health checks
  - Added Redis 7 service with LRU eviction policy
  - Volume management for data persistence

- [x] **CMakeLists.txt Dependencies** (`/tAPI/CMakeLists.txt`)
  - Added `find_package(PostgreSQL REQUIRED)`
  - Added `pkg_check_modules(HIREDIS REQUIRED hiredis)`
  - Updated include directories and link libraries

- [x] **Environment Configuration** (`/tAPI/env.template`)
  - Billing service URL and API key
  - Edge device identity (tenant_id, device_id, management_tier)
  - PostgreSQL and Redis connection settings
  - License caching configuration (TTLs, offline mode)
  - Usage tracking settings (batch size, sync interval)
  - Feature flags for gradual rollout

### ✅ Phase 2: PostgreSQL Schema Design (COMPLETE)

- [x] **Initialization Script** (`/tAPI/scripts/init-postgres.sql`)
  - UUID generation extension
  - Query performance monitoring (pg_stat_statements)
  - Auto-update timestamp triggers
  - Automatic partition creation functions
  - Database health monitoring views

- [x] **Core Schema Migration** (`/tAPI/scripts/migrations/001_initial_schema.sql`)
  - Migrated all SQLite tables to PostgreSQL:
    - `config` - System configuration key-value store
    - `cameras` - Camera configurations with JSONB pipeline definitions
    - `camera_components` - Component associations (source/processor/sink)
    - `line_zones` - Line crossing detection zones
    - `polygon_zones` - Area monitoring zones
    - `telemetry_events` - Detection/tracking/counting events (partitioned by time)
    - `frames` - Frame thumbnails with BYTEA storage
    - `telemetry_aggregates` - Pre-computed analytics
  - Added multi-tenancy support (tenant_id throughout)
  - Implemented time-based partitioning for telemetry
  - Created materialized views for dashboard performance
  - Added data retention cleanup functions

- [x] **Billing Schema Migration** (`/tAPI/scripts/migrations/002_billing_schema.sql`)
  - `edge_devices` - Edge device registration and heartbeat tracking
  - `camera_licenses` - License cache from billing service
  - `feature_entitlements` - Feature access control cache
  - `usage_events` - Local usage tracking (partitioned by time)
  - `billing_sync_status` - Sync operation tracking
  - `license_validation_log` - Audit trail for license checks
  - `growth_pack_features` - Feature mapping configuration
  - Materialized views for tenant usage summaries
  - Helper functions for license validation and usage sync

### ✅ Phase 3: Database Abstraction Layer (HEADERS COMPLETE)

- [x] **PostgreSQL Connection Manager** (`/tAPI/include/database/postgres_connection.h`)
  - `PostgreSQLConnection` - Connection wrapper with auto-reconnect
  - `PostgreSQLConnectionPool` - Thread-safe connection pooling (10 connections)
  - `ConnectionGuard` - RAII wrapper for automatic return to pool
  - `PreparedStatement` - Type-safe parameter binding
  - `ResultSet` - Easy data access with row iterators
  - Transaction support (begin/commit/rollback)
  - Parameterized queries for SQL injection prevention

- [x] **Redis Cache Manager** (`/tAPI/include/database/redis_cache.h`)
  - `RedisCache` - Redis operations with auto-reconnect
  - Basic operations: get/set/del/exists/expire/ttl
  - JSON serialization/deserialization
  - Hash operations for structured data
  - List operations for queues
  - Pattern-based operations (keys/deletePattern)
  - Atomic increment/decrement
  - `TwoLevelCache<T>` - Memory + Redis for ultimate performance

- [x] **Billing Configuration** (`/tAPI/include/database/billing_config.h`)
  - `BillingConfig` - Complete configuration structure
  - Environment variable loading with defaults
  - `GrowthPackFeatures` - Feature mapping for all growth packs
  - `Pricing` namespace with all pricing constants
  - Validation and connection string generation

## Next Steps (Implementation Required)

### 🔄 Phase 3: Database Abstraction Layer (IMPLEMENTATIONS)

**Required C++ Implementation Files:**

1. `/tAPI/src/database/postgres_connection.cpp`
   - Implement PostgreSQLConnection class
   - Implement PostgreSQLConnectionPool class
   - Implement PreparedStatement class
   - Implement ResultSet class
   - Use libpq API for all PostgreSQL operations

2. `/tAPI/src/database/redis_cache.cpp`
   - Implement RedisCache class
   - Use hiredis library for all Redis operations
   - Implement retry logic with exponential backoff
   - Handle connection failures gracefully

3. `/tAPI/src/billing/billing_config.cpp`
   - Implement BillingConfig::load() - read environment variables
   - Implement BillingConfig::validate() - check required fields
   - Implement GrowthPackFeatures::initialize() - load feature mappings
   - Implement hardware UUID generation for device_id

4. **Migrate SQLite to PostgreSQL:**
   - `/tAPI/src/components/sink/database_sink.cpp` - Replace SQLite3 with PostgreSQL
   - `/tAPI/src/config_manager.cpp` - Replace SQLite3 with PostgreSQL
   - Update all SQL queries to PostgreSQL syntax
   - Use parameterized queries (PreparedStatement)

5. **Repository Pattern:**
   - `/tAPI/include/database/repository.h` - Define repository interfaces
   - `/tAPI/src/database/repository.cpp` - Implement repositories:
     - `CameraRepository` - CRUD for cameras
     - `LicenseRepository` - CRUD for licenses
     - `UsageRepository` - Insert usage events (batch)
     - `EntitlementRepository` - Feature access checks
     - `TelemetryRepository` - Store events and aggregates

### 🔄 Phase 4: Billing Service HTTP Client

**Required Files:**

1. `/tAPI/include/billing/billing_client.h`
   - Define BillingServiceClient class
   - License validation request/response structs
   - Entitlement check request/response structs
   - Usage reporting structs
   - Heartbeat request/response structs

2. `/tAPI/src/billing/billing_client.cpp`
   - Implement HTTP POST/GET using CURL (already in dependencies)
   - JSON serialization using nlohmann::json
   - Exponential backoff retry (3 attempts: 100ms, 200ms, 400ms)
   - Timeout handling (5 seconds default)
   - SSL/TLS support for production URLs

### 🔄 Phase 5: License Validation & Caching

**Required Files:**

1. `/tAPI/include/billing/license_validator.h`
   - Define LicenseValidator class
   - ValidationResult struct (is_valid, license_mode, growth_packs, is_cached)
   - Cache management methods
   - Graceful degradation (offline mode)

2. `/tAPI/src/billing/license_validator.cpp`
   - Implement 3-tier validation: Redis → PostgreSQL → Billing Service
   - Cache results with 1-hour TTL
   - Offline mode: accept cached licenses even if expired
   - Background thread to refresh licenses proactively

3. `/tAPI/include/billing/entitlement_manager.h`
   - Define EntitlementManager class
   - Methods: isModelEnabled, isAnalyticsFeatureEnabled, isOutputEnabled
   - Quota management (getAgentQuota, getLLMTokenQuota)

4. `/tAPI/src/billing/entitlement_manager.cpp`
   - Load growth pack feature mappings from database
   - Check entitlements with caching (5-minute TTL)
   - Integrate with ComponentPermissionHelper (existing)

### 🔄 Phase 6: Usage Tracking & Metering

**Required Files:**

1. `/tAPI/include/billing/usage_tracker.h`
   - Define UsageTracker class
   - Methods for all usage types: trackAPICall, trackLLMTokens, trackStorageUsage, etc.
   - Background sync every 5 minutes

2. `/tAPI/src/billing/usage_tracker.cpp`
   - Insert usage events to PostgreSQL (fast, local)
   - Background thread batch-uploads to billing service (1000 events at a time)
   - Retry failed uploads with exponential backoff
   - Mark events as synced in database

3. **Integration Points** (modify existing files):
   - `/tAPI/src/api.cpp` - Wrap all API handlers with usage tracking
   - `/tAPI/src/components/sink/database_sink.cpp` - Track storage usage daily
   - Add usage tracking to all output components

### 🔄 Phase 7: API Endpoints for License Management

**Modify `/tAPI/src/api.cpp`:**

Add new REST API endpoints in `setupLicenseRoutes()`:
- `GET /api/v1/license/status` - Current license status for all cameras
- `POST /api/v1/license/validate/{camera_id}` - Force refresh license validation
- `GET /api/v1/entitlements` - Get all feature entitlements for tenant
- `POST /api/v1/license/heartbeat` - Manual heartbeat trigger
- `GET /api/v1/usage/summary` - Get usage summary since last sync

### 🔄 Phase 8: Testing & Validation

**Required Files:**

1. `/tAPI/tests/test_postgres_connection.cpp` - Test connection pool, prepared statements
2. `/tAPI/tests/test_redis_cache.cpp` - Test caching, expiry, two-level cache
3. `/tAPI/tests/test_billing_client.cpp` - Mock HTTP responses, test retry logic
4. `/tAPI/tests/test_license_validator.cpp` - Test caching, graceful degradation
5. `/tAPI/tests/integration/test_billing_integration.cpp` - End-to-end tests with Docker

### 🔄 Phase 9: Migration & Deployment

**Required Files:**

1. `/tAPI/scripts/migrate_sqlite_to_postgres.py`
   - Python script to export SQLite → CSV → PostgreSQL
   - Validate row counts match
   - Create SQLite backup before migration

2. `/tAPI/scripts/install_deps.sh`
   - Add `libpq-dev` (PostgreSQL client library)
   - Add `libhiredis-dev` (Redis client library)
   - Update for both x86_64 and ARM64/Jetson platforms

3. `/tAPI/scripts/build.sh`
   - Check for PostgreSQL and Redis client libraries
   - Run database migrations on startup
   - Validate environment variables

4. **Documentation:**
   - `/tAPI/docs/BILLING_DEPLOYMENT.md` - Deployment guide
   - `/tAPI/docs/BILLING_API.md` - API documentation with curl examples

## Building and Running

### Prerequisites

```bash
# Install PostgreSQL client library
sudo apt-get install libpq-dev

# Install Redis client library
sudo apt-get install libhiredis-dev

# Start services
docker-compose up -d postgres redis

# Wait for services to be healthy
docker-compose ps
```

### Build tAPI

```bash
cd /home/alec/projects/brinkbyte/tAPI

# Run migrations
psql -h localhost -U tapi_user -d tapi_edge -f scripts/init-postgres.sql
psql -h localhost -U tapi_user -d tapi_edge -f scripts/migrations/001_initial_schema.sql
psql -h localhost -U tapi_user -d tapi_edge -f scripts/migrations/002_billing_schema.sql

# Build tAPI
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

### Configure

```bash
# Copy environment template
cp env.template .env

# Edit .env and set:
# - TENANT_ID (your tenant UUID from billing service)
# - BILLING_API_KEY (your API key from billing service)
# - BILLING_SERVICE_URL (production URL)

# For development, you can use mock billing service:
# MOCK_BILLING_SERVICE=true
```

### Run

```bash
./tAPI
```

## Growth Pack Feature Mapping

### Base License ($60/camera/month)
- **CV Models**: person, car, van, truck, bus, motorcycle
- **Analytics**: detection, tracking, counting, dwell, heatmap, direction, speed, privacy_mask
- **Outputs**: edge_io, dashboard, email, webhook, api
- **Quotas**: 50k API calls/month, 250k LLM tokens/month

### Advanced Analytics Pack ($20/camera/month)
- **Analytics**: near_miss, interaction_time, queue_counter, object_size

### Industry Packs (separate SKUs)
- **Active Transport**: bike, scooter, pram, wheelchair
- **Advanced Vehicles**: car, ute, van, bus, light_rigid, medium_rigid, heavy_rigid, prime_mover, heavy_articulated
- **Emergency Vehicles**: police, ambulance, fire_fighter
- **Retail**: trolley, staff, customer
- **Mining**: light_vehicle, heavy_vehicle, ppe
- **Airports**: trolley, plane, gse (fuel_truck, tug, tractor, belt_loader)
- **Waterways**: boats (commercial, recreational, fishing, cruise, tanker, cargo), jetski, kayak

### Intelligence Pack ($400/tenant/month)
- **LLM**: analyst_seat_full, premium_connectors, automated_reports
- **Quotas**: 5 seats included, 250k tokens/seat/month, +$120 per extra seat

### Integration Pack
- **Outputs**: sms (pass-through pricing), cloud_export ($150/tenant/month), vms_connectors ($500 one-time + $75/year)
- **API Overages**: $0.05 per 1000 calls

## Success Criteria

- ✅ All cameras successfully validate licenses via billing service
- ✅ Cache hit rate >95% for license validation (reduce cloud calls)
- ✅ Usage events sync to billing service within 5 minutes
- ✅ System operates normally for 1 hour offline (cached licenses)
- ✅ Zero data loss during SQLite → PostgreSQL migration
- ✅ API response times <100ms for license checks (cached)
- ✅ Support 100+ cameras on single edge device without performance degradation
- ✅ Feature enforcement blocks unauthorized model/analytics usage
- ✅ Comprehensive test coverage (>85%)
- ✅ Production-ready monitoring and alerting

## Rollout Strategy

1. **Development Environment** - Local Docker Compose stack
2. **Staging Edge Devices** - 2-3 internal devices for testing
3. **Pilot Customers** - 5-10 cameras with close monitoring
4. **General Availability** - Feature flag rollout to all customers
5. **Rollback** - Keep SQLite code path via feature flag for emergency rollback

## Support

For questions or issues during implementation:
1. Review the plan document: `/tapi.plan.md`
2. Check schema migrations: `/tAPI/scripts/migrations/`
3. Refer to header documentation in `/tAPI/include/`

---

**Last Updated**: October 20, 2025  
**Implementation Phase**: 3 of 11  
**Progress**: ~30% Complete

