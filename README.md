# framewright

A drop-in replacement for `cv::VideoCapture` and `cv::VideoWriter` that gives you frame-precise seeking, color space control, HDR10 writing, and lossless encoding.

Available for **C++**, **Python**, and **JavaScript** — Node.js and browsers via WebAssembly (see [wasm/README.md](wasm/README.md)).

## What framewright adds over OpenCV

### Reading

| Feature | OpenCV | framewright |
|---------|--------|----------|
| Frame-precise seeking | Forward only, not frame-accurate | Forward and backward, keyframe-accelerated |
| Color space control | Opaque, implementation-defined | Explicit BT.709/BT.601 selection, full/limited range override |
| Color metadata | Not exposed | `getColorSpace()`, `getColorRange()`, `getPixelFormat()`, `getCodecID()` |
| Zero-copy frames | No | `readRef()` avoids per-frame clone |
| Untagged content | Implementation-defined matrix | Sensible defaults (BT.709 for HD, BT.601 for SD) |
| HDR sources (PQ/HLG) | Washed-out colors, no warning | Opt-in tone mapping to SDR, or full-precision code values via `read16()` |

### Writing

| Feature | OpenCV | framewright |
|---------|--------|----------|
| HDR10 output | Not supported | HEVC 10-bit with BT.2020, PQ, mastering display metadata |
| Lossless encoding | Not supported | FFV1 (RGB, no YUV loss), H.264 qp=0 with 4:4:4 |
| 10-bit SDR | Not supported | HEVC Main10 with BT.709 for high-precision intermediates |
| Color space control | Opaque | Explicit BT.709/BT.2020, full/limited range |
| QuickTime compatibility | Varies | hvc1 codec tag for Safari/QuickTime HDR playback |

### Verified accuracy

Measured by reading BT.709-tagged lossless test videos with both libraries (OpenCV 4.13.0 and framewright on macOS). Both produce identical pixel values when metadata is present — framewright adds control, not a different answer:

```bash
cmake -B build -DFRAMEWRIGHT_BUILD_EXAMPLES=ON && cmake --build build
./build/compare_readers your_video.mp4
```

## Migrating from OpenCV

framewright is a drop-in replacement. The API follows the same conventions (BGR `cv::Mat` frames):

### Reading

```cpp
// OpenCV
cv::VideoCapture cap("video.mp4");
cv::Mat frame;
cap.read(frame);

// framewright — same API, more capabilities
framewright::VideoReader reader;
reader.open("video.mp4");
cv::Mat frame;
reader.read(frame);

// seek backward (not possible with cv::VideoCapture)
reader.seek(0);
reader.read(frame);

// inspect color metadata
std::cout << "Color space: " << reader.getColorSpace() << std::endl;
std::cout << "Pixel format: " << av_get_pix_fmt_name(reader.getPixelFormat()) << std::endl;
```

### Writing

```cpp
// OpenCV
cv::VideoWriter w("out.mp4",
    cv::VideoWriter::fourcc('a','v','c','1'),
    30, cv::Size(1920, 1080));
w.write(frame);

// framewright — same pattern, more control
framewright::VideoWriter w;
w.open("out.mp4", AV_CODEC_ID_H264, 1920, 1080, {30, 1});
w.write(frame);
w.release();
```

### Property mapping

| OpenCV | framewright |
|--------|----------|
| `cap.get(CAP_PROP_FRAME_WIDTH)` | `reader.getWidth()` |
| `cap.get(CAP_PROP_FRAME_HEIGHT)` | `reader.getHeight()` |
| `cap.get(CAP_PROP_FPS)` | `reader.getFPS()` |
| `cap.get(CAP_PROP_FRAME_COUNT)` | `reader.getFrameCount()` |
| `cap.get(CAP_PROP_POS_FRAMES)` | `reader.getCurrentFrameNumber()` |

## Quick start

### C++

```cpp
#include <framewright/framewright.h>

// Read with correct BT.709 color conversion
framewright::VideoReader reader;
reader.open("input.mp4", /*force_bt709=*/true);

cv::Mat frame;
while (reader.read(frame)) {
    // frame is BGR cv::Mat with correct pixel values
}

// Write HDR10 HEVC
framewright::VideoWriterOptions opts;
opts.pix_fmt = AV_PIX_FMT_YUV420P10LE;
opts.is_10bit = true;

framewright::VideoWriter writer;
writer.open("output.mp4", AV_CODEC_ID_HEVC, 3840, 2160,
            {60000, 1001}, opts);  // 59.94 fps
writer.write(hdr_frame);  // CV_16UC3 BGR
writer.release();
```

### Python

```python
import framewright

reader = framewright.VideoReader()
reader.open("input.mp4", force_bt709=True)
for frame in reader:  # numpy (H, W, 3) uint8 BGR
    print(frame.shape, frame.dtype)

writer = framewright.VideoWriter()
writer.open("output.mp4", codec="h264", width=1920, height=1080, fps=30)
writer.write(frame)
writer.release()
```

## Reading HDR video

HDR sources (BT.2020 with PQ/SMPTE 2084 or HLG transfer) always get the
correct BT.2020 color matrix. Beyond the matrix you have three choices:

```cpp
// 1. Display-ready SDR: linearize PQ/HLG, tone map (peak from the source's
//    mastering metadata when present, 1000 nits otherwise), convert BT.2020
//    primaries to BT.709, encode with BT.709 gamma.
framewright::VideoReaderOptions opts;
opts.hdr_mode = framewright::HdrMode::ToneMapSDR;
reader.open("hdr10.mp4", opts);
reader.read(frame);   // CV_8UC3, looks right on an SDR display

// 2. Full-precision code values for your own pipeline: the transfer function
//    is NOT applied, so PQ/HLG data stays encoded, at 16-bit precision.
reader.open("hdr10.mp4");
reader.read16(frame); // CV_16UC3; pair with getColorTransfer()/getColorPrimaries()

// 2b. Linear light for image math: transfer function removed (PQ/HLG/SDR),
//     float BGR normalized so 1.0 == SDR reference white (100 nits).
//     HDR highlights exceed 1.0. Primaries unchanged.
reader.readLinear(frame); // CV_32FC3

// 3. Default read() is unchanged: matrix-only conversion. For HDR sources the
//    result remains HDR-encoded (washed out as sRGB) and a warning is logged.
```

Python: `reader.open("hdr10.mp4", tone_map_hdr=True)`, `reader.read16()`,
`reader.read_linear()`.

New to color management? [docs/COLOR.md](docs/COLOR.md) explains matrices,
range, primaries, transfer functions, linear light, and tone mapping — and
which framewright call gives you which.

Only codec-level color metadata is honored. Frame-level dynamic metadata
(HDR10+, Dolby Vision RPUs) is ignored: Dolby Vision profile 5 content
(ICtCp) decodes with approximated colors and a warning.

## Building

### As a dependency (CMake FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(
    framewright
    GIT_REPOSITORY https://github.com/kmatzen/framewright.git
    GIT_TAG main
)
FetchContent_MakeAvailable(framewright)

target_link_libraries(your_target PRIVATE framewright::framewright)
```

### Standalone

```bash
mkdir build && cd build
cmake .. -DFRAMEWRIGHT_BUILD_EXAMPLES=ON
make -j$(nproc)
```

### Python

```bash
pip install framewright
```

Binary wheels (Linux x86_64, macOS arm64, CPython 3.9–3.13) bundle a
statically linked, **LGPL** FFmpeg and OpenCV core — no system dependencies
needed. One consequence of the LGPL build: the wheels contain no x264/x265,
so **H.264/HEVC *writing* is unavailable in wheel installs** (`open()`
returns false with "codec not found"); reading — including all HDR paths —
and FFV1 lossless writing are fully functional. For H.264/HEVC writing,
install from source against a system FFmpeg that includes them:

```bash
pip install framewright --no-binary framewright   # needs OpenCV + FFmpeg dev packages
```

### Requirements

- C++17 compiler
- OpenCV 4.0+
- FFmpeg 4.2+ (libavformat, libavcodec, libswscale, libavutil)

### Windows

Dependency lookup no longer hard-requires pkg-config (Windows has none by
default): CMake tries pkg-config first, then falls back to a plain
`find_library` search. The easiest way to get both dependencies is
[vcpkg](https://vcpkg.io) in manifest mode, using the `vcpkg.json` in this
repo:

```powershell
git clone https://github.com/microsoft/vcpkg
.\vcpkg\bootstrap-vcpkg.bat

cmake -B build -G Ninja `
    -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake `
    -DVCPKG_TARGET_TRIPLET=x64-windows `
    -DFRAMEWRIGHT_BUILD_TESTS=ON
cmake --build build
```

Windows support is a recent addition (#93) and covered by CI, but is newer
than the Linux/macOS builds -- report anything that looks platform-specific.
No binary wheels ship for Windows yet; Python requires a source build with
the above toolchain.

## Logging

By default, framewright only logs errors. To see informational output:

```cpp
framewright::setLogLevel(framewright::LogLevel::Info);     // See everything
framewright::setLogLevel(framewright::LogLevel::Error);    // Errors only (default)
framewright::setLogLevel(framewright::LogLevel::Quiet);    // Silence all output
```

## API

### `framewright::VideoReader`

| Method | Description |
|--------|-------------|
| `open(filename, force_bt709, force_full_range)` | Open a video file with optional color space overrides |
| `open(filename, options)` | Open with `VideoReaderOptions` (color overrides + `HdrMode`) |
| `read(frame)` | Read the next frame as BGR `cv::Mat` (cloned, always safe) |
| `readRef(frame)` | Read the next frame without copying (valid until next read) |
| `read16(frame)` | Read as CV_16UC3 with source code values preserved (no transfer applied) |
| `readLinear(frame)` | Read as CV_32FC3 linear light (1.0 = SDR white, source primaries) |
| `hasHDR10Metadata()`, `getHDR10Metadata()` | HDR10 static metadata from the source (drives the tone-map peak) |
| `seek(frame_number)` | Seek to a frame (forward and backward) |
| `getPixelFormat()`, `getCodecID()` | Source format info |
| `getColorSpace()`, `getColorRange()` | Color metadata |
| `getColorPrimaries()`, `getColorTransfer()` | HDR-relevant color metadata |
| `isToneMappingActive()` | Whether read() is tone mapping this source |
| `getFPS()`, `getWidth()`, `getHeight()` | Video properties |

### `framewright::VideoWriter`

| Method | Description |
|--------|-------------|
| `open(filename, codec_id, w, h, fps, opts)` | Open output with full codec control |
| `write(frame)` | Write a BGR frame (CV_8UC3 or CV_16UC3, display-encoded) |
| `writeLinear(frame)` | Write a CV_32FC3 linear-light frame (PQ-encoded in HDR mode, BT.709 in SDR) |
| `setHDR10Metadata(metadata)` | Set mastering display / content light level |
| `release()` | Flush and finalize the file |

### `framewright::VideoWriterOptions`

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
