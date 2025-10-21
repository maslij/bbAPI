#!/bin/bash

cd "$(dirname "$0")/.."
g++ -o test_tai_connection test_tai_connection.cpp -lcurl
echo "Build completed."
echo "Running test..."
./test_tai_connection 