#!/usr/bin/env bash
# Generate the hdr10_matrix.mp4 pixel fixture for the WASM smoke test,
# mirroring the CMake test-fixture command (see generate_hdr_pixel_fixture in
# CMakeLists.txt): solid RGB (208, 64, 32) converted to BT.2020 limited-range
# 10-bit YUV, x265 lossless, PQ-tagged in the VUI. zscale (zimg) preferred for
# its independent conversion; swscale accurate_rnd is the fallback and stays
# within the smoke test's tolerances.
set -euo pipefail

out="${1:?usage: gen_fixture.sh <output.mp4>}"

vui="lossless=1:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:range=limited"

ffmpeg -y -loglevel error -f lavfi \
    -i "color=c=0xD04020:size=192x108:rate=30:duration=0.1" \
    -vf "format=gbrp,zscale=matrix=2020_ncl:range=limited:rangein=full,format=yuv420p10le" \
    -c:v libx265 -x265-params "$vui" \
    -colorspace bt2020nc -color_primaries bt2020 -color_trc smpte2084 \
    -color_range tv -tag:v hvc1 "$out" ||
ffmpeg -y -loglevel error -f lavfi \
    -i "color=c=0xD04020:size=192x108:rate=30:duration=0.1" \
    -vf "scale=out_color_matrix=bt2020:out_range=tv:flags=accurate_rnd+bitexact+full_chroma_int" \
    -pix_fmt yuv420p10le \
    -c:v libx265 -x265-params "$vui" \
    -colorspace bt2020nc -color_primaries bt2020 -color_trc smpte2084 \
    -color_range tv -tag:v hvc1 "$out"

echo "fixture written: $out"
