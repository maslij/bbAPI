#!/bin/bash
set -e

echo "Setting up Docker Buildx for multi-architecture builds..."

# Create or use existing builder instance
docker buildx inspect tapi-builder >/dev/null 2>&1 || docker buildx create --name tapi-builder --platform linux/arm64
docker buildx use tapi-builder
docker buildx inspect --bootstrap

# Define image name and tag
IMAGE_NAME="tapi"
IMAGE_TAG="latest"
FULL_IMAGE_NAME="${IMAGE_NAME}:${IMAGE_TAG}"

# Build the image for arm64 platform
echo "Building Docker image for ARM64 (NVIDIA Jetson)..."
docker buildx build --platform linux/arm64 \
  --tag ${FULL_IMAGE_NAME} \
  --load \
  .

echo "Build completed successfully!"
echo "Image: ${FULL_IMAGE_NAME}"
echo ""
echo "To push this image to ECR, use:"
echo "1. Tag the image:"
echo "   docker tag ${FULL_IMAGE_NAME} 246261010633.dkr.ecr.ap-southeast-2.amazonaws.com/tapi:${IMAGE_TAG}"
echo ""
echo "2. Push the image:"
echo "   docker push 246261010633.dkr.ecr.ap-southeast-2.amazonaws.com/tapi:${IMAGE_TAG}"
echo ""
echo "To run the container locally:"
echo "   docker run --rm -it --gpus all -p 8080:8080 ${FULL_IMAGE_NAME}"
echo ""
echo "Or use docker-compose:"
echo "   docker-compose up -d" 