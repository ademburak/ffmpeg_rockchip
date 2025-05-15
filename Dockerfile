FROM ubuntu:22.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install only runtime dependencies
RUN apt-get update && apt-get install -y \
    libavcodec58 \
    libavformat58 \
    libavutil56 \
    libswscale5 \
    libx264-163 \
    libx265-199 \
    libopencv-core4.5d \
    libopencv-imgproc4.5d \
    libopencv-highgui4.5d \
    && rm -rf /var/lib/apt/lists/*

# Create application directory
WORKDIR /app

# Copy the pre-built binary and script
COPY rtsp_player .
COPY test.sh .

# Make script executable
RUN chmod +x test.sh

# Set environment variables for RKMPP
ENV LIBVA_DRIVER_NAME=rkmpp
ENV MPP_DEBUG=0
ENV MPP_LOG_LEVEL=0
ENV MPP_USE_DRM=1
ENV MPP_USE_DRM_DISPLAY=1
ENV MPP_USE_DRM_RENDER=1

# Keep container running for inspection
CMD ["/bin/bash"] 