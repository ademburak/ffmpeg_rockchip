# RTSP Player with Hardware Acceleration

This is a high-performance RTSP player that uses Rockchip hardware acceleration for video decoding and OpenCV for image processing.

## Compilation

### Prerequisites
- FFmpeg with Rockchip MPP support
- OpenCV 4
- pkg-config
- C++ compiler with C++11 support
- Rockchip MPP library

### Build Steps
```bash
# Compile the program
g++ rtsp_player.cpp -o rtsp_player `pkg-config --cflags --libs opencv4 libavformat libavcodec libavutil libswscale` -lrockchip_mpp
```

This command:
- Compiles `rtsp_player.cpp` into the `rtsp_player` executable
- Uses pkg-config to automatically include the correct compiler flags and libraries for:
  - OpenCV 4
  - FFmpeg libraries (libavformat, libavcodec, libavutil, libswscale)
- Links against the Rockchip MPP library (-lrockchip_mpp)

## Usage

The program can be run using the `