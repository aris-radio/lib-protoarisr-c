#!/bin/bash

# Move to the project root directory
cd "$(dirname "$0")/.."

# Set variables
DOCKER_IMAGE_NAME="libarisr-valgrind"
DOCKERFILE_DIR="./docker"

# Remove the existing Docker image
echo "Removing existing Docker image: $DOCKER_IMAGE_NAME..."
docker rmi -f $DOCKER_IMAGE_NAME

# Build the Docker image
echo "Building Docker image: $DOCKER_IMAGE_NAME..."
docker build -t $DOCKER_IMAGE_NAME -f $DOCKERFILE_DIR/Dockerfile ./docker

if [ $? -ne 0 ]; then
    echo "Failed to build the Docker image."
    exit 1
fi

# Run the container
echo "Running Valgrind inside Docker..."
# Volume mount
docker run -it --rm --name $DOCKER_IMAGE_NAME -v $(pwd):/app $DOCKER_IMAGE_NAME
