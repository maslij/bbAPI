#!/bin/bash

cd "$(dirname "$0")/.."

# Check if we have a test image, otherwise use a sample
if [ ! -f "test.jpg" ]; then
    echo "No test.jpg found, looking for sample images..."
    # Look for an image in the samples directory
    SAMPLE_IMAGE=$(find . -name "*.jpg" -o -name "*.png" | head -1)
    if [ -z "$SAMPLE_IMAGE" ]; then
        echo "No sample image found. Creating a simple test image..."
        # Create a simple test image
        convert -size 100x100 xc:white test.jpg
    else
        echo "Using sample image: $SAMPLE_IMAGE"
        export TEST_IMAGE="$SAMPLE_IMAGE"
    fi
fi

g++ -o test_tai_detect test_tai_detect.cpp -lcurl
echo "Build completed."
echo "Running detect test..."
./test_tai_detect 