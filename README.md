# cvffmpeg

Color-correct video I/O for OpenCV. A drop-in replacement for `cv::VideoCapture` and `cv::VideoWriter` that gives you explicit control over color space conversion.

## The problem

OpenCV's `VideoCapture` silently picks a YUV-to-RGB color matrix and gives you no way to override it. For most applications this doesn't matter, but if you're doing computational photography, color calibration, HDR processing, or anything where pixel values need to be *correct*, you'll get subtly wrong results.

Common issues:
- **Wrong color matrix**: OpenCV may use BT.601 for HD content that should be BT.709, shifting colors
- **Wrong range**: Limited range (16-235) treated as full range (0-255) crushes blacks and clips highlights
- **No HDR support**: `cv::VideoWriter` can't produce HDR10 HEVC with proper BT.2020/PQ metadata
- **No lossless intermediates**: No access to FFV1, H.264 qp=0, or 4:4:4 chroma for zero-loss workflows

## What cvffmpeg provides

- **Explicit color matrix control**: Force BT.709, BT.601, or auto-detect with sensible defaults (BT.709 for HD, BT.601 for SD)
- **Full/limited range override**: Choose whether your source is full range (0-255) or limited (16-235)
- **HDR10 writing**: HEVC 10-bit with BT.2020, PQ transfer, mastering display metadata, and content light level — plays correctly in QuickTime/Safari
- **Lossless encoding**: FFV1 (RGB, zero YUV conversion), H.264 qp=0 with 4:4:4 chroma
- **10-bit SDR**: High-precision intermediates using HEVC Main10 with BT.709
- **Same API convention**: Returns BGR `cv::Mat` frames, just like OpenCV

## Quick start

```cpp
#include <cvffmpeg/VideoReader.h>
#include <cvffmpeg/VideoWriter.h>

// Read with correct BT.709 color conversion
cvffmpeg::VideoReader reader;
reader.open("input.mp4", /*force_bt709=*/true);

cv::Mat frame;
while (reader.read(frame)) {
    // frame is BGR cv::Mat with correct pixel values
}

// Write HDR10 HEVC
cvffmpeg::VideoWriterOptions opts;
opts.pix_fmt = AV_PIX_FMT_YUV420P10LE;
opts.is_10bit = true;

cvffmpeg::VideoWriter writer;
writer.open("output.mp4", AV_CODEC_ID_HEVC, 3840, 2160,
            {60000, 1001}, opts);  // 59.94 fps
writer.write(hdr_frame);  // CV_16UC3 BGR
writer.release();
```

## Building

### As a dependency (CMake FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(
    cvffmpeg
    GIT_REPOSITORY https://github.com/kmatzen/cvffmpeg.git
    GIT_TAG main
)
FetchContent_MakeAvailable(cvffmpeg)

target_link_libraries(your_target PRIVATE cvffmpeg::cvffmpeg)
```

### Standalone

```bash
mkdir build && cd build
cmake .. -DCVFFMPEG_BUILD_EXAMPLES=ON
make -j$(nproc)
```

### Requirements

- C++17 compiler
- OpenCV 4.0+
- FFmpeg 4.2+ (libavformat, libavcodec, libswscale, libavutil)

## Logging

By default, cvffmpeg only logs errors. To see informational output (codec details, color space decisions):

```cpp
#include <cvffmpeg/LogLevel.h>

cvffmpeg::setLogLevel(cvffmpeg::LogLevel::Info);     // See everything
cvffmpeg::setLogLevel(cvffmpeg::LogLevel::Warning);  // Errors + warnings
cvffmpeg::setLogLevel(cvffmpeg::LogLevel::Error);    // Errors only (default)
cvffmpeg::setLogLevel(cvffmpeg::LogLevel::Quiet);    // Silence all output
```

## API

### `cvffmpeg::VideoReader`

| Method | Description |
|--------|-------------|
| `open(filename, force_bt709, force_full_range)` | Open a video file with optional color space overrides |
| `read(frame)` | Read the next frame as BGR `cv::Mat` (cloned, always safe) |
| `readRef(frame)` | Read the next frame without copying (valid until next read) |
| `seek(frame_number)` | Seek forward to a frame (forward-only) |
| `getColorSpace()` | Get the file's declared color space |
| `getColorRange()` | Get the file's declared range |
| `getFPS()`, `getWidth()`, `getHeight()` | Video properties |

### `cvffmpeg::VideoWriter`

| Method | Description |
|--------|-------------|
| `open(filename, codec_id, w, h, fps, opts)` | Open output with full codec control |
| `write(frame)` | Write a BGR frame (CV_8UC3 or CV_16UC3) |
| `release()` | Flush and finalize the file |

### `cvffmpeg::VideoWriterOptions`

| Field | Default | Description |
|-------|---------|-------------|
| `bitrate` | 25000000 | Target bitrate in bits/sec |
| `pix_fmt` | YUV420P | Output pixel format |
| `is_10bit` | false | HDR10 mode (BT.2020 + PQ) |
| `full_range` | false | Full range (0-255) vs limited (16-235) |
| `use_444` | false | 4:4:4 chroma (no subsampling) |
| `lossless` | false | Mathematically lossless encoding |

Supported codecs: H.264, H.265/HEVC (8-bit and 10-bit), FFV1 (lossless RGB).

## License

MIT
