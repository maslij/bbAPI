#!/bin/bash

# Script to download object detection models for tAPI

MODELS_DIR="$(pwd)/models"
mkdir -p "$MODELS_DIR"

echo "Downloading models to $MODELS_DIR"

# Download YOLOv3 weights and cfg
echo "Downloading YOLOv3..."
wget -O "$MODELS_DIR/yolov3.weights" https://pjreddie.com/media/files/yolov3.weights
wget -O "$MODELS_DIR/yolov3.cfg" https://raw.githubusercontent.com/pjreddie/darknet/master/cfg/yolov3.cfg
wget -O "$MODELS_DIR/coco.names" https://raw.githubusercontent.com/pjreddie/darknet/master/data/coco.names

# Download YOLOv4 weights and cfg
echo "Downloading YOLOv4..."
wget -O "$MODELS_DIR/yolov4.weights" https://github.com/AlexeyAB/darknet/releases/download/darknet_yolo_v3_optimal/yolov4.weights
wget -O "$MODELS_DIR/yolov4.cfg" https://raw.githubusercontent.com/AlexeyAB/darknet/master/cfg/yolov4.cfg

echo "All models downloaded."
echo "Available models:"
ls -la "$MODELS_DIR" 