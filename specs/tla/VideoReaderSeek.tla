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
(*     av_rescale_q / frame_duration.  We model that round trip as exact,  *)
(*     which is GENEROUS: real rounding can only make things worse.        *)
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
    PtsAvailable    \* TRUE  <-> frame_->pts != AV_NOPTS_VALUE and the frame
                    \* rate is known, so seek() can recover a frame index
                    \* from the timestamp (VideoReader.cpp:365-373).
                    \* FALSE <-> that recovery block is skipped.

ASSUME NumFrames \in Nat /\ NumFrames > 0
ASSUME Keyframes \subseteq (0 .. NumFrames - 1) /\ 0 \in Keyframes
ASSUME SeekThreshold \in Nat
ASSUME PtsAvailable \in BOOLEAN

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

\* av_seek_frame(..., AVSEEK_FLAG_BACKWARD) lands on the last keyframe at or
\* before the requested timestamp.
KeyframeAtOrBefore(n) ==
    LET candidates == { k \in Keyframes : k <= n }
    IN  CHOOSE k \in candidates : \A j \in candidates : j <= k

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
             LET k == KeyframeAtOrBefore(n)
             IN  IF k >= NumFrames
                 THEN \* the probe read() fails; the counters were already
                      \* clobbered by the flush before this point.
                      [pos |-> k, cur |-> 0, ok |-> FALSE]
                 ELSE IF ~PtsAvailable
                 THEN \* No usable timestamp, so the landing position cannot be
                      \* determined.  seek() rewinds to the start -- a position
                      \* it can honestly report -- and fails.
                      [pos |-> 0, cur |-> 0, ok |-> FALSE]
                 ELSE IF k >= n
                 THEN \* The probe read consumed the target frame itself. The
                      \* seek is redone so the frame is left undecoded, and the
                      \* now-known keyframe index is used directly.
                      [pos |-> k, cur |-> k, ok |-> k = n]
                 ELSE \* The probe consumed frame k, so we sit at k+1.
                      LET p1 == k + 1
                          c1 == k + 1
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
