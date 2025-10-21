#!/bin/bash
set -e

# Parse command line arguments
L4T_VERSION="35.1.0"  # Default version
while [[ $# -gt 0 ]]; do
  case $1 in
    --version)
      L4T_VERSION="$2"
      shift 2
      ;;
    -v)
      L4T_VERSION="$2"
      shift 2
      ;;
    *)
      echo "Unknown option $1"
      echo "Usage: $0 [--version|-v <L4T_VERSION>]"
      echo "Example: $0 --version 32.6.1"
      exit 1
      ;;
  esac
done

# Configuration
AWS_REGION="ap-southeast-2"
ECR_REGISTRY="246261010633.dkr.ecr.ap-southeast-2.amazonaws.com"
ECR_REPOSITORY="bbapi-jetson"
IMAGE_TAG=$(date +%Y%m%d-%H%M%S)

echo "Building and deploying tAPI ARM64 image to ECR..."
echo "L4T Version: ${L4T_VERSION}"
echo "Repository: ${ECR_REGISTRY}/${ECR_REPOSITORY}"
echo "Tag: ${IMAGE_TAG}"

# Authenticate Docker to ECR
echo "Authenticating with AWS ECR..."
aws ecr get-login-password --region ${AWS_REGION} | docker login --username AWS --password-stdin ${ECR_REGISTRY}

# Check if repository exists, create if not
aws ecr describe-repositories --repository-names ${ECR_REPOSITORY} --region ${AWS_REGION} > /dev/null 2>&1 || \
  aws ecr create-repository --repository-name ${ECR_REPOSITORY} --region ${AWS_REGION}

# Set up Docker Buildx for multi-architecture builds
echo "Setting up Docker Buildx..."
docker buildx inspect tapi-builder >/dev/null 2>&1 || docker buildx create --name tapi-builder --platform linux/arm64
docker buildx use tapi-builder
docker buildx inspect --bootstrap

# Build and push the image
echo "Building and pushing Docker image for ARM64 (NVIDIA Jetson)..."
docker buildx build --platform linux/arm64 \
  --build-arg L4T_VERSION=${L4T_VERSION} \
  --tag ${ECR_REGISTRY}/${ECR_REPOSITORY}:${IMAGE_TAG} \
  --tag ${ECR_REGISTRY}/${ECR_REPOSITORY}:latest \
  --push \
  .

echo "Deployment completed successfully!"
echo "Image: ${ECR_REGISTRY}/${ECR_REPOSITORY}:${IMAGE_TAG}"
echo "Image: ${ECR_REGISTRY}/${ECR_REPOSITORY}:latest"
echo ""
echo "To pull this image on your Jetson device:"
echo "1. Authenticate with ECR:"
echo "   aws ecr get-login-password --region ${AWS_REGION} | docker login --username AWS --password-stdin ${ECR_REGISTRY}"
echo ""
echo "2. Pull the image:"
echo "   docker pull ${ECR_REGISTRY}/${ECR_REPOSITORY}:latest"
echo ""
echo "3. Run the container:"
echo "   docker run --rm -it --gpus all -p 8080:8080 ${ECR_REGISTRY}/${ECR_REPOSITORY}:latest" 