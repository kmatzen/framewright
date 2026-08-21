---------------------------- MODULE VideoReaderSeek ----------------------------
(***************************************************************************)
(* A model of framewright::VideoReader's seek / position-tracking state    *)
(* machine, as implemented in src/VideoReader.cpp (read() at :237,         *)
(* seek() at :316).                                                        *)
(*                                                                         *)
(* WHAT IS ABSTRACTED AWAY                                                 *)
(*                                                                         *)
(*   - Decoding.  A frame is just its index.  Colour conversion, sws,      *)
(*     packet/frame allocation and all AVERROR paths other than EOF are    *)
(*     out of scope; they do not touch the position counters.              *)
(*   - Time.  The C++ converts a frame index to a timestamp and back via   *)
(*     av_rescale_q / frame_duration, then (correctly, as of the fix in    *)
(*     src/VideoReader.cpp:816-825,869-872) folds the stream's start_time  *)
(*     into that conversion. We model the frame-index * frame_duration     *)
(*     part of the round trip as exact -- GENEROUS, since real av_rescale_q*)
(*     truncation can only make things worse -- but the start_time term is *)
(*     modelled precisely, including the C++'s +0.5 rounding on recovery,  *)
(*     via TicksPerFrame/StartTimeTicks and the FixTargetStartTime /       *)
(*     FixRecoveryStartTime toggles below. That is what NoStartTime.cfg,   *)
(*     StartTimeTargetOnly.cfg, StartTimeRecoveryOnly.cfg and              *)
(*     StartTimeFixed.cfg exercise -- see the README.                      *)
(*   - The decoder's internal reorder buffer.  We assume one packet in,    *)
(*     one frame out, so `pos` advances by exactly one per successful      *)
(*     read().                                                             *)
(*                                                                         *)
(* THE TWO COUNTERS                                                        *)
(*                                                                         *)
(*   pos     - the TRUE stream position: the index of the frame the next   *)
(*             successful read() will hand back.  Not a variable in the    *)
(*             C++; it is the ground truth the code is trying to track.    *)
(*   current - the code's `current_frame_` member, i.e. what               *)
(*             getCurrentFrameNumber() reports.                            *)
(*                                                                         *)
(* The whole contract of this class is that those two agree.  See          *)
(* PositionAccurate below.                                                 *)
(***************************************************************************)

EXTENDS Integers, FiniteSets, Sequences

CONSTANTS
    NumFrames,      \* frames in the file
    Keyframes,      \* set of frame indices that are keyframes; must contain 0
    SeekThreshold,  \* forward jumps strictly greater than this go via keyframe
                    \* seek.  The C++ hardcodes 50 (VideoReader.cpp:333).
    PtsAvailable,   \* TRUE  <-> frame_->pts != AV_NOPTS_VALUE and the frame
                    \* rate is known, so seek() can recover a frame index
                    \* from the timestamp (VideoReader.cpp:365-373).
                    \* FALSE <-> that recovery block is skipped.
    TicksPerFrame,      \* time_base ticks per frame -- av_rescale_q's unit,
                        \* i.e. what 1/fps rescales to. Real files use a much
                        \* finer time_base; kept small here for a tractable
                        \* state space, since only its ratio to StartTimeTicks
                        \* matters.
    StartTimeTicks,     \* the container's stream->start_time, in the same
                        \* tick units. 0 reproduces the original fixture
                        \* (start_time == 0); a value not a multiple of
                        \* TicksPerFrame reproduces the real-world case the
                        \* bug report hit, where start_time does not land on
                        \* a frame boundary.
    FixTargetStartTime,   \* TRUE <-> target_ts folds in StartTimeTicks
                        \* (VideoReader.cpp:824-825). FALSE reproduces the
                        \* pre-fix bug: the seek targets the wrong absolute
                        \* position whenever StartTimeTicks # 0.
    FixRecoveryStartTime  \* TRUE <-> the landed pts has StartTimeTicks
                        \* subtracted back out before being divided into a
                        \* frame index (VideoReader.cpp:872). FALSE
                        \* reproduces the pre-fix bug: the recovered index is
                        \* inflated by StartTimeTicks / TicksPerFrame.

ASSUME NumFrames \in Nat /\ NumFrames > 0
ASSUME Keyframes \subseteq (0 .. NumFrames - 1) /\ 0 \in Keyframes
ASSUME SeekThreshold \in Nat
ASSUME PtsAvailable \in BOOLEAN
ASSUME TicksPerFrame \in Nat /\ TicksPerFrame > 0
ASSUME StartTimeTicks \in Nat
ASSUME FixTargetStartTime \in BOOLEAN
ASSUME FixRecoveryStartTime \in BOOLEAN

VARIABLES
    pos,        \* ground-truth next frame index, or NumFrames at EOF
    current,    \* the C++ current_frame_
    opened,     \* is a file open?
    lastOk      \* did the most recent read()/seek() report success?

vars == <<pos, current, opened, lastOk>>

-----------------------------------------------------------------------------
(***************************************************************************)
(* Helpers                                                                 *)
(***************************************************************************)

\* The real (correct) presentation timestamp of frame i, in ticks: the
\* container's start_time plus i frame durations. This is ground truth --
\* what av_seek_frame() actually sees in the file -- independent of whether
\* the C++'s seek() math accounts for it correctly.
ActualPts(i) == StartTimeTicks + i * TicksPerFrame

\* target_ts, as seek() computes it (VideoReader.cpp:824-825). Correct when
\* FixTargetStartTime; otherwise reproduces the bug of targeting a timestamp
\* as if the stream started at tick 0.
TargetTicks(n) ==
    IF FixTargetStartTime
    THEN StartTimeTicks + n * TicksPerFrame
    ELSE n * TicksPerFrame

\* av_seek_frame(..., AVSEEK_FLAG_BACKWARD) lands on the last keyframe whose
\* REAL pts is at or before the requested (possibly wrongly-computed) target.
\* If the target undershoots every keyframe's real pts -- which the buggy
\* target formula can do when StartTimeTicks is large relative to n -- a
\* real demuxer clamps to the start of the file rather than failing; we
\* model that as landing on keyframe 0.
KeyframeAtOrBeforeTicks(t) ==
    LET candidates == { k \in Keyframes : ActualPts(k) <= t }
    IN  IF candidates = {} THEN 0
        ELSE CHOOSE k \in candidates : \A j \in candidates : j <= k

\* a % b rounded to the nearest integer, mirroring the C++'s
\* `static_cast<int64_t>(time_pos / frame_duration + 0.5)`. Both arguments
\* are always >= 0 in how this is used below.
RoundDiv(a, b) == (2 * a + b) \div (2 * b)

\* The frame index seek() *believes* it landed on, having decoded keyframe k
\* and looked at its real pts (VideoReader.cpp:872-873). Correct (== k)
\* when FixRecoveryStartTime; otherwise reproduces the bug of reading the
\* pts as if the stream started at tick 0, inflating the recovered index by
\* roughly StartTimeTicks / TicksPerFrame.
RecoveredIndex(k) ==
    LET landed == ActualPts(k)
        numerator == IF FixRecoveryStartTime THEN landed - StartTimeTicks ELSE landed
    IN  RoundDiv(numerator, TicksPerFrame)

\* The `while (current_frame_ < frame_number && read(tempFrame))` loops at
\* VideoReader.cpp:375 and :379.  Each successful read advances BOTH counters
\* by one; the loop also stops at EOF.
RECURSIVE ScanForward(_, _, _)
ScanForward(p, c, target) ==
    IF c >= target \/ p >= NumFrames
    THEN <<p, c>>
    ELSE ScanForward(p + 1, c + 1, target)

-----------------------------------------------------------------------------
(***************************************************************************)
(* seek(), transcribed                                                     *)
(***************************************************************************)

\* Returns [pos |-> ..., cur |-> ..., ok |-> ...], mirroring seek()'s effect
\* on the two counters and its bool return.
SeekOutcome(p, c, n) ==
    \* VideoReader.cpp:325 -- early return, no I/O at all.
    IF c = n THEN [pos |-> p, cur |-> c, ok |-> TRUE]
    ELSE
    LET needKeyframe == (n < c) \/ (n - c > SeekThreshold)   \* :332
    IN  IF ~needKeyframe
        THEN \* :378 plain forward scan
             LET r == ScanForward(p, c, n)
             IN  [pos |-> r[1], cur |-> r[2], ok |-> r[2] = n]
        ELSE \* keyframe path
             LET k == KeyframeAtOrBeforeTicks(TargetTicks(n))     \* TRUE landed keyframe
                 recovered == RecoveredIndex(k)                   \* what seek() BELIEVES it is
             IN  IF k >= NumFrames
                 THEN \* the probe read() fails; the counters were already
                      \* clobbered by the flush before this point.
                      [pos |-> k, cur |-> 0, ok |-> FALSE]
                 ELSE IF ~PtsAvailable
                 THEN \* No usable timestamp, so the landing position cannot be
                      \* determined.  seek() rewinds to the start -- a position
                      \* it can honestly report -- and fails.
                      [pos |-> 0, cur |-> 0, ok |-> FALSE]
                 ELSE IF recovered >= n
                 THEN \* The probe read consumed the (believed) target frame
                      \* itself. The seek is redone so the frame is left
                      \* undecoded, and the now-known (possibly wrong)
                      \* recovered index is used directly. The reader is
                      \* really sitting at true keyframe k, whatever it
                      \* believes.
                      [pos |-> k, cur |-> recovered, ok |-> recovered = n]
                 ELSE \* The probe consumed the frame at true position k,
                      \* believing it was `recovered`; both counters then
                      \* advance in lockstep, so any gap between k and
                      \* recovered persists through the scan.
                      LET p1 == k + 1
                          c1 == recovered + 1
                          r  == ScanForward(p1, c1, n)
                      IN  [pos |-> r[1], cur |-> r[2], ok |-> r[2] = n]

-----------------------------------------------------------------------------
(***************************************************************************)
(* Actions                                                                 *)
(***************************************************************************)

Init ==
    /\ pos = 0
    /\ current = 0
    /\ opened = FALSE
    /\ lastOk = TRUE

Open ==
    /\ ~opened
    /\ opened' = TRUE
    /\ pos' = 0
    /\ current' = 0          \* VideoReader.cpp:182
    /\ lastOk' = TRUE

Close ==
    /\ opened
    /\ opened' = FALSE
    /\ pos' = 0
    /\ current' = 0          \* cleanup(), VideoReader.cpp:424
    /\ lastOk' = TRUE

Read ==
    /\ opened
    /\ IF pos < NumFrames
       THEN /\ pos' = pos + 1
            /\ current' = current + 1     \* VideoReader.cpp:311
            /\ lastOk' = TRUE
       ELSE /\ UNCHANGED <<pos, current>>
            /\ lastOk' = FALSE
    /\ UNCHANGED opened

Seek(n) ==
    /\ opened
    /\ LET o == SeekOutcome(pos, current, n)
       IN  /\ pos' = o.pos
           /\ current' = o.cur
           /\ lastOk' = o.ok
    /\ UNCHANGED opened

\* Targets a caller might plausibly ask for, including one past the end.
SeekTargets == 0 .. NumFrames

Next ==
    \/ Open
    \/ Close
    \/ Read
    \/ \E n \in SeekTargets : Seek(n)

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
(***************************************************************************)
(* Properties                                                              *)
(***************************************************************************)

TypeOK ==
    /\ pos \in 0 .. NumFrames
    /\ current \in Int
    /\ opened \in BOOLEAN
    /\ lastOk \in BOOLEAN

(*************************************************************************)
(* THE central contract.  After any operation that reported success, the  *)
(* frame number the class advertises must be the frame number the next    *)
(* read() will actually produce.  A caller doing                          *)
(*                                                                        *)
(*     reader.seek(n);                                                    *)
(*     reader.read(frame);   // expects frame n                           *)
(*                                                                        *)
(* is relying on exactly this.                                            *)
(*************************************************************************)
PositionAccurate == (opened /\ lastOk) => (current = pos)

(*************************************************************************)
(* Weaker: even ignoring success, the advertised position should never    *)
(* run ahead of the real one -- reporting a position past what has been   *)
(* decoded means frames were silently skipped.                            *)
(*************************************************************************)
NeverOverruns == opened => (current <= pos)

(*************************************************************************)
(* A failed seek should not leave the object claiming a position it is    *)
(* not at.  (The C++ has no rollback: the keyframe path mutates state at  *)
(* :356-358 before it can fail at :362.)                                  *)
(*************************************************************************)
FailedSeekIsHonest == (opened /\ ~lastOk) => (current = pos)

=============================================================================
