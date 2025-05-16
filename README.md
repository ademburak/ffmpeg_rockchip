# RTSP Player with Hardware Acceleration

This is a high-performance RTSP player that uses Rockchip hardware acceleration for video decoding and OpenCV for image processing.

## Compilation

### Prerequisites
- FFmpeg with Rockchip MPP support
- OpenCV 4
- pkg-config
- C++ compiler with C++11 support

### Build Steps
```bash
# Compile the program
g++ rtsp_player.cpp -o rtsp_player `pkg-config --cflags --libs opencv4 libavformat libavcodec libavutil libswscale`
```

This command:
- Compiles `rtsp_player.cpp` into the `rtsp_player` executable
- Uses pkg-config to automatically include the correct compiler flags and libraries for:
  - OpenCV 4
  - FFmpeg libraries (libavformat, libavcodec, libavutil, libswscale)

## Usage

The program can be run using the `test.sh` script which provides a convenient way to access different cameras and configure options.

### Basic Usage
```bash
./test.sh <camera_name> [options]
```

### Available Cameras
- `burak_high`: High resolution stream from Burak camera
- `burak_low`: Low resolution stream from Burak camera
- `burak_high_2`: Second high resolution stream from Burak camera
- `burak_low_2`: Second low resolution stream from Burak camera
- `burak_high_3`: Third high resolution stream from Burak camera
- `burak_low_3`: Third low resolution stream from Burak camera
- `burak_high_4`: Fourth high resolution stream from Burak camera
- `burak_low_4`: Fourth low resolution stream from Burak camera

### Command Line Options

#### Color Format
- `--color-format=bgr`: Use BGR color format (default, compatible with OpenCV)
- `--color-format=yuv`: Use original YUV format (fastest, no conversion)
- `--color-format=nv12`: Use NV12 format (hardware-friendly)

#### Resizing
- `--no-resize`: Disable frame resizing (use original resolution)
- Without `--no-resize`: Frames are resized to 800x600

#### Recording
- `--no-record`: Disable video recording
- Without `--no-record`: Video is recorded to output file

### Examples

1. Play high resolution stream with default settings (BGR format, resized):
```bash
./test.sh burak_high
```

2. Play high resolution stream without resizing:
```bash
./test.sh burak_high --no-resize
```

3. Play high resolution stream in YUV format (fastest):
```bash
./test.sh burak_high --color-format=yuv
```

4. Play high resolution stream without recording:
```bash
./test.sh burak_high --no-record
```

5. Play high resolution stream with all options:
```bash
./test.sh burak_high --no-resize --color-format=yuv --no-record
```

### Performance Notes

- YUV format is fastest as it requires no color conversion
- BGR format is compatible with OpenCV but requires conversion
- No-resize mode is faster as it skips the resizing step
- Hardware acceleration is automatically used when available

### Output

The program displays real-time statistics including:
- Number of frames processed
- CPU usage
- Current FPS
- Average conversion time (in milliseconds)

### Output File

When recording is enabled (default), the output is saved to `/tmp/output.mp4` by default. The output file path can be changed by providing it as the last argument:

```bash
./test.sh burak_high /path/to/output.mp4
```
