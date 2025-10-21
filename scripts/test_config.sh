#!/bin/bash
set -e

# Configuration
API_URL=${API_URL:-"http://localhost:8080"}
LICENSE_KEY=${LICENSE_KEY:-"PRO-LICENSE-KEY-789"}

# Color codes for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=================================================${NC}"
echo -e "${BLUE}Testing tAPI Global Configuration${NC}"
echo -e "${BLUE}=================================================${NC}"

# Check if API is running
echo "Checking if API server is running at ${API_URL}"
if ! curl -s -o /dev/null -w "%{http_code}" ${API_URL}/health | grep -q "200"; then
  echo -e "${RED}API server is not running at ${API_URL}. Please start it before running the tests.${NC}"
  echo "You can start it with: cd /home/alec/Projects/talking-vision/tAPI/build && ./tAPI --port 8080"
  exit 1
fi

echo -e "${GREEN}API server is running at ${API_URL}${NC}"
echo ""

# Test 1: Check the current configuration
echo -e "${YELLOW}Test 1: Checking current configuration${NC}"
response=$(curl -s ${API_URL}/api/v1/config)
echo "Current configuration:"
echo "$response" | python3 -m json.tool
echo ""

# Test 2: Set custom AI server URL
echo -e "${YELLOW}Test 2: Setting custom AI server URL${NC}"
response=$(curl -s -X PUT -H "Content-Type: application/json" \
    -d '{"ai_server_url": "http://localhost:9000"}' \
    ${API_URL}/api/v1/config/ai_server_url)
echo "Response:"
echo "$response" | python3 -m json.tool
echo ""

# Test 3: Verify the configuration was updated
echo -e "${YELLOW}Test 3: Verifying AI server URL was updated${NC}"
response=$(curl -s ${API_URL}/api/v1/config/ai_server_url)
echo "Updated AI server URL configuration:"
echo "$response" | python3 -m json.tool
echo ""

# Test 4: Set shared memory configuration
echo -e "${YELLOW}Test 4: Setting shared memory configuration${NC}"
response=$(curl -s -X PUT -H "Content-Type: application/json" \
    -d '{"use_shared_memory": true}' \
    ${API_URL}/api/v1/config/use_shared_memory)
echo "Response:"
echo "$response" | python3 -m json.tool
echo ""

# Test 5: Verify shared memory configuration
echo -e "${YELLOW}Test 5: Verifying shared memory configuration${NC}"
response=$(curl -s ${API_URL}/api/v1/config/use_shared_memory)
echo "Updated shared memory configuration:"
echo "$response" | python3 -m json.tool
echo ""

# Adding a sleep to make sure the configuration changes are properly saved
echo -e "${YELLOW}Waiting for configuration to be applied...${NC}"
sleep 2

# Test 6: Create a camera with an object detector to test if it uses the global config
echo -e "${YELLOW}Test 6: Creating camera with object detector${NC}"
# First create a camera
response=$(curl -s -X POST -H "Content-Type: application/json" \
    -d '{"name": "Test Camera"}' \
    ${API_URL}/api/v1/cameras)
camera_id=$(echo "$response" | python3 -c "import sys, json; print(json.load(sys.stdin).get('id', ''))")
echo "Created camera with ID: $camera_id"

# Create a file source for the camera
response=$(curl -s -X POST -H "Content-Type: application/json" \
    -d '{"type": "file", "config": {"url": "/tmp/test.mp4", "width": 640, "height": 480}}' \
    ${API_URL}/api/v1/cameras/${camera_id}/source)
echo "Created source component:"
echo "$response" | python3 -m json.tool
echo ""

# Double-check the current configuration again before creating the processor
echo -e "${YELLOW}Double-checking current configuration before creating processor:${NC}"
response=$(curl -s ${API_URL}/api/v1/config/ai_server_url)
echo "Current AI server URL: $(echo "$response" | python3 -c "import sys, json; print(json.load(sys.stdin).get('ai_server_url', ''))")"

response=$(curl -s ${API_URL}/api/v1/config/use_shared_memory)
echo "Current shared memory: $(echo "$response" | python3 -c "import sys, json; print(json.load(sys.stdin).get('use_shared_memory', ''))")"
echo ""

# Create object detection processor
echo -e "${YELLOW}Creating object detector to test global config usage${NC}"
response=$(curl -s -X POST -H "Content-Type: application/json" \
    -d '{"type": "object_detection", "config": {"model_id": "", "classes": ["person"]}}' \
    ${API_URL}/api/v1/cameras/${camera_id}/processors)
processor_id=$(echo "$response" | python3 -c "import sys, json; print(json.load(sys.stdin).get('id', ''))")
echo "Created processor with ID: $processor_id"
echo ""

# Check the processor configuration to see if it's using our custom AI server URL
echo -e "${YELLOW}Checking processor configuration for server URL${NC}"
response=$(curl -s ${API_URL}/api/v1/cameras/${camera_id}/processors/${processor_id})
server_url=$(echo "$response" | python3 -c "import sys, json; print(json.load(sys.stdin).get('server_url', ''))")
use_shared_memory=$(echo "$response" | python3 -c "import sys, json; print(json.load(sys.stdin).get('use_shared_memory', ''))")

echo "Processor server URL: $server_url"
echo "Processor shared memory: $use_shared_memory"
echo ""

# Check if the values match our configured values
if [[ "$server_url" == "http://localhost:9000" ]]; then
    echo -e "${GREEN}✓ Processor is using the custom AI server URL from global config${NC}"
else
    echo -e "${RED}✗ Processor is NOT using the custom AI server URL from global config${NC}"
fi

if [[ "$use_shared_memory" == "true" || "$use_shared_memory" == "True" ]]; then
    echo -e "${GREEN}✓ Processor is using the shared memory setting from global config${NC}"
else
    echo -e "${RED}✗ Processor is NOT using the shared memory setting from global config${NC}"
fi

# Test 7: Reset to default configuration
echo -e "${YELLOW}Test 7: Resetting to default configuration${NC}"
response=$(curl -s -X DELETE ${API_URL}/api/v1/config/ai_server_url)
echo "Deleted AI server URL config:"
echo "$response" | python3 -m json.tool

response=$(curl -s -X DELETE ${API_URL}/api/v1/config/use_shared_memory)
echo "Deleted shared memory config:"
echo "$response" | python3 -m json.tool
echo ""

# Test 8: Clean up - delete the test camera
echo -e "${YELLOW}Test 8: Cleaning up - deleting test camera${NC}"
response=$(curl -s -X DELETE ${API_URL}/api/v1/cameras/${camera_id})
echo "Deleted camera:"
echo "$response" | python3 -m json.tool
echo ""

echo -e "${GREEN}Test completed successfully!${NC}" 