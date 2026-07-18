# TLA+ model of `VideoReader` seek / position tracking

`VideoReaderSeek.tla` models the seek state machine in `src/VideoReader.cpp`
(`read()` at :237, `seek()` at :316) and checks one contract:

> the frame number `getCurrentFrameNumber()` reports must be the frame number
> the next `read()` actually returns.

That is `PositionAccurate` in the spec. Callers depend on it implicitly every
time they write `reader.seek(n); reader.read(frame);`.

## Running

```sh
java -cp tla2tools.jar tlc2.TLC -config VideoReaderSeek.cfg VideoReaderSeek.tla
```

`tla2tools.jar` is not vendored; fetch it from
<https://github.com/tlaplus/tlaplus/releases/latest/download/tla2tools.jar>.

| config | constants | invariant | before fix | after fix |
|---|---|---|---|---|
| `VideoReaderSeek.cfg` | threshold 3, pts available | `PositionAccurate` | violated, depth 3 | **holds** |
| `NoPts.cfg` | threshold 3, no usable pts | `PositionAccurate` | violated, depth 3 | **holds** |
| `Faithful50.cfg` | threshold 50 (the real constant) | `PositionAccurate` | violated, depth 4 | **holds** |
| `FailedSeek.cfg` | threshold 3 | `FailedSeekIsHonest` | violated, depth 3 | **holds** |
| `Overrun.cfg` | threshold 3 | `NeverOverruns` | holds | **holds** |

All five are exhaustive (0 states left on queue). The spec now models the
fixed code; the counterexamples below are what it found against the original.

## What the counterexamples say

**1. The keyframe path is off by one** (`Faithful50.cfg`, the real threshold).

```
open();  read(f);          -> pos = 1, current = 1     (consistent)
seek(0);                   -> pos = 1, current = 0     (returns true)
```

`seek()` returns `true` and reports frame 0, but the stream is positioned at
frame 1 — frame 0 has already been consumed and thrown away into `tempFrame`.

The cause is a clash of conventions on `current_frame_`. Everywhere else it
means *frames consumed*, so it is the index of the **next** frame (`read()`
post-increments it at :311, `open()` sets 0 at :182). But the pts-recovery
block at :365-373 assigns it the index of the frame **just decoded**:

```cpp
current_frame_ = static_cast<int64_t>(time_pos / frame_duration + 0.5);
```

`time_pos` is `frame_->pts` — the frame `read()` already consumed at :361. It
should be that index **+ 1**.

Because the forward-scan branch at :378 is correct, the two branches disagree
by one frame, so the bug appears and disappears depending on seek distance and
direction. `seek(4)` on the baseline config lands on `pos=5, current=4`.

**2. Without usable timestamps it is off by much more** (`NoPts.cfg`).

If `frame_->pts == AV_NOPTS_VALUE` or `frame_duration <= 0`, the recovery block
is skipped entirely and `current_frame_` keeps the value `1` that `read()` left
it at — as though the keyframe were frame 0. The forward scan then runs from
the wrong origin. TLC's trace: `seek(6)` reports frame 6 while sitting at frame
12, i.e. EOF. The error scales with the keyframe index, so it is worst exactly
where keyframe seeking matters most: deep into a long file.

**3. A failed seek leaves the object mis-positioned** (`FailedSeek.cfg`).

`seek()` mutates `current_frame_` at :357 before the read at :361 that can
fail, and neither branch rolls back on the `return current_frame_ == frame_number`
at :383. After a failed seek the object reports a position it is not at.

**4. What is *not* broken** (`Overrun.cfg`, exhaustive, no error).

`current_frame_` never runs *ahead* of the true position. Divergence is always
"reported position lags reality", so the failure mode is uniformly *frames get
silently skipped* — never frames replayed or fabricated. That bounds the blast
radius: callers get the wrong frame, not a wrong-sized or duplicated one.

## Fidelity and limits

The model is generous to the code in three ways, so real behaviour can only be
worse:

- the frame-index ↔ timestamp round trip is exact, ignoring the `+ 0.5`
  rounding and `av_rescale_q` truncation;
- the decoder emits exactly one frame per packet, ignoring reorder-buffer
  delay after `avcodec_flush_buffers()`;
- error paths other than EOF are not modelled.

It also models a single reader with no concurrency — that is faithful, since
`VideoReader` is not thread-safe and does not claim to be.

## Empirical confirmation

`empirical/seek_probe.cpp` runs the real library against a 60-frame lossless
H.264 file with keyframes at 0 and 30, and identifies each decoded frame by a
frame-index value baked into its luma. Full output in `empirical/results.txt`:

```
from   seek(n)  branch   returned   reports frame  next read() is frame
1      0        keyframe true       0              1     <-- MISMATCH
50     5        keyframe true       5              6     <-- MISMATCH
50     30       keyframe true       30             31    <-- MISMATCH
50     45       keyframe true       45             46    <-- MISMATCH
40     35       keyframe true       35             36    <-- MISMATCH
1      5        scan     true       5              5
1      29       scan     true       29             29
1      45       scan     true       45             45
```

Every keyframe-path seek is off by exactly one; every forward-scan seek is
correct. That is precisely the split the model predicted, which is the useful
part — the abstraction picked the right branch as the culprit, and the
discrepancy is in `VideoReader.cpp`, not in the spec.

After the fix (`empirical/results-after-fix.txt`) all twelve cases agree:
`reports frame` equals `next read() is frame` for every target, on both
branches.

## The fix

Applied to `seek()`. Three changes, all in the keyframe branch:

1. **The off-by-one.** The index recovered from the pts is the frame the probe
   read already *consumed*, so `current_frame_` is that index **+ 1**.

2. **Target-is-a-keyframe.** `+ 1` alone regresses the case where
   `av_seek_frame()` lands exactly on the requested frame: the probe read
   consumes frame `n` itself, `current_frame_` becomes `n + 1`, the scan loop
   does not run, and `seek()` returns *false* for a seek that used to
   (wrongly) succeed. A consumed frame cannot be un-consumed, so when the
   recovered index is `>= frame_number` the seek is redone now that the
   keyframe index is known, leaving the frame undecoded.

3. **No usable timestamp.** Previously the code guessed, silently. It now
   rewinds to the start — a position it can honestly report — and returns
   false.

Point 2 is worth dwelling on: the obvious one-line fix is wrong, and it is
wrong in a way that only shows up when the seek target happens to be a
keyframe. That is exactly the kind of case a hand-written test set tends to
miss and an exhaustive check does not.

## Regression tests

`tests/test_video_reader.cpp` (`[seek]` tag) covers this against a generated
fixture, `seek_numbered.mp4`: 60 frames whose luma encodes the frame index, with
keyframes forced at 0 and 30. That lets the tests assert *which* frame came
back, not merely that `seek()` returned true.

Verified to actually catch the bug: reverting `seek()` and re-running fails 8 of
the 12 cases, each off by exactly one.

Two traps these tests are shaped around:

- **Branch coverage.** An early probe seeking to 0, 5, 29, 30, 31 and 45 from
  position 1 showed only `seek(0)` failing, because every other target was a
  forward jump of under 50 frames and so took the already-correct scan path.
  Cases must seek **backward**, or forward by more than `SeekThreshold`, and
  include a target that is itself a keyframe.
- **Fixture aliasing.** The first fixture used luma `16 + 4*N`, which exceeds
  the limited-range ceiling of 235 near the end, so frames 55-59 all clipped to
  255 and became indistinguishable — surfacing as a bogus "seek(59) returned
  frame 55" failure. The step is now 3, and the test asserts up front that
  consecutive frames differ by more than the matching tolerance, so any future
  recurrence names itself instead of masquerading as a seek bug.
