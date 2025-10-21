#!/bin/bash
set -e

# Configuration
API_URL="http://localhost:8090"

# Define the license API test function
test_license_api() {
  echo "Testing license API..."
  
  # Test invalid license
  echo "Testing invalid license..."
  response=$(curl -s -X POST -H "Content-Type: application/json" \
      -d '{"license_key": "INVALID-LICENSE-123"}' \
      ${API_URL}/api/v1/license)
  
  # The response should have valid=false
  if echo $response | grep -q '"valid":false'; then
    echo "✅ Invalid license test passed"
  else
    echo "❌ Invalid license test failed"
    echo "Response: $response"
    return 1
  fi
  
  # Test basic license
  echo "Testing basic license..."
  response=$(curl -s -X POST -H "Content-Type: application/json" \
      -d '{"license_key": "BASIC-LICENSE-KEY-123", "owner": "Test User", "email": "test@example.com"}' \
      ${API_URL}/api/v1/license)
  
  # The response should have valid=true and tier=basic
  if echo $response | grep -q '"valid":true' && echo $response | grep -q '"tier":"basic"'; then
    echo "✅ Basic license test passed"
  else
    echo "❌ Basic license test failed"
    echo "Response: $response"
    return 1
  fi
  
  # Test license update
  echo "Testing license update..."
  response=$(curl -s -X PUT -H "Content-Type: application/json" \
      -d '{"owner": "Updated User", "email": "updated@example.com"}' \
      ${API_URL}/api/v1/license)
  
  # The response should have owner=Updated User
  if echo $response | grep -q '"owner":"Updated User"'; then
    echo "✅ License update test passed"
  else
    echo "❌ License update test failed"
    echo "Response: $response"
    return 1
  fi
  
  # Delete the license
  echo "Testing license deletion..."
  response=$(curl -s -X DELETE ${API_URL}/api/v1/license)
  
  # The response should have success=true
  if echo $response | grep -q '"success":true'; then
    echo "✅ License deletion test passed"
  else
    echo "❌ License deletion test failed"
    echo "Response: $response"
    return 1
  fi
  
  # Test professional license
  echo "Testing professional license..."
  response=$(curl -s -X POST -H "Content-Type: application/json" \
      -d '{"license_key": "PRO-LICENSE-KEY-789"}' \
      ${API_URL}/api/v1/license)
  
  # The response should have valid=true and tier=professional
  if echo $response | grep -q '"valid":true' && echo $response | grep -q '"tier":"professional"'; then
    echo "✅ Professional license test passed"
  else
    echo "❌ Professional license test failed"
    echo "Response: $response"
    return 1
  fi
  
  echo "All license API tests passed!"
  return 0
}

# Try to run the tests
test_license_api 