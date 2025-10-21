#!/bin/bash
set -e

# Configuration
API_URL="http://localhost:8090"

# Color codes for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counter
TOTAL_TESTS=0
PASSED_TESTS=0

function print_header() {
    echo -e "\n${YELLOW}=== $1 ===${NC}"
}

function check_status() {
    local expected_status=$1
    local actual_status=$2
    local description=$3
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    if [ "$actual_status" -eq "$expected_status" ]; then
        echo -e "${GREEN}✓ $description - Status code: $actual_status${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        return 0
    else
        echo -e "${RED}✗ $description - Expected status: $expected_status, got: $actual_status${NC}"
        return 1
    fi
}

echo "Running license tests against $API_URL"

# Start the tests
print_header "Testing license verification"

# Test invalid license
echo "Testing invalid license..."
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/json" \
    -d '{"license_key": "INVALID-LICENSE-123"}' \
    ${API_URL}/api/v1/license)
check_status 400 "$STATUS" "Invalid license verification"

# Test basic license
echo "Testing basic license..."
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/json" \
    -d '{"license_key": "BASIC-LICENSE-KEY-123", "owner": "Test User", "email": "test@example.com"}' \
    ${API_URL}/api/v1/license)
check_status 200 "$STATUS" "Basic license verification"

# Test license update
echo "Testing license update..."
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "Content-Type: application/json" \
    -d '{"owner": "Updated User", "email": "updated@example.com"}' \
    ${API_URL}/api/v1/license)
check_status 200 "$STATUS" "License update"

# Delete the license
echo "Testing license deletion..."
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE ${API_URL}/api/v1/license)
check_status 200 "$STATUS" "License deletion"

# Summary
echo -e "\n${YELLOW}=== Test Summary ===${NC}"
echo -e "Total tests: $TOTAL_TESTS"
echo -e "Passed tests: $PASSED_TESTS"

if [ "$PASSED_TESTS" -eq "$TOTAL_TESTS" ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed. ($((TOTAL_TESTS - PASSED_TESTS)) failures)${NC}"
    exit 1
fi 