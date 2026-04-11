# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Initial public release
- `VideoReader` with explicit color space control (BT.709/BT.601, full/limited range)
- `VideoWriter` with HDR10 support (HEVC 10-bit, BT.2020, PQ, mastering display metadata)
- Lossless encoding support (FFV1 RGB, H.264 qp=0 with 4:4:4)
- 10-bit SDR intermediates (HEVC Main10 with BT.709)
- QuickTime/Safari HDR compatibility (hvc1 codec tag)
- Example: `compare_readers` for comparing cvffmpeg vs OpenCV color handling
