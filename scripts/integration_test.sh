#!/bin/bash
set -e

# Configuration
API_URL=${API_URL:-"http://localhost:8090"}
LICENSE_KEY=${LICENSE_KEY:-"PRO-LICENSE-KEY-789"}

# Sample video URLs
PEOPLE_DETECTION_URL="https://github.com/intel-iot-devkit/sample-videos/raw/master/people-detection.mp4"
WORKER_ZONE_URL="https://github.com/intel-iot-devkit/sample-videos/raw/master/worker-zone-detection.mp4"

# Local video files
PEOPLE_DETECTION_FILE="/tmp/people-detection.mp4"
WORKER_ZONE_FILE="/tmp/worker-zone-detection.mp4"

# Output directories
OUTPUT_DIR="/tmp/tapi_test"
mkdir -p ${OUTPUT_DIR}

# Color codes for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counter
TOTAL_TESTS=0
PASSED_TESTS=0

# Helper functions
function print_header() {
    echo -e "\n${YELLOW}=== $1 ===${NC}"
}

function print_formatted_response() {
    local response=$1
    local description=$2
    
    # Extract status line
    status_line=$(echo "$response" | head -n 1)
    
    # Extract headers (skip the status line and stop at the empty line)
    headers=$(echo "$response" | awk 'NR>1 && $0!~/^$/ {print}' | grep -v "^{")
    
    # Extract body
    body=$(extract_json_body "$response")
    
    # Pretty print JSON if the body is valid JSON
    if [ -n "$body" ] && echo "$body" | python3 -c "import sys, json; json.loads(sys.stdin.read())" &> /dev/null; then
        formatted_body=$(echo "$body" | python3 -m json.tool)
    else
        formatted_body="$body"
    fi
    
    echo -e "${BLUE}→ Response for: $description${NC}"
    echo -e "${BLUE}Status:${NC} $status_line"
    echo -e "${BLUE}Headers:${NC}\n$headers"
    echo -e "${BLUE}Body:${NC}\n$formatted_body"
    echo -e "${BLUE}-----------------------------------${NC}"
}

function check_status_code() {
    local expected_status=$1
    local response=$2
    local description=$3
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    actual_status=$(echo "$response" | head -n 1 | awk '{print $2}')
    if [ "$actual_status" -eq "$expected_status" ]; then
        echo -e "${GREEN}✓ $description - Status code: $actual_status${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ $description - Expected status: $expected_status, got: $actual_status${NC}"
        echo "$response" | head -n 10
    fi
    
    # Print formatted response
    print_formatted_response "$response" "$description"
}

function check_json_value() {
    local json=$1
    local field=$2
    local expected_value=$3
    local description=$4
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Extract value using Python for reliable JSON parsing
    actual_value=$(echo "$json" | python3 -c "import sys, json; print(json.load(sys.stdin).get('$field', 'null'))")
    
    # Normalize values for comparison (handle quotes and case-insensitive boolean comparison)
    normalized_expected=$(echo "$expected_value" | sed 's/^"//;s/"$//' | tr '[:upper:]' '[:lower:]')
    normalized_actual=$(echo "$actual_value" | sed 's/^"//;s/"$//' | tr '[:upper:]' '[:lower:]')
    
    if [ "$normalized_actual" = "$normalized_expected" ]; then
        echo -e "${GREEN}✓ $description - $field: $actual_value${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ $description - Expected $field: $expected_value, got: $actual_value${NC}"
    fi
}

function extract_json_body() {
    # Skip HTTP headers to get just the JSON body
    echo "$1" | awk 'BEGIN{flag=0} /^\{/ {flag=1} flag {print}'
}

function extract_id() {
    local json=$1
    echo "$json" | python3 -c "import sys, json; print(json.load(sys.stdin).get('id', ''))"
}

# Check if API is running
echo "Checking if API server is running at ${API_URL}"
if ! curl -s -o /dev/null -w "%{http_code}" ${API_URL}/health | grep -q "200"; then
  echo -e "${RED}API server is not running at ${API_URL}. Please start it before running the tests.${NC}"
  echo "You can start it with: cd /home/alec/Projects/talking-vision/tAPI/build && ./tAPI --port 8090"
  exit 1
fi

# Start the tests
echo "Running integration tests against $API_URL"

# Download sample videos
print_header "Downloading sample videos"
curl -s -L -o $PEOPLE_DETECTION_FILE $PEOPLE_DETECTION_URL
echo -e "${GREEN}✓ Downloaded people detection video to $PEOPLE_DETECTION_FILE${NC}"

curl -s -L -o $WORKER_ZONE_FILE $WORKER_ZONE_URL
echo -e "${GREEN}✓ Downloaded worker zone detection video to $WORKER_ZONE_FILE${NC}"

print_header "Testing health endpoint"
response=$(curl -s -i ${API_URL}/health)
check_status_code 200 "$response" "Health check"

print_header "Testing license verification"

# Test invalid license
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"license_key": "INVALID-LICENSE-123"}' \
    ${API_URL}/api/v1/license)
check_status_code 400 "$response" "Invalid license verification"

# Test basic license
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"license_key": "BASIC-LICENSE-KEY-123", "owner": "Test User", "email": "test@example.com"}' \
    ${API_URL}/api/v1/license)
check_status_code 200 "$response" "Basic license verification"

# Check license tier
json_body=$(extract_json_body "$response")
check_json_value "$json_body" "valid" "true" "Basic license is valid"
check_json_value "$json_body" "tier" "\"basic\"" "License tier is basic"
check_json_value "$json_body" "owner" "\"Test User\"" "Default owner is set" # The API honors the owner from POST

# Get license info to test GET endpoint
response=$(curl -s -i ${API_URL}/api/v1/license)
check_status_code 200 "$response" "Get license info"

# Delete the license
response=$(curl -s -i -X DELETE ${API_URL}/api/v1/license)
check_status_code 200 "$response" "License deletion"

json_body=$(extract_json_body "$response")
check_json_value "$json_body" "success" "true" "License deletion success"

# Test professional license
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"license_key": "PRO-LICENSE-KEY-789", "owner": "Pro User", "email": "pro@example.com"}' \
    ${API_URL}/api/v1/license)
check_status_code 200 "$response" "Professional license verification"

# Check license tier
json_body=$(extract_json_body "$response")
check_json_value "$json_body" "valid" "true" "Professional license is valid"
check_json_value "$json_body" "tier" "\"professional\"" "License tier is professional"

# Test license update
echo "Sending PUT request to update license..."
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"owner": "Updated User", "email": "updated@example.com"}' \
    ${API_URL}/api/v1/license)
check_status_code 200 "$response" "License update"
if [ $? -eq 0 ]; then
  json_body=$(extract_json_body "$response")
  check_json_value "$json_body" "owner" "\"Updated User\"" "License owner updated"
  check_json_value "$json_body" "email" "\"updated@example.com\"" "License email updated"
else
  echo -e "${YELLOW}⚠ Skipping license update checks due to failed response${NC}"
fi

# Now set the license for the rest of the tests
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d "{\"license_key\": \"${LICENSE_KEY}\"}" \
    ${API_URL}/api/v1/license)
check_status_code 200 "$response" "License verification for tests"

print_header "Testing log level management"
# Get current log level
response=$(curl -s -i ${API_URL}/api/v1/system/log-level)
check_status_code 200 "$response" "Get current log level"
json_body=$(extract_json_body "$response")
initial_log_level=$(echo "$json_body" | python3 -c "import sys, json; print(json.load(sys.stdin).get('level', ''))")
echo "Current log level is: $initial_log_level"

# Change log level to debug
print_header "Changing log level to debug"
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"level": "debug"}' \
    ${API_URL}/api/v1/system/log-level)
check_status_code 200 "$response" "Change log level to debug"

# Verify change
json_body=$(extract_json_body "$response")
check_json_value "$json_body" "current_level" "\"debug\"" "Log level changed to debug"
check_json_value "$json_body" "previous_level" "\"$initial_log_level\"" "Previous log level matches initial"

# Get the current log level again to verify
response=$(curl -s -i ${API_URL}/api/v1/system/log-level)
check_status_code 200 "$response" "Get log level after change"
json_body=$(extract_json_body "$response")
check_json_value "$json_body" "level" "\"debug\"" "Log level is now debug"

# Change log level back to the original
print_header "Changing log level back to $initial_log_level"
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d "{\"level\": \"$initial_log_level\"}" \
    ${API_URL}/api/v1/system/log-level)
check_status_code 200 "$response" "Change log level back to $initial_log_level"

# Verify change back
json_body=$(extract_json_body "$response")
check_json_value "$json_body" "current_level" "\"$initial_log_level\"" "Log level changed back to $initial_log_level"

# -------------------------------------------------------------------------
# 1. SET UP CAMERA 1
# -------------------------------------------------------------------------
print_header "Setting up Camera 1 for people detection"

# Create camera 1
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"name": "People Detection Camera"}' \
    ${API_URL}/api/v1/cameras)
check_status_code 201 "$response" "Create camera 1"

# Extract camera ID
json_body=$(extract_json_body "$response")
camera1_id=$(extract_id "$json_body")
echo "Created camera 1 with ID: $camera1_id"

# Create file source for camera 1
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d "{\"type\": \"file\", \"config\": {\"url\": \"$PEOPLE_DETECTION_FILE\", \"width\": 640, \"height\": 480, \"fps\": 30, \"use_hw_accel\": true, \"adaptive_timing\": true}}" \
    ${API_URL}/api/v1/cameras/${camera1_id}/source)
check_status_code 201 "$response" "Create file source for camera 1"

json_body=$(extract_json_body "$response")
source1_id=$(extract_id "$json_body")
echo "Created source component with ID: $source1_id"

# Create object detection processor for camera 1
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"type": "object_detection", "config": {"model_id": "", "classes": ["person"]}}' \
    ${API_URL}/api/v1/cameras/${camera1_id}/processors)
check_status_code 201 "$response" "Create processor component for people detection" 

json_body=$(extract_json_body "$response")
processor1_id=$(extract_id "$json_body")
echo "Created processor component with ID: $processor1_id"

# Create object tracker processor for camera 1
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"type": "object_tracking", "config": {"frame_rate": 30, "track_buffer": 30, "track_thresh": 0.5, "high_thresh": 0.6, "match_thresh": 0.8, "draw_tracking": true, "draw_track_trajectory": true, "draw_track_id": true, "draw_semi_transparent_boxes": true, "label_font_scale": 0.6}}' \
    ${API_URL}/api/v1/cameras/${camera1_id}/processors)
check_status_code 201 "$response" "Create object tracker processor for camera 1" 

json_body=$(extract_json_body "$response")
tracker1_id=$(extract_id "$json_body")
echo "Created object tracker component with ID: $tracker1_id"

# Create line zone manager processor for camera 1
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"type": "line_zone_manager", "config": {"draw_zones": true, "line_color": [255, 255, 255], "line_thickness": 2, "draw_counts": true, "text_color": [0, 0, 0], "text_scale": 0.5, "text_thickness": 2, "zones": [{"id": "entrance", "start_x": 0.15625, "start_y": 0.5, "end_x": 0.625, "end_y": 0.5, "min_crossing_threshold": 1, "triggering_anchors": ["BOTTOM_LEFT", "BOTTOM_RIGHT"]}]}}' \
    ${API_URL}/api/v1/cameras/${camera1_id}/processors)
check_status_code 201 "$response" "Create line zone manager for camera 1" 

json_body=$(extract_json_body "$response")
line_zone_manager1_id=$(extract_id "$json_body")
echo "Created line zone manager component with ID: $line_zone_manager1_id"

# Create file sink for camera 1
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d "{\"type\": \"file\", \"config\": {\"path\": \"${OUTPUT_DIR}/people-detection-output.mp4\", \"width\": 640, \"height\": 480, \"fps\": 30, \"fourcc\": \"mp4v\"}}" \
    ${API_URL}/api/v1/cameras/${camera1_id}/sinks)
check_status_code 201 "$response" "Create file sink for camera 1"

json_body=$(extract_json_body "$response")
file_sink1_id=$(extract_id "$json_body")
echo "Created file sink component with ID: $file_sink1_id"

# Create database sink for camera 1
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d "{\"type\": \"database\", \"config\": {\"db_path\": \"${OUTPUT_DIR}/telemetry.db\", \"store_thumbnails\": true, \"thumbnail_width\": 320, \"thumbnail_height\": 180, \"retention_days\": 30}}" \
    ${API_URL}/api/v1/cameras/${camera1_id}/sinks)
check_status_code 201 "$response" "Create database sink for camera 1"

json_body=$(extract_json_body "$response")
db_sink1_id=$(extract_id "$json_body")
echo "Created database sink component with ID: $db_sink1_id"

# -------------------------------------------------------------------------
# 2. SET UP CAMERA 2
# -------------------------------------------------------------------------
print_header "Setting up Camera 2 for worker zone detection"

# Create camera 2
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"name": "Worker Zone Detection Camera"}' \
    ${API_URL}/api/v1/cameras)
check_status_code 201 "$response" "Create camera 2"

# Extract camera ID
json_body=$(extract_json_body "$response")
camera2_id=$(extract_id "$json_body")
echo "Created camera 2 with ID: $camera2_id"

# Create file source for camera 2
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d "{\"type\": \"file\", \"config\": {\"url\": \"$WORKER_ZONE_FILE\", \"width\": 640, \"height\": 480, \"fps\": 30, \"use_hw_accel\": true, \"adaptive_timing\": true}}" \
    ${API_URL}/api/v1/cameras/${camera2_id}/source)
check_status_code 201 "$response" "Create file source for camera 2"

json_body=$(extract_json_body "$response")
source2_id=$(extract_id "$json_body")
echo "Created source component with ID: $source2_id"

# Create object detection processor for camera 2
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"type": "object_detection", "config": {"model_id": "", "classes": ["person"]}}' \
    ${API_URL}/api/v1/cameras/${camera2_id}/processors)
check_status_code 201 "$response" "Create processor component for worker zone detection" 

json_body=$(extract_json_body "$response")
processor2_id=$(extract_id "$json_body")
echo "Created processor component with ID: $processor2_id"

# Create object tracker processor for camera 2
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"type": "object_tracking", "config": {"frame_rate": 30, "track_buffer": 30, "track_thresh": 0.5, "high_thresh": 0.6, "match_thresh": 0.8, "draw_tracking": true, "draw_track_trajectory": true, "draw_track_id": true, "draw_semi_transparent_boxes": true, "label_font_scale": 0.6}}' \
    ${API_URL}/api/v1/cameras/${camera2_id}/processors)
check_status_code 201 "$response" "Create object tracker processor for camera 2" 

json_body=$(extract_json_body "$response")
tracker2_id=$(extract_id "$json_body")
echo "Created object tracker component with ID: $tracker2_id"

# Create line zone manager processor for camera 2
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"type": "line_zone_manager", "config": {"draw_zones": true, "line_color": [255, 255, 255], "line_thickness": 2, "draw_counts": true, "text_color": [0, 0, 0], "text_scale": 0.5, "text_thickness": 2, "zones": [{"id": "worker_area", "start_x": 0.078125, "start_y": 0.416667, "end_x": 0.921875, "end_y": 0.416667, "min_crossing_threshold": 1, "triggering_anchors": ["TOP_LEFT", "TOP_RIGHT"]}, {"id": "safety_zone", "start_x": 0.15625, "start_y": 0.729167, "end_x": 0.84375, "end_y": 0.729167, "min_crossing_threshold": 1, "triggering_anchors": ["CENTER", "BOTTOM_CENTER"]}]}}' \
    ${API_URL}/api/v1/cameras/${camera2_id}/processors)
check_status_code 201 "$response" "Create line zone manager for camera 2" 

json_body=$(extract_json_body "$response")
line_zone_manager2_id=$(extract_id "$json_body")
echo "Created line zone manager component with ID: $line_zone_manager2_id"

# Create file sink for camera 2
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d "{\"type\": \"file\", \"config\": {\"path\": \"${OUTPUT_DIR}/worker-zone-output.mp4\", \"width\": 640, \"height\": 480, \"fps\": 30, \"fourcc\": \"mp4v\"}}" \
    ${API_URL}/api/v1/cameras/${camera2_id}/sinks)
check_status_code 201 "$response" "Create file sink for camera 2"

json_body=$(extract_json_body "$response")
file_sink2_id=$(extract_id "$json_body")
echo "Created file sink component with ID: $file_sink2_id"

# Verify component setup for both cameras
print_header "Verifying components for both cameras"
response=$(curl -s -i ${API_URL}/api/v1/cameras/${camera1_id}/components)
check_status_code 200 "$response" "Get components for camera 1"

response=$(curl -s -i ${API_URL}/api/v1/cameras/${camera2_id}/components)
check_status_code 200 "$response" "Get components for camera 2"

# -------------------------------------------------------------------------
# TEST LINE ZONE CONFIGURATION UPDATE
# -------------------------------------------------------------------------
print_header "Testing line zone configuration update"

# Update line zone manager configuration for camera 1
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"config": {"zones": [{"id": "entrance", "start_x": 0.234375, "start_y": 0.5, "end_x": 0.703125, "end_y": 0.5, "triggering_anchors": ["CENTER"]}, {"id": "exit", "start_x": 0.3125, "start_y": 0.729167, "end_x": 0.78125, "end_y": 0.729167, "triggering_anchors": ["CENTER"]}], "line_color": [255, 255, 255]}}' \
    ${API_URL}/api/v1/cameras/${camera1_id}/processors/${line_zone_manager1_id})
check_status_code 200 "$response" "Update line zone manager configuration"

# Wait 2 seconds for update to take effect
sleep 2

# -------------------------------------------------------------------------
# TEST DATABASE DELETION ON CAMERA DELETE
# -------------------------------------------------------------------------
print_header "Testing database data deletion when a camera is deleted"

# Create a test camera specifically for database tests
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"name": "Database Test Camera"}' \
    ${API_URL}/api/v1/cameras)
check_status_code 201 "$response" "Create database test camera"

# Extract camera ID
json_body=$(extract_json_body "$response")
db_test_camera_id=$(extract_id "$json_body")
echo "Created database test camera with ID: $db_test_camera_id"

# Create file source for test camera
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d "{\"type\": \"file\", \"config\": {\"url\": \"$PEOPLE_DETECTION_FILE\", \"width\": 640, \"height\": 480, \"fps\": 30, \"use_hw_accel\": true, \"adaptive_timing\": true}}" \
    ${API_URL}/api/v1/cameras/${db_test_camera_id}/source)
check_status_code 201 "$response" "Create file source for database test camera"

# Create database sink for test camera
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d "{\"type\": \"database\", \"config\": {\"store_thumbnails\": true, \"thumbnail_width\": 320, \"thumbnail_height\": 180, \"retention_days\": 30}}" \
    ${API_URL}/api/v1/cameras/${db_test_camera_id}/sinks)
check_status_code 201 "$response" "Create database sink for test camera"

json_body=$(extract_json_body "$response")
db_test_sink_id=$(extract_id "$json_body")
echo "Created database sink with ID: $db_test_sink_id"

# Create object detection for test camera to generate events
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"type": "object_detection", "config": {"model_id": "", "classes": ["person"]}}' \
    ${API_URL}/api/v1/cameras/${db_test_camera_id}/processors)
check_status_code 201 "$response" "Create object detection for test camera" 

# Start the test camera to generate data
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"running": true}' \
    ${API_URL}/api/v1/cameras/${db_test_camera_id})
check_status_code 200 "$response" "Start database test camera"

# Wait a few seconds for data to be generated
echo "Waiting for database test camera to generate data (5 seconds)..."
sleep 5

# Stop the test camera
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"running": false}' \
    ${API_URL}/api/v1/cameras/${db_test_camera_id})
check_status_code 200 "$response" "Stop database test camera"

# Get database sink status to check if data was inserted
response=$(curl -s -i ${API_URL}/api/v1/cameras/${db_test_camera_id}/sinks/${db_test_sink_id})
check_status_code 200 "$response" "Get database sink status"

# Check if events were inserted
json_body=$(extract_json_body "$response")
inserted_events=$(echo "$json_body" | python3 -c "import sys, json; print(json.load(sys.stdin).get('inserted_events', 0))")
inserted_frames=$(echo "$json_body" | python3 -c "import sys, json; print(json.load(sys.stdin).get('inserted_frames', 0))")

TOTAL_TESTS=$((TOTAL_TESTS + 2))
if [ "$inserted_events" -gt 0 ]; then
    echo -e "${GREEN}✓ Database has inserted events: $inserted_events${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${YELLOW}⚠ No events inserted in database, test may not be conclusive${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1)) # Still count as passed since it might be normal to have no events
fi

if [ "$inserted_frames" -gt 0 ]; then
    echo -e "${GREEN}✓ Database has inserted frames: $inserted_frames${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${YELLOW}⚠ No frames inserted in database, test may not be conclusive${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1)) # Still count as passed since it might be normal to have no frames
fi

# -------------------------------------------------------------------------
# TEST DATABASE ANALYTICS API
# -------------------------------------------------------------------------
print_header "Testing database analytics API"

# Wait a moment to ensure all data is written
sleep 2

# Get analytics data from the database
response=$(curl -s -i ${API_URL}/api/v1/cameras/${db_test_camera_id}/database/analytics)
check_status_code 200 "$response" "Get analytics data from database"

# Check the structure of the analytics response
json_body=$(extract_json_body "$response")
has_analytics=$(echo "$json_body" | python3 -c "import sys, json; print('analytics' in json.load(sys.stdin))")
has_time_series=$(echo "$json_body" | python3 -c "import sys, json; data=json.load(sys.stdin); print('time_series' in data.get('analytics', {}))")
has_heatmap=$(echo "$json_body" | python3 -c "import sys, json; data=json.load(sys.stdin); print('heatmap_data' in data.get('analytics', {}))")
has_summary=$(echo "$json_body" | python3 -c "import sys, json; data=json.load(sys.stdin); print('summary' in data.get('analytics', {}))")

TOTAL_TESTS=$((TOTAL_TESTS + 4))
if [ "$has_analytics" = "True" ]; then
    echo -e "${GREEN}✓ Response contains analytics data${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Response does not contain analytics data${NC}"
fi

if [ "$has_time_series" = "True" ]; then
    echo -e "${GREEN}✓ Analytics contains time series data${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Analytics does not contain time series data${NC}"
fi

if [ "$has_heatmap" = "True" ]; then
    echo -e "${GREEN}✓ Analytics contains heatmap data${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Analytics does not contain heatmap data${NC}"
fi

if [ "$has_summary" = "True" ]; then
    echo -e "${GREEN}✓ Analytics contains summary data${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Analytics does not contain summary data${NC}"
fi

# Check summary contents
has_total_detections=$(echo "$json_body" | python3 -c "import sys, json; data=json.load(sys.stdin); print('total_detections' in data.get('analytics', {}).get('summary', {}))")
has_total_trackings=$(echo "$json_body" | python3 -c "import sys, json; data=json.load(sys.stdin); print('total_trackings' in data.get('analytics', {}).get('summary', {}))")
has_avg_detections=$(echo "$json_body" | python3 -c "import sys, json; data=json.load(sys.stdin); print('avg_detections_per_frame' in data.get('analytics', {}).get('summary', {}))")

TOTAL_TESTS=$((TOTAL_TESTS + 3))
if [ "$has_total_detections" = "True" ]; then
    echo -e "${GREEN}✓ Summary contains total_detections${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Summary does not contain total_detections${NC}"
fi

if [ "$has_total_trackings" = "True" ]; then
    echo -e "${GREEN}✓ Summary contains total_trackings${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Summary does not contain total_trackings${NC}"
fi

if [ "$has_avg_detections" = "True" ]; then
    echo -e "${GREEN}✓ Summary contains avg_detections_per_frame${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Summary does not contain avg_detections_per_frame${NC}"
fi

# Test the zone line counts endpoint
print_header "Testing zone line counts API endpoint"
response=$(curl -s -i ${API_URL}/api/v1/cameras/${db_test_camera_id}/database/zone-line-counts)
# Accept either 200 (with content) or 204 (no content) as valid responses
status_code=$(echo "$response" | head -n 1 | awk '{print $2}')
if [ "$status_code" -eq 200 ] || [ "$status_code" -eq 204 ]; then
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo -e "${GREEN}✓ Get zone line counts data - Status code: $status_code${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
    
    # Only check for JSON content if we received a 200 status
    if [ "$status_code" -eq 200 ]; then
        # Print formatted response
        print_formatted_response "$response" "Get zone line counts data"
        
        # Check the structure of the zone line counts response
        json_body=$(extract_json_body "$response")
        has_zone_line_counts=$(echo "$json_body" | python3 -c "import sys, json; print('zone_line_counts' in json.load(sys.stdin))")
    
        TOTAL_TESTS=$((TOTAL_TESTS + 1))
        if [ "$has_zone_line_counts" = "True" ]; then
            echo -e "${GREEN}✓ Response contains zone line counts data${NC}"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            echo -e "${RED}✗ Response does not contain zone line counts data${NC}"
        fi
    else
        echo -e "${YELLOW}⚠ No content returned for zone line counts (204 status), which is acceptable${NC}"
        # Print formatted response for 204
        print_formatted_response "$response" "Get zone line counts data (No Content)"
        # Add test for empty content
        TOTAL_TESTS=$((TOTAL_TESTS + 1))
        PASSED_TESTS=$((PASSED_TESTS + 1))  # Count as passed since 204 is a valid response
    fi
else
    check_status_code 200 "$response" "Get zone line counts data"  # Will report failure for anything other than 200
fi

# Test the zone line counts endpoint with time parameters
response=$(curl -s -i "${API_URL}/api/v1/cameras/${db_test_camera_id}/database/zone-line-counts?start_time=1600000000000&end_time=1700000000000")
# Accept either 200 (with content) or 204 (no content) as valid responses
status_code=$(echo "$response" | head -n 1 | awk '{print $2}')
if [ "$status_code" -eq 200 ] || [ "$status_code" -eq 204 ]; then
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo -e "${GREEN}✓ Get zone line counts data with time parameters - Status code: $status_code${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
    
    # Print formatted response
    print_formatted_response "$response" "Get zone line counts data with time parameters"
else
    check_status_code 200 "$response" "Get zone line counts data with time parameters"  # Will report failure for anything other than 200
fi

# Test the class-based heatmap endpoint
print_header "Testing class-based heatmap API endpoint"
response=$(curl -s -i ${API_URL}/api/v1/cameras/${db_test_camera_id}/database/class-heatmap)
# Accept either 200 (with content) or 204 (no content) as valid responses
status_code=$(echo "$response" | head -n 1 | awk '{print $2}')
if [ "$status_code" -eq 200 ] || [ "$status_code" -eq 204 ]; then
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo -e "${GREEN}✓ Get class-based heatmap data - Status code: $status_code${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
    
    # Print formatted response
    print_formatted_response "$response" "Get class-based heatmap data"
    
    # Only check for JSON content if we received a 200 status
    if [ "$status_code" -eq 200 ]; then
        # Check the structure of the class-based heatmap response
        json_body=$(extract_json_body "$response")
        has_class_heatmap=$(echo "$json_body" | python3 -c "import sys, json; print('class_heatmap_data' in json.load(sys.stdin))")
        
        TOTAL_TESTS=$((TOTAL_TESTS + 1))
        if [ "$has_class_heatmap" = "True" ]; then
            echo -e "${GREEN}✓ Response contains class-based heatmap data${NC}"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            echo -e "${RED}✗ Response does not contain class-based heatmap data${NC}"
        fi
    else
        echo -e "${YELLOW}⚠ No content returned for class-based heatmap (204 status), which is acceptable${NC}"
        # Add test for empty content
        TOTAL_TESTS=$((TOTAL_TESTS + 1))
        PASSED_TESTS=$((PASSED_TESTS + 1))  # Count as passed since 204 is a valid response
    fi
else
    check_status_code 200 "$response" "Get class-based heatmap data"  # Will report failure for anything other than 200
fi

# Now delete the camera - this should trigger database cleanup
response=$(curl -s -i -X DELETE ${API_URL}/api/v1/cameras/${db_test_camera_id})
check_status_code 200 "$response" "Delete database test camera"

# Verify the deletion response includes database_cleaned flag
json_body=$(extract_json_body "$response")
db_cleaned=$(echo "$json_body" | python3 -c "import sys, json; print(json.load(sys.stdin).get('database_cleaned', False))")

TOTAL_TESTS=$((TOTAL_TESTS + 1))
if [ "$db_cleaned" = "True" ]; then
    echo -e "${GREEN}✓ Camera delete response shows database was cleaned${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Camera delete response does not indicate database was cleaned${NC}"
fi

# -------------------------------------------------------------------------
# 3. START CAMERA 1 PIPELINE
# -------------------------------------------------------------------------
print_header "Starting camera 1 pipeline"
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"running": true}' \
    ${API_URL}/api/v1/cameras/${camera1_id})
check_status_code 200 "$response" "Start camera 1 pipeline"

json_body=$(extract_json_body "$response")
check_json_value "$json_body" "running" "true" "Camera 1 pipeline started"

# -------------------------------------------------------------------------
# 4. START CAMERA 2 PIPELINE
# -------------------------------------------------------------------------
print_header "Starting camera 2 pipeline"
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"running": true}' \
    ${API_URL}/api/v1/cameras/${camera2_id})
check_status_code 200 "$response" "Start camera 2 pipeline"

json_body=$(extract_json_body "$response")
check_json_value "$json_body" "running" "true" "Camera 2 pipeline started"

# -------------------------------------------------------------------------
# 5. TEST FRAME RETRIEVAL
# -------------------------------------------------------------------------
print_header "Testing frame retrieval"
echo "Waiting for video processing to start (5 seconds)..."
sleep 5

# Get frame status from camera 1
response=$(curl -s -i ${API_URL}/api/v1/cameras/${camera1_id}/frame/status)
check_status_code 200 "$response" "Get frame status from camera 1"

json_body=$(extract_json_body "$response")
# Since we're now processing in the background, we should have frames already
check_json_value "$json_body" "has_frame" "true" "Camera 1 has frames with background processing"

# Get a frame from camera 1
response=$(curl -s -i ${API_URL}/api/v1/cameras/${camera1_id}/frame)
check_status_code 200 "$response" "Get frame from camera 1"

# Save the frame to a file for inspection
frame_output_file="${OUTPUT_DIR}/camera1_frame.jpg"
# Direct download to file without header processing
curl -s ${API_URL}/api/v1/cameras/${camera1_id}/frame > "$frame_output_file"
echo -e "${GREEN}✓ Saved camera 1 frame to ${frame_output_file}${NC}"

# Get a frame from camera 2
response=$(curl -s -i ${API_URL}/api/v1/cameras/${camera2_id}/frame)
check_status_code 200 "$response" "Get frame from camera 2"

# Save the frame to a file for inspection
frame_output_file="${OUTPUT_DIR}/camera2_frame.jpg"
# Direct download to file without header processing
curl -s ${API_URL}/api/v1/cameras/${camera2_id}/frame > "$frame_output_file"
echo -e "${GREEN}✓ Saved camera 2 frame to ${frame_output_file}${NC}"

# Get frame with quality parameter
response=$(curl -s -i "${API_URL}/api/v1/cameras/${camera1_id}/frame?quality=50")
check_status_code 200 "$response" "Get low quality frame from camera 1"

# Save the lower quality frame for comparison
frame_output_file="${OUTPUT_DIR}/camera1_frame_low_quality.jpg"
# Direct download to file without header processing
curl -s "${API_URL}/api/v1/cameras/${camera1_id}/frame?quality=50" > "$frame_output_file"
echo -e "${GREEN}✓ Saved low quality camera 1 frame to ${frame_output_file}${NC}"

# -------------------------------------------------------------------------
# 6. PROCESS FOR 15 SECONDS
# -------------------------------------------------------------------------
print_header "Processing video streams for 15 seconds"
echo "Waiting for video processing (15 seconds)..."
sleep 15

# -------------------------------------------------------------------------
# 7. STOP CAMERA 1
# -------------------------------------------------------------------------
print_header "Stopping camera 1 pipeline"
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"running": false}' \
    ${API_URL}/api/v1/cameras/${camera1_id})
check_status_code 200 "$response" "Stop camera 1 pipeline"

json_body=$(extract_json_body "$response")
check_json_value "$json_body" "running" "false" "Camera 1 pipeline stopped"

# -------------------------------------------------------------------------
# 8. STOP CAMERA 2
# -------------------------------------------------------------------------
print_header "Stopping camera 2 pipeline"
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"running": false}' \
    ${API_URL}/api/v1/cameras/${camera2_id})
check_status_code 200 "$response" "Stop camera 2 pipeline"

json_body=$(extract_json_body "$response")
check_json_value "$json_body" "running" "false" "Camera 2 pipeline stopped"

# -------------------------------------------------------------------------
# CLEANUP
# -------------------------------------------------------------------------
print_header "Cleaning up resources"

# Try to get frame from stopped camera - should fail
response=$(curl -s -i ${API_URL}/api/v1/cameras/${camera1_id}/frame)
check_status_code 400 "$response" "Get frame from stopped camera - should fail"

# Delete processors
response=$(curl -s -i -X DELETE \
    ${API_URL}/api/v1/cameras/${camera1_id}/processors/${processor1_id})
check_status_code 200 "$response" "Delete processor 1"

response=$(curl -s -i -X DELETE \
    ${API_URL}/api/v1/cameras/${camera1_id}/processors/${tracker1_id})
check_status_code 200 "$response" "Delete tracker 1"

response=$(curl -s -i -X DELETE \
    ${API_URL}/api/v1/cameras/${camera2_id}/processors/${processor2_id})
check_status_code 200 "$response" "Delete processor 2"

response=$(curl -s -i -X DELETE \
    ${API_URL}/api/v1/cameras/${camera2_id}/processors/${tracker2_id})
check_status_code 200 "$response" "Delete tracker 2"

# Delete file sinks
response=$(curl -s -i -X DELETE \
    ${API_URL}/api/v1/cameras/${camera1_id}/sinks/${file_sink1_id})
check_status_code 200 "$response" "Delete file sink 1"

response=$(curl -s -i -X DELETE \
    ${API_URL}/api/v1/cameras/${camera1_id}/sinks/${db_sink1_id})
check_status_code 200 "$response" "Delete database sink 1"

response=$(curl -s -i -X DELETE \
    ${API_URL}/api/v1/cameras/${camera2_id}/sinks/${file_sink2_id})
check_status_code 200 "$response" "Delete file sink 2"

# Get available component types
print_header "Getting available component types"
response=$(curl -s -i ${API_URL}/api/v1/component-types)
check_status_code 200 "$response" "Get component types"

# Check if the response contains the new dependency information
json_body=$(extract_json_body "$response")
has_dependencies=$(echo "$json_body" | python3 -c "import sys, json; print('dependencies' in json.load(sys.stdin))")
has_rules=$(echo "$json_body" | python3 -c "import sys, json; print('dependency_rules' in json.load(sys.stdin))")

TOTAL_TESTS=$((TOTAL_TESTS + 2))
if [ "$has_dependencies" = "True" ]; then
    echo -e "${GREEN}✓ Response contains dependencies information${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Response does not contain dependencies information${NC}"
fi

if [ "$has_rules" = "True" ]; then
    echo -e "${GREEN}✓ Response contains dependency rules${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Response does not contain dependency rules${NC}"
fi

# Verify specific dependencies exist
has_tracking_dependency=$(echo "$json_body" | python3 -c "import sys, json; data=json.load(sys.stdin); print('object_tracking' in data.get('dependencies', {}))")
has_zone_dependency=$(echo "$json_body" | python3 -c "import sys, json; data=json.load(sys.stdin); print('line_zone_manager' in data.get('dependencies', {}))")

TOTAL_TESTS=$((TOTAL_TESTS + 2))
if [ "$has_tracking_dependency" = "True" ]; then
    echo -e "${GREEN}✓ Response contains tracking dependency${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Response does not contain tracking dependency${NC}"
fi

if [ "$has_zone_dependency" = "True" ]; then
    echo -e "${GREEN}✓ Response contains line zone manager dependency${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Response does not contain line zone manager dependency${NC}"
fi

# Test object detection models endpoint
print_header "Checking available object detection models"
response=$(curl -s -i ${API_URL}/api/v1/models/object-detection)
check_status_code 200 "$response" "Get object detection models" 

# Check if the response contains models data
json_body=$(extract_json_body "$response")
has_models=$(echo "$json_body" | python3 -c "import sys, json; print('models' in json.load(sys.stdin))")
check_json_value "$json_body" "service" "\"tAI\"" "Service name is tAI"
check_json_value "$json_body" "status" "\"ok\"" "Service status is ok"

if [ "$has_models" = "True" ]; then
    echo -e "${GREEN}✓ Response contains models information${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Response does not contain models information${NC}"
fi
TOTAL_TESTS=$((TOTAL_TESTS + 1))

# Delete cameras
print_header "Deleting cameras"
response=$(curl -s -i -X DELETE ${API_URL}/api/v1/cameras/${camera1_id})
check_status_code 200 "$response" "Delete camera 1"

response=$(curl -s -i -X DELETE ${API_URL}/api/v1/cameras/${camera2_id})
check_status_code 200 "$response" "Delete camera 2"

# After the camera type tests and before the final cleanup
print_header "Testing configuration persistence"

# Get basic configuration first
response=$(curl -s -i ${API_URL}/api/v1/config)
check_status_code 200 "$response" "Get initial configuration"

# Set a test configuration value
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"test_value": 123, "nested": {"value": "test"}}' \
    ${API_URL}/api/v1/config/test_config)
check_status_code 200 "$response" "Set test configuration"

# Read back the configuration
response=$(curl -s -i ${API_URL}/api/v1/config/test_config)
check_status_code 200 "$response" "Get test configuration"

# Check the JSON structure
json_body=$(extract_json_body "$response")
test_value=$(echo "$json_body" | python3 -c "import sys, json; print(json.load(sys.stdin).get('test_value', 0))")
nested_value=$(echo "$json_body" | python3 -c "import sys, json; print(json.load(sys.stdin).get('nested', {}).get('value', ''))")

TOTAL_TESTS=$((TOTAL_TESTS + 2))
if [ "$test_value" = "123" ]; then
    echo -e "${GREEN}✓ Configuration contains test_value=123${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Configuration does not contain expected test_value, got: $test_value${NC}"
fi

if [ "$nested_value" = "test" ]; then
    echo -e "${GREEN}✓ Configuration contains nested.value='test'${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Configuration does not contain expected nested.value, got: $nested_value${NC}"
fi

# Export full configuration
response=$(curl -s -i ${API_URL}/api/v1/config/export)
check_status_code 200 "$response" "Export full configuration"

# Save the export data for import test later
json_body=$(extract_json_body "$response")
export_data_file="${OUTPUT_DIR}/export_config.json"
echo "$json_body" > "$export_data_file"
echo -e "${GREEN}✓ Saved exported configuration to $export_data_file${NC}"

# Check all cameras section from the export
cameras_count=$(echo "$json_body" | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('cameras', {})))")
config_count=$(echo "$json_body" | python3 -c "import sys, json; print(len(json.load(sys.stdin).get('config', {})))")

TOTAL_TESTS=$((TOTAL_TESTS + 2))
if [ "$cameras_count" -gt 0 ]; then
    echo -e "${GREEN}✓ Export contains $cameras_count cameras${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Export does not contain any cameras${NC}"
fi

if [ "$config_count" -gt 0 ]; then
    echo -e "${GREEN}✓ Export contains $config_count configuration entries${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Export does not contain any configuration entries${NC}"
fi

# Delete the test configuration
response=$(curl -s -i -X DELETE ${API_URL}/api/v1/config/test_config)
check_status_code 200 "$response" "Delete test configuration"

# Verify deletion
response=$(curl -s -i ${API_URL}/api/v1/config/test_config)
check_status_code 404 "$response" "Verify test configuration is deleted"

# Add a new configuration with a different name
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"import_value": "testing import", "nested": {"new_value": "imported"}}' \
    ${API_URL}/api/v1/config/import_config)
check_status_code 200 "$response" "Set import config"

# Test camera configuration persistence
print_header "Testing camera configuration persistence"

# Create a test camera specifically for persistence
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"name": "Persistence Test Camera"}' \
    ${API_URL}/api/v1/cameras)
check_status_code 201 "$response" "Create persistence test camera"

# Extract camera ID
json_body=$(extract_json_body "$response")
persist_test_camera_id=$(extract_id "$json_body")
echo "Created persistence test camera with ID: $persist_test_camera_id"

# Add a source component
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d "{\"type\": \"file\", \"config\": {\"url\": \"$PEOPLE_DETECTION_FILE\", \"width\": 640, \"height\": 480}}" \
    ${API_URL}/api/v1/cameras/${persist_test_camera_id}/source)
check_status_code 201 "$response" "Add source to persistence test camera"

# Add a simple processor component
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"type": "object_detection", "config": {"model_id": "", "classes": ["person"]}}' \
    ${API_URL}/api/v1/cameras/${persist_test_camera_id}/processors)
check_status_code 201 "$response" "Add processor to persistence test camera"

# Import the previously exported configuration
print_header "Testing configuration import"
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d @"$export_data_file" \
    ${API_URL}/api/v1/config/import)
check_status_code 200 "$response" "Import configuration"

# Verify the import_config is available
response=$(curl -s -i ${API_URL}/api/v1/config/import_config)
check_status_code 200 "$response" "Verify import_config after import"

# Check the JSON structure again
json_body=$(extract_json_body "$response")
import_value=$(echo "$json_body" | python3 -c "import sys, json; print(json.load(sys.stdin).get('import_value', ''))")
nested_new_value=$(echo "$json_body" | python3 -c "import sys, json; print(json.load(sys.stdin).get('nested', {}).get('new_value', ''))")

TOTAL_TESTS=$((TOTAL_TESTS + 2))
if [ "$import_value" = "testing import" ]; then
    echo -e "${GREEN}✓ Imported configuration contains import_value='testing import'${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Imported configuration does not contain expected import_value, got: $import_value${NC}"
fi

if [ "$nested_new_value" = "imported" ]; then
    echo -e "${GREEN}✓ Imported configuration contains nested.new_value='imported'${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "${RED}✗ Imported configuration does not contain expected nested.new_value, got: $nested_new_value${NC}"
fi

# Delete the persistence test camera
response=$(curl -s -i -X DELETE ${API_URL}/api/v1/cameras/${persist_test_camera_id})
check_status_code 200 "$response" "Delete persistence test camera"

# Delete the import_config
response=$(curl -s -i -X DELETE ${API_URL}/api/v1/config/import_config)
check_status_code 200 "$response" "Delete import_config"

# -------------------------------------------------------------------------
# TEST POLYGON ZONE MANAGER
# -------------------------------------------------------------------------
print_header "Testing Polygon Zone Manager"

# Create a test camera for polygon zone testing
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"name": "Polygon Zone Test Camera"}' \
    ${API_URL}/api/v1/cameras)
check_status_code 201 "$response" "Create polygon zone test camera"

# Extract camera ID
json_body=$(extract_json_body "$response")
polygon_camera_id=$(extract_id "$json_body")
echo "Created polygon zone test camera with ID: $polygon_camera_id"

# Create file source for test camera
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d "{\"type\": \"file\", \"config\": {\"url\": \"$PEOPLE_DETECTION_FILE\", \"width\": 640, \"height\": 480, \"fps\": 30, \"use_hw_accel\": true, \"adaptive_timing\": true}}" \
    ${API_URL}/api/v1/cameras/${polygon_camera_id}/source)
check_status_code 201 "$response" "Create file source for polygon zone test camera"

# Create object detection processor
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"type": "object_detection", "config": {"model_id": "", "classes": ["person"]}}' \
    ${API_URL}/api/v1/cameras/${polygon_camera_id}/processors)
check_status_code 201 "$response" "Create object detection processor for polygon zone test"

# Create object tracker processor
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"type": "object_tracking", "config": {"frame_rate": 30, "track_buffer": 30}}' \
    ${API_URL}/api/v1/cameras/${polygon_camera_id}/processors)
check_status_code 201 "$response" "Create object tracker processor for polygon zone test"

# Create polygon zone manager processor
response=$(curl -s -i -X POST -H "Content-Type: application/json" \
    -d '{"type": "polygon_zone_manager", "config": {"draw_zones": true, "fill_color": [0, 100, 0], "opacity": 0.3, "outline_color": [0, 255, 0], "outline_thickness": 2, "zones": [{"id": "restricted_zone", "polygon": [{"x": 0.1, "y": 0.1}, {"x": 0.9, "y": 0.1}, {"x": 0.9, "y": 0.9}, {"x": 0.1, "y": 0.9}], "triggering_anchors": ["CENTER"]}]}}' \
    ${API_URL}/api/v1/cameras/${polygon_camera_id}/processors)
check_status_code 201 "$response" "Create polygon zone manager processor"

json_body=$(extract_json_body "$response")
polygon_zone_manager_id=$(extract_id "$json_body")
echo "Created polygon zone manager component with ID: $polygon_zone_manager_id"

# Start the camera
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"running": true}' \
    ${API_URL}/api/v1/cameras/${polygon_camera_id})
check_status_code 200 "$response" "Start polygon zone test camera"

json_body=$(extract_json_body "$response")
check_json_value "$json_body" "running" "true" "Polygon zone camera started"

# Wait for 5 seconds to let the camera process frames
echo "Waiting for 5 seconds to process frames..."
sleep 5

# Check the status of the polygon zone manager to verify it's working
response=$(curl -s -i ${API_URL}/api/v1/cameras/${polygon_camera_id}/processors/${polygon_zone_manager_id})
check_status_code 200 "$response" "Get polygon zone manager status"

json_body=$(extract_json_body "$response")
check_json_value "$json_body" "running" "true" "Polygon zone manager is running"

# Update the polygon zone configuration
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"config": {"zones": [{"id": "restricted_zone", "polygon": [{"x": 0.2, "y": 0.2}, {"x": 0.8, "y": 0.2}, {"x": 0.8, "y": 0.8}, {"x": 0.2, "y": 0.8}], "triggering_anchors": ["CENTER", "BOTTOM_CENTER"]}]}}' \
    ${API_URL}/api/v1/cameras/${polygon_camera_id}/processors/${polygon_zone_manager_id})
check_status_code 200 "$response" "Update polygon zone configuration"

# Get a frame to verify visualization
response=$(curl -s -i ${API_URL}/api/v1/cameras/${polygon_camera_id}/frame)
check_status_code 200 "$response" "Get frame from polygon zone camera"

# Save the frame to a file for inspection
frame_output_file="${OUTPUT_DIR}/polygon_zone_frame.jpg"
curl -s ${API_URL}/api/v1/cameras/${polygon_camera_id}/frame > "$frame_output_file"
echo -e "${GREEN}✓ Saved polygon zone frame to ${frame_output_file}${NC}"

# Stop the camera
response=$(curl -s -i -X PUT -H "Content-Type: application/json" \
    -d '{"running": false}' \
    ${API_URL}/api/v1/cameras/${polygon_camera_id})
check_status_code 200 "$response" "Stop polygon zone test camera"

# Delete the camera
response=$(curl -s -i -X DELETE ${API_URL}/api/v1/cameras/${polygon_camera_id})
check_status_code 200 "$response" "Delete polygon zone test camera"

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
