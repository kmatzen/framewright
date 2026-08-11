#!/usr/bin/env bash
# Build framewright as a WASM module for Node.js and browsers.
#
# Prerequisites:
#   - Emscripten SDK activated (source emsdk_env.sh) — emcc/em++/emcmake on PATH
#   - FFmpeg source tree      (FFMPEG_SRC, default ../ffmpeg-src, tag n6.x)
#   - OpenCV source tree      (OPENCV_SRC, default ../opencv-src, tag 4.x)
#
# The two dependencies are compiled once into wasm/deps and reused; delete
# that directory to force a rebuild. Output: wasm/dist/framewright.{js,wasm}.
#
# Codec surface of this build (kept small on purpose):
#   decode: H.264, HEVC, FFV1        containers: MP4/MOV, Matroska
#   encode: FFV1 (lossless) only — x264/x265 are not compiled in.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/wasm/deps"
DIST="$ROOT/wasm/dist"
FFMPEG_SRC="${FFMPEG_SRC:-$ROOT/../ffmpeg-src}"
OPENCV_SRC="${OPENCV_SRC:-$ROOT/../opencv-src}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"

command -v emcc >/dev/null || { echo "emcc not on PATH — source emsdk_env.sh first" >&2; exit 1; }

# ---------------------------------------------------------------- FFmpeg ----
if [ ! -f "$DEPS/lib/libavcodec.a" ]; then
    echo "== Building FFmpeg for WASM"
    [ -d "$FFMPEG_SRC" ] || { echo "FFmpeg source not found at $FFMPEG_SRC" >&2; exit 1; }
    mkdir -p "$DEPS"
    (
        cd "$FFMPEG_SRC"
        emconfigure ./configure \
            --prefix="$DEPS" \
            --cc=emcc --cxx=em++ --ar=emar --ranlib=emranlib --nm=llvm-nm \
            --target-os=none --arch=x86_32 --enable-cross-compile \
            --disable-asm --disable-inline-asm --disable-x86asm \
            --disable-pthreads --disable-network --disable-programs --disable-doc \
            --disable-avdevice --disable-postproc --disable-avfilter \
            --disable-debug --disable-everything \
            --enable-decoder=h264,hevc,ffv1 \
            --enable-encoder=ffv1 \
            --enable-demuxer=mov,matroska \
            --enable-muxer=matroska,mov,mp4 \
            --enable-parser=h264,hevc \
            --enable-protocol=file
        emmake make -j"$JOBS"
        emmake make install
    )
fi

# ------------------------------------------------------------ OpenCV core ---
if [ ! -f "$DEPS/lib/libopencv_core.a" ]; then
    echo "== Building OpenCV core for WASM"
    [ -d "$OPENCV_SRC" ] || { echo "OpenCV source not found at $OPENCV_SRC" >&2; exit 1; }
    emcmake cmake -S "$OPENCV_SRC" -B "$ROOT/wasm/build-opencv" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS" \
        -DCMAKE_CXX_FLAGS="-fwasm-exceptions" \
        -DBUILD_LIST=core \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF \
        -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF -DBUILD_ZLIB=ON \
        -DWITH_IPP=OFF -DWITH_TBB=OFF -DWITH_OPENCL=OFF -DWITH_EIGEN=OFF \
        -DWITH_ITT=OFF -DWITH_PNG=OFF -DWITH_JPEG=OFF -DWITH_TIFF=OFF \
        -DWITH_WEBP=OFF -DWITH_OPENJPEG=OFF -DWITH_JASPER=OFF -DWITH_OPENEXR=OFF \
        -DWITH_V4L=OFF -DWITH_GTK=OFF -DWITH_PTHREADS_PF=OFF \
        -DCV_ENABLE_INTRINSICS=OFF -DCPU_BASELINE= -DCPU_DISPATCH=
    cmake --build "$ROOT/wasm/build-opencv" -j"$JOBS"
    cmake --install "$ROOT/wasm/build-opencv"
fi

# ------------------------------------------------------------- framewright --
echo "== Building framewright WASM module"
mkdir -p "$DIST"
OPENCV_INC="$DEPS/include/opencv4"
em++ -O2 -std=c++17 -fwasm-exceptions \
    --bind \
    -I "$ROOT/include" -I "$OPENCV_INC" -I "$DEPS/include" \
    "$ROOT/src/VideoReader.cpp" "$ROOT/src/VideoWriter.cpp" "$ROOT/src/LogLevel.cpp" \
    "$ROOT/wasm/bindings.cpp" \
    "$DEPS/lib/libavformat.a" "$DEPS/lib/libavcodec.a" "$DEPS/lib/libswscale.a" \
    "$DEPS/lib/libswresample.a" "$DEPS/lib/libavutil.a" \
    "$DEPS/lib/libopencv_core.a" $(ls "$DEPS"/lib/opencv4/3rdparty/libzlib.a 2>/dev/null || true) \
    -s MODULARIZE=1 \
    -s EXPORT_NAME=createFramewright \
    -s EXPORT_ES6=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s ENVIRONMENT=node,web,worker \
    -s EXPORTED_RUNTIME_METHODS=FS \
    -o "$DIST/framewright.js"

echo "== Done: $DIST/framewright.js"
