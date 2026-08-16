#!/usr/bin/env bash
# Build the static, LGPL-clean Windows dependency prefix the wheel's compiled
# extension links against: FFmpeg (avcodec/avformat/swscale, no gpl/x264/x265
# -- H.264/HEVC *decoding* stays fully functional via the native decoders,
# and FFV1 lossless *writing* works, but H.264/HEVC writing needs a source
# install) and OpenCV core, via a throwaway vcpkg *classic mode* install.
#
# Deliberately NOT the repo's vcpkg.json manifest, which enables
# gpl+x264+x265 for full test-suite coverage in CI (#93) and uses the
# dynamic x64-windows triplet -- both wrong for a self-contained wheel,
# which must be both LGPL-only and free of external DLL dependencies.
# x64-windows-static-md links FFmpeg/OpenCV statically into the extension
# module while keeping the dynamic CRT, matching CPython's own CRT linkage
# (a static CRT would risk the classic mixed-CRT crash).
#
# cibuildwheel runs Windows before-all/before-build commands under Git Bash
# (a documented requirement -- present by default on GitHub's windows-latest
# runners), so this is a bash script rather than PowerShell, even though it
# only shells out to native Windows tools (vcpkg.exe et al).
#
# Usage: build_windows_wheel_deps.sh <vcpkg-root>
# Idempotent: skips the vcpkg install if the target triplet already has the
# libraries.
set -euo pipefail

VCPKG_ROOT="${1:?usage: build_windows_wheel_deps.sh <vcpkg-root>}"
TRIPLET="x64-windows-static-md"

if [ ! -f "$VCPKG_ROOT/vcpkg.exe" ]; then
    echo "== Bootstrapping vcpkg"
    git clone --depth 1 https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
    "$VCPKG_ROOT/bootstrap-vcpkg.bat" -disableMetrics
fi

INSTALLED="$VCPKG_ROOT/installed/$TRIPLET"
if [ ! -f "$INSTALLED/lib/avcodec.lib" ]; then
    echo "== Installing LGPL-only FFmpeg + OpenCV core ($TRIPLET)"
    # --classic: vcpkg.exe runs with cwd == the repo root (cibuildwheel's
    # package_dir), which contains our own vcpkg.json -- vcpkg auto-detects
    # that as a manifest and then refuses positional package arguments
    # ("does not support individual package arguments") unless classic mode
    # is forced. "core" is vcpkg's classic-mode CLI syntax for "no default
    # features" (the manifest-mode equivalent is "default-features": false
    # -- listing "core" inside a manifest's "features" array is rejected,
    # see #93).
    "$VCPKG_ROOT/vcpkg.exe" install --classic \
        "opencv4[core]:$TRIPLET" \
        "ffmpeg[core,avcodec,avformat,swscale]:$TRIPLET"
fi

# pkgconf is installed as a host tool (under the host triplet, not the
# target triplet) -- copy it to a fixed, predictable path so pyproject.toml
# can point PKG_CONFIG_EXECUTABLE at it without knowing the host triplet.
if [ ! -f "$VCPKG_ROOT/pkgconf.exe" ]; then
    PKGCONF="$(find "$VCPKG_ROOT/installed" -name 'pkgconf.exe' -print -quit)"
    if [ -z "$PKGCONF" ]; then
        echo "pkgconf.exe not found under $VCPKG_ROOT/installed (expected as an ffmpeg host dependency)" >&2
        exit 1
    fi
    cp "$PKGCONF" "$VCPKG_ROOT/pkgconf.exe"
fi

echo "== Dependency prefix ready: $INSTALLED"
