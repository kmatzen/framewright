#!/usr/bin/env bash
# Build the static, LGPL-clean dependency prefix the binary wheels link
# against: FFmpeg (static, PIC, --disable-gpl — so no x264/x265; H.264/HEVC
# *decoding* stays fully functional via the native decoders, and FFV1
# lossless *writing* works, but H.264/HEVC writing needs a source install)
# and OpenCV core (static, bundled zlib).
#
# Usage: build_wheel_deps.sh <prefix>
# Idempotent: skips each dependency whose libraries already exist in <prefix>.
# Runs inside cibuildwheel's manylinux container and on the macOS runners.
set -euo pipefail

PREFIX="${1:?usage: build_wheel_deps.sh <prefix>}"
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
WORK="${TMPDIR:-/tmp}/framewright-deps-src"
mkdir -p "$PREFIX" "$WORK"

FFMPEG_TAG="n6.1.2"
OPENCV_TAG="4.10.0"

# Tools (manylinux is dnf-based; macOS runners have everything but maybe nasm)
if command -v dnf >/dev/null 2>&1; then
    dnf -y install cmake nasm git pkgconfig >/dev/null 2>&1 || true
elif command -v brew >/dev/null 2>&1; then
    command -v nasm >/dev/null 2>&1 || brew install nasm >/dev/null 2>&1 || true
fi

# A checkout placed in <repo>/.wheel-deps-src/{ffmpeg,opencv} is used instead
# of cloning — lets offline/proxied environments pre-fetch the sources.
LOCAL_SRC="$(cd "$(dirname "$0")/.." && pwd)/.wheel-deps-src"

# ---------------------------------------------------------------- FFmpeg ----
if [ ! -f "$PREFIX/lib/libavcodec.a" ]; then
    echo "== Building static LGPL FFmpeg ($FFMPEG_TAG)"
    rm -rf "$WORK/ffmpeg"
    if [ -d "$LOCAL_SRC/ffmpeg" ]; then
        cp -r "$LOCAL_SRC/ffmpeg" "$WORK/ffmpeg"
    else
        git clone --depth 1 --branch "$FFMPEG_TAG" https://github.com/FFmpeg/FFmpeg.git "$WORK/ffmpeg"
    fi
    cd "$WORK/ffmpeg"
    make distclean >/dev/null 2>&1 || true

    ASM_FLAG=""
    command -v nasm >/dev/null 2>&1 || ASM_FLAG="--disable-x86asm"

    ./configure \
        --prefix="$PREFIX" \
        --enable-static --disable-shared --enable-pic \
        --disable-gpl --disable-nonfree \
        --disable-programs --disable-doc \
        --disable-avdevice --disable-postproc --disable-avfilter \
        --disable-network --disable-autodetect \
        $ASM_FLAG
    make -j"$JOBS"
    make install
fi

# ------------------------------------------------------------ OpenCV core ---
if [ ! -f "$PREFIX/lib/libopencv_core.a" ] && [ ! -f "$PREFIX/lib64/libopencv_core.a" ]; then
    echo "== Building static OpenCV core ($OPENCV_TAG)"
    rm -rf "$WORK/opencv"
    if [ -d "$LOCAL_SRC/opencv" ]; then
        cp -r "$LOCAL_SRC/opencv" "$WORK/opencv"
    else
        git clone --depth 1 --branch "$OPENCV_TAG" https://github.com/opencv/opencv.git "$WORK/opencv"
    fi
    cmake -S "$WORK/opencv" -B "$WORK/opencv-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_LIST=core \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF \
        -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF -DBUILD_ZLIB=ON \
        -DWITH_IPP=OFF -DWITH_TBB=OFF -DWITH_OPENCL=OFF -DWITH_EIGEN=OFF \
        -DWITH_ITT=OFF -DWITH_PNG=OFF -DWITH_JPEG=OFF -DWITH_TIFF=OFF \
        -DWITH_WEBP=OFF -DWITH_OPENJPEG=OFF -DWITH_JASPER=OFF -DWITH_OPENEXR=OFF \
        -DWITH_V4L=OFF -DWITH_GTK=OFF \
        -DWITH_PROTOBUF=OFF -DBUILD_PROTOBUF=OFF -DWITH_ADE=OFF \
        -DWITH_QUIRC=OFF -DWITH_FLATBUFFERS=OFF
    cmake --build "$WORK/opencv-build" -j"$JOBS"
    cmake --install "$WORK/opencv-build"
fi

echo "== Dependency prefix ready: $PREFIX"
