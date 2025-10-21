#!/bin/bash

# Test script for object detection functionality

HOST="http://localhost:8080"

# Check if tAPI server is running
echo "Checking if tAPI server is running..."
if ! curl -s "$HOST" > /dev/null; then
  echo "Error: tAPI server is not running. Please start it with ./build/tAPI"
  exit 1
fi

# 1. Check available models
echo "Checking available object detection models..."
curl -s "$HOST/api/vision/models" | jq .

# 2. Create a camera stream (using device 0)
echo -e "\nCreating camera stream..."
STREAM_RESPONSE=$(curl -s -X POST "$HOST/api/streams" \
  -H "Content-Type: application/json" \
  -d '{"name":"Test Camera","source":"0","type":"camera","autoStart":true}')

STREAM_ID=$(echo $STREAM_RESPONSE | jq -r '.id')
echo "Created stream with ID: $STREAM_ID"

# 3. Create a pipeline with object detection
echo -e "\nCreating detection pipeline..."
PIPELINE_RESPONSE=$(curl -s -X POST "$HOST/api/streams/$STREAM_ID/pipelines" \
  -H "Content-Type: application/json" \
  -d '{
    "name":"Object Detection Pipeline",
    "nodes":[
      {
        "id":"src1",
        "componentId":"camera_feed",
        "position":{"x":100,"y":100},
        "connections":["det1"],
        "config":{}
      },
      {
        "id":"det1",
        "componentId":"object_detector",
        "position":{"x":300,"y":100},
        "connections":["out1"],
        "config":{
          "model":"yolov3",
          "confidence":0.4
        }
      },
      {
        "id":"out1",
        "componentId":"annotated_stream",
        "position":{"x":500,"y":100},
        "connections":[],
        "config":{
          "show_title":true,
          "show_timestamp":true
        }
      }
    ]
  }')

PIPELINE_ID=$(echo $PIPELINE_RESPONSE | jq -r '.id')
echo "Created pipeline with ID: $PIPELINE_ID"

# 4. Activate the pipeline
echo -e "\nActivating pipeline..."
curl -s -X POST "$HOST/api/streams/$STREAM_ID/pipelines/$PIPELINE_ID/activate"

echo -e "\nObject detection pipeline is now active."
echo "View annotated stream at: $HOST/api/streams/$STREAM_ID/annotated_frame"
echo "View UI at: $HOST/api/streams/$STREAM_ID/embed"

# Keep script running to maintain session
echo -e "\nPress Ctrl+C to stop the test..."
while true; do
  sleep 1
done 