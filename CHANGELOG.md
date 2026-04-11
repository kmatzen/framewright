# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- `VideoReader`: explicit color space control (BT.709/BT.601, full/limited range)
- `VideoReader`: `readRef()` zero-copy frame access for performance-critical paths
- `VideoReader`: bidirectional `seek()` using keyframe seeking
- `VideoReader`: `getPixelFormat()` and `getCodecID()` accessors
- `VideoWriter`: HDR10 support (HEVC 10-bit, BT.2020, PQ, mastering display metadata)
- `VideoWriter`: configurable `HDR10Metadata` struct with `setHDR10Metadata()`
- `VideoWriter`: `VideoWriterOptions` struct for cleaner `open()` calls
- `VideoWriter`: lossless encoding (FFV1 RGB, H.264 qp=0 with 4:4:4)
- `VideoWriter`: 10-bit SDR intermediates (HEVC Main10 with BT.709)
- `VideoWriter`: QuickTime/Safari HDR compatibility (hvc1 codec tag)
- Configurable log level system (`LogLevel::Quiet/Error/Warning/Info`)
- Comprehensive test suite using Catch2
- GitHub Actions CI for Linux and macOS
- Examples: `compare_readers`, `basic_read`, `basic_write`, `hdr_write`

### Fixed
- Resource leaks in `VideoWriter::open()` error paths
- `VideoWriter::open()` called twice now safely releases previous state
- `sws_scale` return value checked in `VideoReader::read()`
- Frame dimension validation in `VideoWriter::write()`
- `VideoWriter` move constructor/assignment: added `noexcept`, `av_log_set_level`
- Replaced deprecated `av_init_packet()` with `av_packet_alloc()`
- `getFPS()` returns 0.0 instead of fabricating 30 FPS when metadata is missing
- `getFrameCount()` documents that it returns -1 when unknown
- Framerate validation in `VideoWriter::open()` rejects zero/negative values

### Changed
- Default log level is `Error` (library is silent during normal operation)
- Minimum dependency versions enforced: OpenCV 4.0+, FFmpeg 4.2+
- `VideoWriter` private members renamed to trailing underscore convention
- Removed unused `frame_pts_cache_` from `VideoReader`
