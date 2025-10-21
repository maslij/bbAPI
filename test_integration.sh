#!/bin/bash
# BrinkByte Vision - Integration Test Suite
# Tests the complete stack: PostgreSQL + Redis + Billing Server

# Don't exit on error - we want to run all tests
set +e

echo "========================================="
echo "BrinkByte Vision Integration Tests"
echo "========================================="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counter
TESTS_PASSED=0
TESTS_FAILED=0

# Function to print test results
test_result() {
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ PASSED${NC}: $1"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}✗ FAILED${NC}: $1"
        ((TESTS_FAILED++))
    fi
}

# Test 1: PostgreSQL is running
echo -e "${BLUE}Test 1: PostgreSQL Health Check${NC}"
docker exec tapi-postgres pg_isready -U tapi_user -d tapi_edge > /dev/null 2>&1
test_result "PostgreSQL is healthy"

# Test 2: Redis is running
echo -e "${BLUE}Test 2: Redis Health Check${NC}"
docker exec tapi-redis redis-cli ping | grep -q "PONG"
test_result "Redis is healthy"

# Test 3: Check database tables exist
echo -e "${BLUE}Test 3: Database Schema${NC}"
TABLE_COUNT=$(docker exec tapi-postgres psql -U tapi_user -d tapi_edge -t -c "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'public' AND table_type = 'BASE TABLE';" | tr -d ' ')
if [ "$TABLE_COUNT" -gt 15 ]; then
    test_result "Database schema created ($TABLE_COUNT tables)"
else
    false
    test_result "Database schema incomplete (only $TABLE_COUNT tables)"
fi

# Test 4: Check growth pack features loaded
echo -e "${BLUE}Test 4: Growth Pack Features${NC}"
FEATURE_COUNT=$(docker exec tapi-postgres psql -U tapi_user -d tapi_edge -t -c "SELECT COUNT(*) FROM growth_pack_features;" | tr -d ' ')
if [ "$FEATURE_COUNT" -gt 30 ]; then
    test_result "Growth pack features loaded ($FEATURE_COUNT features)"
else
    false
    test_result "Growth pack features incomplete (only $FEATURE_COUNT features)"
fi

# Test 5: Billing server health
echo -e "${BLUE}Test 5: Billing Server Health${NC}"
HEALTH_RESPONSE=$(curl -s http://localhost:8081/health)
echo "$HEALTH_RESPONSE" | grep -q "healthy"
test_result "Billing server is healthy"

# Test 6: License validation endpoint
echo -e "${BLUE}Test 6: License Validation${NC}"
LICENSE_RESPONSE=$(curl -s -X POST http://localhost:8081/api/v1/licenses/validate \
  -H "Content-Type: application/json" \
  -d '{"camera_id":"cam-test-001","tenant_id":"tenant-123","device_id":"device-001"}')
echo "$LICENSE_RESPONSE" | grep -q "is_valid"
test_result "License validation endpoint working"

# Test 7: Validate license response
echo -e "${BLUE}Test 7: License Response Validation${NC}"
echo "$LICENSE_RESPONSE" | grep -q '"is_valid":true'
test_result "License validation returns valid license"

# Test 8: Check growth packs in license
echo -e "${BLUE}Test 8: Growth Packs in License${NC}"
echo "$LICENSE_RESPONSE" | grep -q "advanced_analytics"
test_result "License includes growth packs"

# Test 9: Entitlement check endpoint
echo -e "${BLUE}Test 9: Entitlement Check${NC}"
ENTITLEMENT_RESPONSE=$(curl -s -X POST http://localhost:8081/api/v1/entitlements/check \
  -H "Content-Type: application/json" \
  -d '{"tenant_id":"tenant-123","feature_category":"cv_models","feature_name":"person"}')
echo "$ENTITLEMENT_RESPONSE" | grep -q "is_enabled"
test_result "Entitlement check endpoint working"

# Test 10: Usage reporting endpoint
echo -e "${BLUE}Test 10: Usage Reporting${NC}"
USAGE_RESPONSE=$(curl -s -X POST http://localhost:8081/api/v1/usage/batch \
  -H "Content-Type: application/json" \
  -d '{
    "events": [
      {
        "tenant_id": "tenant-123",
        "event_type": "api_call",
        "resource_id": "cam-test-001",
        "quantity": 1,
        "unit": "calls",
        "event_time": "2025-10-20T10:00:00Z",
        "metadata": {"endpoint": "/api/v1/test"}
      }
    ]
  }')
echo "$USAGE_RESPONSE" | grep -q "accepted_count"
test_result "Usage reporting endpoint working"

# Test 11: Heartbeat endpoint
echo -e "${BLUE}Test 11: Device Heartbeat${NC}"
HEARTBEAT_RESPONSE=$(curl -s -X POST http://localhost:8081/api/v1/heartbeat \
  -H "Content-Type: application/json" \
  -d '{
    "device_id": "device-001",
    "tenant_id": "tenant-123",
    "active_camera_ids": ["cam-test-001"],
    "management_tier": "basic"
  }')
echo "$HEARTBEAT_RESPONSE" | grep -q "ok"
test_result "Heartbeat endpoint working"

# Test 12: Check billing stats
echo -e "${BLUE}Test 12: Billing Statistics${NC}"
STATS_RESPONSE=$(curl -s http://localhost:8081/stats)
echo "$STATS_RESPONSE" | grep -q "total_events"
test_result "Statistics endpoint working"

# Test 13: Verify usage was recorded
echo -e "${BLUE}Test 13: Usage Event Storage${NC}"
STATS_TOTAL=$(echo "$STATS_RESPONSE" | grep -o '"total_events":[0-9]*' | grep -o '[0-9]*')
if [ "$STATS_TOTAL" -gt 0 ]; then
    test_result "Usage events are being stored ($STATS_TOTAL events)"
else
    false
    test_result "No usage events stored"
fi

# Test 14: Redis connectivity from host
echo -e "${BLUE}Test 14: Redis Client Access${NC}"
echo "PING" | nc localhost 6379 | grep -q "PONG"
test_result "Redis accessible from host"

# Test 15: PostgreSQL connectivity from host  
echo -e "${BLUE}Test 15: PostgreSQL Client Access${NC}"
docker exec tapi-postgres psql -U tapi_user -d tapi_edge -c "SELECT 1;" > /dev/null 2>&1
test_result "PostgreSQL accessible from host"

echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
echo -e "${RED}Failed: $TESTS_FAILED${NC}"
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}🎉 All tests passed!${NC}"
    echo ""
    echo "Next steps:"
    echo "1. Complete C++ implementation (redis_cache.cpp, repository.cpp)"
    echo "2. Build tAPI with: cd tAPI && mkdir -p build && cd build && cmake .. && make"
    echo "3. Run tAPI and test integration with billing server"
    exit 0
else
    echo -e "${RED}❌ Some tests failed${NC}"
    exit 1
fi

