# Contributing to framewright

Thanks for your interest in contributing! Here's how to get started.

## Building from source

```bash
git clone https://github.com/kmatzen/framewright.git
cd framewright
mkdir build && cd build
cmake .. -DFRAMEWRIGHT_BUILD_EXAMPLES=ON -DFRAMEWRIGHT_BUILD_TESTS=ON
make -j$(nproc)
```

### Requirements

- C++17 compiler
- OpenCV 4.0+
- FFmpeg 4.2+ (libavformat, libavcodec, libswscale, libavutil)
- For tests: ffmpeg and ffprobe CLI tools (for generating test fixtures)

## Running tests

```bash
cd build
ctest --output-on-failure
```

## Submitting changes

1. Fork the repository and create a branch from `main`.
2. Make your changes. Keep commits focused — one logical change per commit.
3. Make sure the project builds cleanly and tests pass.
4. Open a pull request against `main`.

## Code style

- Use trailing underscores for private member variables (`width_`, `formatCtx_`).
- Match the existing formatting (clang-format is not enforced yet, but keep it consistent).
- Log messages use `detail::log(LogLevel::Error/Warning/Info)` — never `std::cerr` directly.
- Check return values from FFmpeg API calls.
- Free resources on all error paths in `open()` methods.

## Reporting bugs

Open an issue on GitHub with:
- What you expected to happen
- What actually happened
- A minimal reproducer if possible
- Your OS, compiler, OpenCV version, and FFmpeg version
