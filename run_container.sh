#!/bin/bash

# Build the Docker image
docker build -t rtsp_player .

# Create output directory if it doesn't exist
mkdir -p output

# Check if devices exist and are device nodes
DEVICE_OPTS=""
for dev in /dev/mpp_service /dev/rga /dev/dri/card0 /dev/dri/renderD128; do
    if [ -e "$dev" ] && [ -c "$dev" ]; then
        DEVICE_OPTS="$DEVICE_OPTS --device=$dev"
    else
        echo "Warning: Device $dev not found or not a character device, skipping..."
    fi
done

# Run the container with necessary device access
docker run -it --rm \
    $DEVICE_OPTS \
    --group-add=video \
    --cap-add=SYS_NICE \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v /usr/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu \
    -v /proc/device-tree:/proc/device-tree \
    -v "$(pwd)/output:/tmp" \
    -e DISPLAY=$DISPLAY \
    -e MPP_DEBUG=1 \
    -e MPP_LOG_LEVEL=1 \
    -e MPP_USE_DRM=1 \
    -e MPP_USE_DRM_DISPLAY=1 \
    -e MPP_USE_DRM_RENDER=1 \
    -e MPP_DEC_FORCE_SOFTWARE=0 \
    -e MPP_DEC_FORCE_HARDWARE=1 \
    rtsp_player "$@" 