# Video color, explained

Everything framewright does exists because "the pixels in a video file" and
"the pixels you want in memory" are separated by four independent encoding
decisions. Get any one of them wrong and the picture is subtly — or wildly —
off. This document explains each one, what HDR adds, and which framewright
API gives you which result. It applies equally to the C++, Python, and
JavaScript/WASM APIs.

## The four decisions between a file and correct pixels

### 1. The color matrix (YUV ↔ RGB)

Video is almost never stored as RGB. It's stored as **Y'CbCr** ("YUV"): a
brightness-ish channel (Y') plus two color-difference channels (Cb, Cr). The
conversion between RGB and Y'CbCr is a 3×3 matrix — and there are several
in circulation:

| Matrix | Where you meet it |
|--------|-------------------|
| BT.601 | SD content (DVDs, old broadcasts) |
| BT.709 | HD content (managed by essentially everything 720p–4K SDR) |
| BT.2020 NCL | HDR and wide-gamut content |

The file *should* declare which matrix it used (`getColorSpace()` /
`color_space` / `colorSpace()`). Many files don't. If a decoder guesses
wrong — decoding BT.601 video with the BT.709 matrix, say — every color
shifts a few percent: skin tones drift, reds and greens exchange a little
energy. It's the classic "looks almost right but not quite" bug, and it's
why framewright exists: OpenCV's `VideoCapture` neither tells you which
matrix it used nor lets you choose.

**What framewright does:** uses the declared matrix, including BT.2020 for
HDR; defaults untagged content sensibly (BT.709 for HD, BT.601 for SD);
lets you override with `force_bt709`.

### 2. Range (limited vs full)

Historical broadcast video doesn't use the full 8-bit scale: black is code
16 and white is code 235 ("limited"/"TV" range), with the rest reserved for
overshoot. Computer-generated video often uses 0–255 ("full"/"PC" range).
Decode a limited-range file as full range and everything looks washed out
gray; the reverse crushes shadows and clips highlights.

**What framewright does:** uses the declared range, defaults to limited
(the overwhelmingly common case for video), and lets you override with
`force_full_range`.

### 3. Primaries (the gamut)

"RGB" is meaningless until you say *which* red, green, and blue. The
**primaries** are the exact chromaticities of the three basis colors:

- **BT.709/sRGB** — the standard SDR monitor gamut.
- **BT.2020** — a much wider gamut used by HDR video. Its "red" is far more
  saturated than sRGB's red.
- **Display P3** — between the two; common on phones and laptops.

The numeric triple (1.0, 0, 0) is a *different actual color* in each. If you
take BT.2020-encoded values and display them unconverted on an sRGB screen,
everything looks **undersaturated** — the encoded values assumed a more
saturated red than your screen can show, so they asked for "less" of it.
Converting between primaries is a 3×3 matrix applied in *linear* light
(see below), followed by handling colors the destination can't represent
(gamut mapping — at its simplest, clipping).

**What framewright does:** reports the primaries (`getColorPrimaries()`),
and in tone-map mode converts BT.2020 → BT.709 for you. `read16()` and
`readLinear()` deliberately do *not* convert, so your own pipeline can.

### 4. The transfer function (gamma, PQ, HLG)

Human vision is roughly logarithmic: we're far more sensitive to a change
in a shadow than the same change in a highlight. Storing light *linearly*
in 8 bits would waste most codes on highlights we can't distinguish and
starve the shadows — visible banding. So stored values are **non-linear**:
a *transfer function* maps between stored code values and actual light.

- **BT.709 / sRGB gamma** — SDR video and images. Roughly `light ≈ code^2.2`.
- **PQ (SMPTE ST 2084)** — HDR10's transfer. *Absolute*: a code value means
  a specific luminance, from 0.0001 up to 10,000 cd/m² (nits). Extremely
  aggressive shadow allocation.
- **HLG (ARIB STD-B67)** — broadcast HDR. *Relative* like gamma, with a log
  segment on top; degrades gracefully on SDR displays.

This is the one conversion swscale/OpenCV never touch — a YUV→RGB matrix
conversion changes the *representation*, not the encoding of light. That's
why naively decoded HDR looks washed out and flat: PQ code values, designed
to span 10,000 nits, get displayed as if they were sRGB gamma spanning ~300.

**What framewright does:** reports the transfer (`getColorTransfer()`),
warns when you decode PQ/HLG without handling it, and offers three explicit
ways to handle it (next section).

## Linear light, and why you'd want it

"Linear RGB" means values proportional to physical light: doubling the
number doubles the photons. It is the *only* correct domain for most math on
images — blending, resizing, blurring, compositing, averaging, and any
physically-based computation. Do those operations on gamma- or PQ-encoded
values and you get darkened edges, wrong mixes, and halos; it often looks
"okay" only because we're all used to seeing the artifacts.

The catch: linear light needs more precision than 8 bits (that's *why*
transfer functions exist), so linear frames are floats.

**What framewright does:** `readLinear()` returns float BGR
(`CV_32FC3` / numpy `float32` / `Float32Array`) with the transfer function
removed — PQ and HLG per their specs, SDR via the inverse BT.709 OETF.
Values are normalized so **1.0 = SDR reference white (100 nits)**. SDR
content therefore lands in [0, 1], and HDR highlights go *above* 1.0 (PQ's
10,000-nit peak is 100.0). Primaries are unchanged; check
`getColorPrimaries()` and gamut-convert if you need a specific working
space.

## HDR in one paragraph

An HDR10 video = 10-bit code values + BT.2020 primaries + PQ transfer +
static metadata about the mastering display (peak brightness, etc.). HLG
swaps PQ for its hybrid curve and drops the metadata requirement. Dynamic
formats (HDR10+, Dolby Vision) add per-scene metadata on top — framewright
ignores those layers (and warns on Dolby Vision profile 5's ICtCp, which
needs more than a matrix). So "reading HDR correctly" means: BT.2020
matrix, 10-bit precision, and *deciding what to do about PQ/HLG and the
wide gamut* — which is exactly the choice the API puts in your hands.

## Tone mapping: HDR content, SDR result

An HDR frame can contain 1,000-nit highlights; an SDR encoding tops out at
reference white. **Tone mapping** is the lossy, opinionated compression of
that range into the displayable one — deciding that a 1,000-nit sky becomes
"very bright white" while mid-tones stay put. There is no single correct
answer; there are families of curves with different trade-offs.

framewright's `HdrMode::ToneMapSDR` (`tone_map_hdr=True` in Python, the
fourth `open()` argument in JS) implements a standard, hue-preserving
pipeline:

1. linearize PQ/HLG (per spec, HLG with the nominal 1000-nit OOTF),
2. scale all three channels by a luminance ratio from an extended Reinhard
   curve (assumed 1000-nit mastering peak) — scaling luminance rather than
   each channel independently preserves hue,
3. convert primaries BT.2020 → BT.709 in linear light, clip what's left
   outside,
4. re-encode with BT.709 gamma to 8-bit.

The result is what you'd expect a competent video player to show on an SDR
monitor.

## Bit depth: why 10-bit matters and where it goes

HDR video is 10-bit for the same banding reasons as above — PQ across
10,000 nits needs the extra codes. Truncating to 8 bits *before* your
processing throws away information you cannot recover afterward.
`read16()` preserves the full precision (10-bit codes scaled onto the
16-bit scale) with the encoding untouched; `readLinear()` preserves it as
floats with the encoding removed. Plain `read()` is 8-bit and fine when
8-bit is where you were headed anyway.

## Choosing an API, by goal

| You want | Call | Comes back as |
|----------|------|---------------|
| Frames that look right on an SDR screen, HDR or not | `open(..., ToneMapSDR)` + `read()` | 8-bit BGR, BT.709 |
| The exact bits the file encodes, at full precision | `read16()` | 16-bit BGR code values (PQ/HLG still encoded) |
| Physically meaningful light for image math / custom pipelines | `readLinear()` | float BGR, linear, 1.0 = SDR white, source primaries |
| Legacy behavior (SDR workflows) | `read()` | 8-bit BGR, matrix-only conversion |
| To write HDR10 from linear light | `VideoWriter` with `is_10bit` + `writeLinear()` | PQ encoding applied for you; round-trips with `readLinear()` |
| To write HDR10 from PQ code values | `VideoWriter` with `is_10bit` + `write(CV_16UC3)` | HEVC Main10, BT.2020+PQ tagged, mastering metadata |

All three languages expose the same surface:

| Concept | C++ | Python | JS/WASM |
|---------|-----|--------|---------|
| Tone-mapped open | `VideoReaderOptions{.hdr_mode=HdrMode::ToneMapSDR}` | `open(path, tone_map_hdr=True)` | `open(path, false, false, true)` |
| Code values | `read16(mat)` → `CV_16UC3` | `read16()` → `uint16` array | `read16()` → `Uint16Array` |
| Linear light | `readLinear(mat)` → `CV_32FC3` | `read_linear()` → `float32` array | `readLinear()` → `Float32Array` |
| Metadata | `getColorSpace()` etc. | `color_space` etc. | `colorSpace()` etc. |

(The WASM build decodes H.264/HEVC/FFV1 and encodes FFV1 only — see
[wasm/README.md](../wasm/README.md).)

## Glossary

- **Y'CbCr / YUV** — luma + two chroma channels; how video stores color.
- **Chroma subsampling (4:2:0 / 4:4:4)** — storing chroma at reduced
  resolution; 4:2:0 halves it both ways, 4:4:4 keeps it all.
- **Code value** — the stored integer before any interpretation.
- **EOTF / OETF** — the transfer function directions: Electro-Optical
  (code → light, display side) and Opto-Electronic (light → code, camera
  side).
- **Gamut** — the set of colors a primaries triple can represent.
- **Nit (cd/m²)** — luminance unit; SDR reference white ≈ 100, HDR10 peaks
  up to 10,000.
- **PQ / HLG** — the two HDR transfer functions (absolute / relative).
- **Reference white** — the luminance of "diffuse white" (paper, UI white);
  the anchor between SDR and HDR brightness scales.
- **Tone mapping** — compressing HDR luminance range into SDR.
- **VUI** — the part of an H.264/HEVC bitstream that carries the color
  tags; what decoders actually trust.
