# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Binary wheels for Linux (x86_64) and macOS (arm64), CPython 3.9–3.13, with
  statically linked LGPL FFmpeg and OpenCV core — `pip install framewright`
  needs no system dependencies. The LGPL build ships no x264/x265, so
  H.264/HEVC writing requires a source install; reading and FFV1 writing are
  fully functional (#92)
- Windows support: FFmpeg dependency lookup falls back to plain
  `find_library` when pkg-config is unavailable or has no .pc files; a
  `vcpkg.json` manifest for one-command dependency install; a Windows/MSVC
  CI job building with Ninja and running the full C++/Python test suite (#93)

## [0.2.0] - 2026-08-11

### Added
- `VideoWriter`: `writeLinear()` accepts CV_32FC3 linear-light BGR (1.0 ==
  SDR reference white) and encodes it — PQ in HDR10 mode, BT.709 OETF in SDR
  mode; round-trips with `readLinear()`. Python: `write_linear()`
- `VideoReader`: HDR10 static metadata is parsed from the source (container
  side data at open, decoder SEI on the first frame) and exposed via
  `hasHDR10Metadata()`/`getHDR10Metadata()`; the mastering max luminance now
  drives the tone-mapping peak instead of a fixed 1000-nit assumption
- `VideoReader`: warnings when Dolby Vision configuration or HDR10+ dynamic
  metadata is present and ignored
- WASM: `readRGBA()` returns canvas-ready RGBA; browser demo page
  (`wasm/demo/index.html`) decodes and tone maps HDR video entirely
  client-side
- Release automation: pushing a `v*` tag builds the sdist and WASM module,
  creates the GitHub release with artifacts, and publishes to PyPI
- `VideoReader`: `readLinear()` returns CV_32FC3 linear-light BGR — PQ/HLG
  linearized per spec, SDR via the inverse BT.709 OETF, normalized so 1.0 ==
  SDR reference white; primaries left unconverted
- JavaScript/WASM support: Emscripten build (`wasm/build.sh`) producing an ES
  module for Node ≥ 18 and browsers, exposing the full reader surface
  (`read`/`read16`/`readLinear`, tone mapping, color metadata) and FFV1
  lossless writing; Node smoke test included
- Python: `read_linear()`
- `docs/COLOR.md`: an educational guide to color matrices, range, primaries,
  transfer functions, linear light, HDR, and tone mapping
- `VideoReader`: opt-in HDR-to-SDR tone mapping (`HdrMode::ToneMapSDR`) — PQ and
  HLG sources are linearized, tone mapped (extended Reinhard, 1000-nit assumed
  peak), gamut mapped BT.2020 → BT.709 and encoded with BT.709 gamma (#76, #77)
- `VideoReader`: `read16()` returns CV_16UC3 frames with the source's code
  values preserved at full precision — the lossless input for custom HDR
  pipelines, round-trips with `VideoWriter` HDR output (#78)
- `VideoReader`: `VideoReaderOptions` overload of `open()` with `HdrMode`
- `VideoReader`: `isToneMappingActive()` and `getHdrMode()` accessors
- Python: `tone_map_hdr=` option on `VideoReader.open()`, `read16()`,
  `tone_mapping_active` property
- Tests: HDR fixtures with exactly-known code values; pixel-accuracy assertions
  for the BT.2020 matrix, PQ/HLG tone mapping, and the HDR write→read
  round-trip (#80)

### Changed
- `seek()` no longer runs BGR conversion (or tone mapping) on frames it scans
  past — decode only, significantly faster over HDR content
- Tone mapping, `readLinear()` and `writeLinear()` pixel loops are
  parallelized with `cv::parallel_for_`

### Fixed
- `VideoReader` warns at `open()` when a source uses a PQ/HLG transfer and
  read() would deliver HDR-encoded (washed-out) pixels (#79)
- BT.2020 constant-luminance and ICtCp sources no longer silently fall back to
  BT.601 coefficients; the closest supported matrix is used and a warning
  logged (#81)
- `sws_setColorspaceDetails()` failures are no longer silently ignored in
  either the reader or the writer (#81)

## [0.1.0] - 2026-04-11

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
