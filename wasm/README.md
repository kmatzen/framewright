# framewright for JavaScript (Node.js and browsers, via WebAssembly)

The same color-correct reader that the C++ and Python APIs expose, compiled to
WASM with Emscripten. All the HDR machinery carries over: BT.2020 matrix
handling, `read16()` code values, `readLinear()` linear light, and opt-in
tone mapping to SDR.

## Building

```bash
# One-time: Emscripten SDK + dependency sources
git clone https://github.com/emscripten-core/emsdk.git && cd emsdk
./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh && cd ..
git clone --depth 1 --branch n6.1.2 https://github.com/FFmpeg/FFmpeg.git ffmpeg-src
git clone --depth 1 --branch 4.10.0 https://github.com/opencv/opencv.git opencv-src

# Build (FFmpeg and OpenCV core compile once into wasm/deps and are cached)
cd framewright
FFMPEG_SRC=../ffmpeg-src OPENCV_SRC=../opencv-src ./wasm/build.sh
```

Output: `wasm/dist/framewright.js` + `framewright.wasm` (ES module, works in
Node ≥ 18 and modern browsers/workers).

## Usage (Node)

```js
import createFramewright from "./wasm/dist/framewright.js";
import { readFileSync } from "node:fs";

const M = await createFramewright();

// The module reads from its virtual filesystem — put the bytes there first.
M.FS.writeFile("/video.mp4", readFileSync("video.mp4"));

const reader = new M.VideoReader();
//               path       force_bt709  force_full_range  tone_map_hdr
reader.open("/video.mp4",   false,       false,            true);

console.log(reader.width(), reader.height(), reader.fps());
console.log(reader.colorSpace(), reader.colorTransfer(), reader.colorPrimaries());

let frame; // Uint8Array, H*W*3, BGR — display-ready SDR even for HDR input
while ((frame = reader.read()) !== null) {
  /* ... */
}

reader.close();
reader.delete(); // embind objects need explicit disposal
```

In a browser, fetch the file into an ArrayBuffer and `M.FS.writeFile` it the
same way. To show a frame on a canvas, expand BGR→RGBA into an `ImageData`.

### The three read methods

| Method | Returns | What you get |
|--------|---------|--------------|
| `read()` | `Uint8Array` | 8-bit BGR. With `tone_map_hdr=true`, HDR is tone mapped to display-ready SDR; otherwise matrix-only conversion (HDR stays HDR-encoded) |
| `readRGBA()` | `Uint8ClampedArray` | Same as `read()` but RGBA — drop straight into an `ImageData` for canvas rendering |
| `read16()` | `Uint16Array` | Source code values at full precision — PQ/HLG left encoded, for custom pipelines |
| `readLinear()` | `Float32Array` | Linear light, `1.0` = SDR reference white (100 nits); HDR highlights exceed 1.0. Primaries unchanged |

## Browser demo

`wasm/demo/index.html` is a self-contained demo: serve the `wasm/` directory
(`python3 -m http.server`), open the page, and drop in an HDR video — it
decodes and tone maps entirely client-side, with a toggle to see the
washed-out colors a naive decode produces.

See [docs/COLOR.md](../docs/COLOR.md) for what these terms mean and how to
choose.

## What's included in the WASM build

- **Decode**: H.264, HEVC, FFV1 — in MP4/MOV and Matroska containers
- **Encode**: FFV1 (lossless) only, via `VideoWriter.openFfv1()`. x264/x265 are
  not compiled in, so H.264/HEVC writing is native/Python-only.
- Single-threaded, no networking, files via MEMFS.

## Testing

```bash
# Fixture comes from the native test build (cmake -B build ... generates it)
node wasm/test/smoke.mjs build/test_fixtures/hdr10_matrix.mp4
```
