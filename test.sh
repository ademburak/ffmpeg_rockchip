#!/bin/bash

# Camera map - add your cameras here
declare -A CAMERAS=(
    ["burak_high"]="rtsp://admin:kuz60TOL1234@10.126.200.233:8554/Streaming/Channels/101"
    ["burak_low"]="rtsp://admin:kuz60TOL1234@10.126.200.233:8554/Streaming/Channels/102"
    ["outdoor_high"]="rtsp://admin:Admin123!@192.168.215.109:554/Streaming/Channels/101"
    ["outdoor_low"]="rtsp://admin:Admin123!@192.168.215.109:554/Streaming/Channels/102"
    # Add more cameras as needed
)

# Function to print usage
print_usage() {
    echo "Usage:"
    echo "  $0 <camera_name> [options]"
    echo "  $0 <rtsp_url> [options]"
    echo ""
    echo "Available cameras:"
    for cam in "${!CAMERAS[@]}"; do
        echo "  $cam"
    done
    echo ""
    echo "Options:"
    echo "  --no-resize    Disable frame resizing"
    echo "  --no-record    Disable video recording"
    echo "  <output.mp4>   Specify output file (default: output.mp4)"
}

# Check if any arguments are provided
if [ $# -eq 0 ]; then
    print_usage
    exit 1
fi

# Get the first argument (camera name or RTSP URL)
CAMERA=$1
shift  # Remove the first argument

# Check if the camera name exists in our map
if [[ -n "${CAMERAS[$CAMERA]}" ]]; then
    # Use the mapped RTSP URL
    RTSP_URL="${CAMERAS[$CAMERA]}"
    echo "Using camera: $CAMERA"
    echo "RTSP URL: $RTSP_URL"
else
    # Assume it's a direct RTSP URL
    RTSP_URL="$CAMERA"
    echo "Using direct RTSP URL: $RTSP_URL"
fi

# Output file in /tmp directory
OUTPUT_FILE="/tmp/output.mp4"
echo "Output file: $OUTPUT_FILE"

# Run the player with frame resizing
echo "Running with frame resizing (800x600)"
./rtsp_player "$RTSP_URL" "$OUTPUT_FILE"