#!/bin/bash
set -e

# Build the Docker image
echo "Building debug tAPI Docker image..."
docker build -t bbapi-jetson-debug .

# Tag the image for ECR
echo "Tagging image for ECR..."
docker tag bbapi-jetson-debug 246261010633.dkr.ecr.ap-southeast-2.amazonaws.com/bbapi-jetson-debug

# Push to ECR
echo "Pushing image to ECR..."
docker push 246261010633.dkr.ecr.ap-southeast-2.amazonaws.com/bbapi-jetson-debug

echo "Debug image built and pushed successfully!" 