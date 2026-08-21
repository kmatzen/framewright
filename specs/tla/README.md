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

## Start_time: a second, independent bug class

The five configs above model `seek()`'s off-by-one and are unaffected by the
container's `start_time` -- they all set `StartTimeTicks = 0`. A second,
later bug lived in the same function but in a different part of the round
trip: the keyframe path converts a frame index to a target timestamp and
back (`TargetTicks` / `RecoveredIndex` in the spec, `VideoReader.cpp:816-825`
and `:869-872` in the code) and, before the fix, did that conversion as if
the stream's `start_time` were always zero. Real files commonly have a
non-zero `start_time` that is not even a whole number of frame durations.

`TicksPerFrame` and `StartTimeTicks` model the tick-level arithmetic
precisely (including the C++'s `+0.5` rounding), rather than treating the
frame-index/timestamp round trip as exact the way the base model does.
`FixTargetStartTime` and `FixRecoveryStartTime` toggle the two halves of the
fix independently, so the four configs below form a small truth table
instead of a single before/after pair:

| config | target folds in start_time | recovery folds in start_time | `PositionAccurate` |
|---|---|---|---|
| `StartTimeBug.cfg` | no | no | **violated**, depth 3 (matches the real pre-fix code) |
| `StartTimeTargetOnly.cfg` | yes | no | **violated**, depth 3 |
| `StartTimeRecoveryOnly.cfg` | no | yes | holds |
| `StartTimeFixed.cfg` | yes | yes | **holds** (matches the real fix) |

All four are exhaustive. `StartTimeBug.cfg` and `StartTimeTargetOnly.cfg`
also violate `NeverOverruns` on the same trace -- unlike the off-by-one bug
above (which the README's own `Overrun.cfg` result shows never runs ahead of
the true position), this bug class *can* report a position past what has
been decoded.

**The counterexample**, identical for both violating configs (`NumFrames =
12`, `Keyframes = {0, 6}`, `TicksPerFrame = 4`, `StartTimeTicks = 5` -- a
start_time that is 1.25 frame durations, deliberately not frame-aligned,
mirroring the real bug report's 0.083s offset):

```
open();  seek(4);          -> pos = 3, current = 4     (reports true, current AHEAD of pos)
```

`current_frame_` claims to be at frame 4, and the caller believes the seek
succeeded, but the next `read()` actually returns frame 3.

**Why fixing only the target half is not enough.** With `StartTimeTicks = 5`
and `TicksPerFrame = 4`, requesting frame 4 has a true target timestamp of
`5 + 4*4 = 21` ticks, which lands on keyframe 0 (`ActualPts(0) = 5 <= 21`;
`ActualPts(6) = 29 > 21`). That part is fixed. But the still-buggy recovery
then reads keyframe 0's real pts (5 ticks) as if start_time were zero:
`RoundDiv(5, 4) = 1`, not `0` (`(2*5+4) \div (2*4) = 14 \div 8 = 1`). Landing
on keyframe 0 (the frame just decoded to probe it) should make the *next*
read frame `0 + 1 = 1`; instead, believing itself to already be at recovered
frame `1`, the code starts the forward-scan one frame ahead of where it
really is, at `pos=1, current=2` instead of `pos=1, current=1` -- the same
shape of error as the original off-by-one bug's `NoPts` case, a wrong
believed origin, just introduced by a different piece of unfixed arithmetic.
The scan then advances both counters in lockstep up to `current = 4`, ending
at `pos=3, current=4`, which is exactly the `current`-ahead-of-`pos` state
TLC reports above: the one-frame error at the start never gets corrected,
only carried forward.

**Why fixing only the recovery half turns out to be harmless (here).** This
is the interesting asymmetric result. If recovery is fixed but target is
not, target still undershoots -- since `start_time > 0`, `n * TicksPerFrame`
is always less than the true `start_time + n * TicksPerFrame`, so
`KeyframeAtOrBeforeTicks` can only land on a keyframe at or *before* the one
a correct target would reach (monotonicity: a smaller target timestamp never
lands later). The recovery step, now correct, reports that earlier landing
truthfully. `current_frame_` and the true position are equal at that point,
so the plain index-based forward-scan loop -- unaffected by start_time,
since it counts frames rather than timestamps -- simply decodes a few extra
frames to reach the requested index. Correct end state, worse performance.
This is exactly why the real fix touches both sites: an unfixed target isn't
just "a little worse", it silently degrades keyframe seeking to scanning for
some inputs, whereas an unfixed recovery is an outright correctness bug on
its own.

**Empirical confirmation.** `tests/test_video_reader.cpp` covers this against
`seek_numbered_offset_start.mp4` -- the same numbered-luma fixture as the
off-by-one bug's, but muxed with `-output_ts_offset 0.083` so `start_time` is
non-zero without changing the frame count (plain `-itsoffset` was tried
first; it gets absorbed into extra padding frames by the muxer's
negative-timestamp handling instead of surfacing as a `start_time`, so it
does not reproduce the bug). Reverting the fix and re-running fails with
`seek(0)` from frame 1 returning `false`, matching the model's prediction
that this bug class can turn a seek that should succeed into an outright
failure, not just a wrong frame. A second test,
`VideoReader getCurrentTimestamp is relative to frame 0, not container
start_time`, covers the same root cause in `getCurrentTimestamp()`, which
reported the raw container pts (frame 0 at `~start_time`, not `~0.0`)
independently of `seek()`.

## What the counterexamples say (the off-by-one bug)

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

- the `n * frame_duration` term of the frame-index ↔ timestamp round trip is
  exact, ignoring `av_rescale_q`'s integer truncation. The `start_time` term
  is *not* idealized this way -- `TicksPerFrame` / `StartTimeTicks` model it
  exactly, including the C++'s `+ 0.5` rounding on recovery, per the
  start_time section above;
- the decoder emits exactly one frame per packet, ignoring reorder-buffer
  delay after `avcodec_flush_buffers()`. This assumption is *not* verified by
  the model, but it is now covered empirically: the `[seek]` cases also run
  against `seek_numbered_bframes.mp4`, encoded with B-frames so decode and
  presentation order differ, and all of them pass unchanged. Building that
  fixture is what exposed #59 — the reorder buffer was not being drained at
  EOF, silently costing the last frame of every B-frame file;
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
