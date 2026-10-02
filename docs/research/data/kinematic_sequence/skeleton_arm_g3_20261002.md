# The skeleton's lead arm in the library — G2 and G3 (2 October 2026)

`ks_skeleton3d_route_design.md` Part A: the lead-arm rung `faceOn+dtl3d`. The trunk has no
skeleton rung (`skeleton_rate_k0_20261002.md` §8).

## G2 — parity

These runs used the Mac `swinglab_run` with pinned poses. The "before" binary is the pre-change
build, run from inside the build directory: a copy run from elsewhere cannot find its models, and
the address marks and skeleton then differ for that reason alone.

| Swing | Comparison | Keys that differ |
|---|---|---|
| 07-04 s8 (two cameras) | before vs `sequence.skel3d.leadArm=false` | `skeleton3d.diagnostics.ms` only (the fit's wall-clock time). The rate series move later in the series array, as designed |
| 07-04 s8 | before vs default (rung on) | `leadArmAngularSpeed`, `kinematicSequence` (+ the ms) |
| 07-05 s1 (face-on only) | before vs default | `skeleton3d.diagnostics.ms` only |

## G3 — 07-04 re-analysed in place

The 15 swings were re-analysed in place on GOLFSIMPC (Release, `--write-back --session-type 1`, the
reuse path). The backup is `/mnt/swingdata/scratch/backup-pre-ksarm-20261002/`.

**Control.** s8's pre-change document was copied twice into
`/mnt/swingdata/scratch/ksarm-ctl/`, and each copy was run through one of the two studio builds:

- **old build vs new build, same input:** only `leadArmAngularSpeed`, `kinematicSequence` and
  `skeleton3d.diagnostics.ms` differ;
- **old build vs the backup it started from:** `club`, `clubDtl`, `clubheadSpeed`, `handSpeed`,
  the plane, the rotation series and `skeleton3d` all move. Re-analysis is **not idempotent**: the
  reuse path re-runs the DTL shaft tracker, which reads its own previous flags, and refreshes the
  synth. That is pre-existing and not from this change.

At the phase samples checked, the headline readings are unchanged to 0.1 on all 15 swings:
clubhead speed at impact, pelvis turn at impact, and thorax turn at the top. The drift is in the
detail of the series.

**The lead arm, before (face-on) → after.**

| Swing | Before: route, ms before impact, ±σ_t, peak °/s ±σ | After |
|---|---|---|
| 1 | faceOn 107.7 ±18.8, 666 ±39 | faceOn+dtl3d 107.7 ±16.3, 808 ±117 |
| 2 | faceOn 107.2 ±28.0, 682 ±42 | faceOn+dtl3d 113.9 ±19.2, 865 ±127 |
| 3 | faceOn 107.2 ±18.2, 709 ±42 | faceOn+dtl3d 107.2 ±13.3, 831 ±105 |
| 4 | faceOn 100.5 ±17.5, 702 ±42 | faceOn+dtl3d 100.5 ±16.1, 816 ±119 |
| 5 | faceOn 100.4 ±18.1, 678 ±41 | **faceOn, unchanged.** The skeleton's arm peak is flat (σ_t ≈ 40 ms), so the rung could neither place nor bound, and stepped aside |
| 6 | faceOn 107.3 ±17.9, 717 ±43 | faceOn+dtl3d 107.3 ±13.0, 865 ±130 |
| 7 | faceOn 100.5 ±16.2, 803 ±49 | faceOn+dtl3d 107.2 ±15.8, 830 ±115 |
| 8 | faceOn 93.9 ±17.6, 677 ±40 | faceOn+dtl3d 107.2 ±10.2, 837 ±112 |
| 9 | faceOn 100.4 ±20.7, 760 ±46 | faceOn+dtl3d 100.4 ±13.2, 819 ±100 |
| 10 | faceOn 120.5 ±49.9, **unplaced** | faceOn+dtl3d 113.8 ±14.3, 802 ±106, **placed** |
| 11 | faceOn 87.2 ±18.5, 689 ±43 | faceOn+dtl3d 113.9 ±22.8, 781 ±117 |
| 12 | faceOn 100.5 ±18.7, 688 ±40 | faceOn+dtl3d 107.2 ±13.4, 823 ±109 |
| 13 | faceOn 93.9 ±27.4, 757 ±45 | faceOn+dtl3d 120.5 ±14.0, 807 ±116 |
| 14 | faceOn 87.1 ±20.0, 756 ±43 | faceOn+dtl3d 113.9 ±13.2, 839 ±111 |
| 15 | faceOn 93.8 ±19.5, 686 ±42 | faceOn+dtl3d 113.9 ±17.6, 801 ±117 |

**What changed, in summary:**

- **Route.** The skeleton arm fired on 14 of 15 swings; s5 stepped aside to the face-on arm.
- **Timing.** The median node moved from 100.5 ms before impact (face-on) to 107.3 ms. The
  swing-to-swing spread of the placed nodes went from 87–108 ms (sd 7.1, 14 placed) to 100–121 ms
  (sd 5.9, 15 placed).
  - On 5 swings (1, 3, 4, 6, 9) the node is in the same frame as before.
  - On 3 swings (2, 7, 12) it moved one frame (6.7 ms) earlier, and on s8 two frames (13 ms).
  - On 4 swings (11, 13, 14, 15) it moved 20–27 ms earlier. The arm curve has two humps of similar
    height, about 105 ms and about 35 ms before impact. On those swings the face-on route had
    picked the later edge of the early hump, and the skeleton picks its centre.
  - The pair route's offline measurement had the arm at −100 to −115 ms on these swings
    (`pair_span_turn_20260920.md`), which matches the skeleton.
- **Timing σ** fell from a median 18.7 ms to 14.3 ms.
- **Peak speed** rose from a median 702 to 819 °/s (Cheetham's reference: 980). The face-on
  ellipse de-projection under-read it.
- **Peak σ** rose from about 42 to about 115 °/s. That is the assumed-camera term, 10% of the peak,
  which falls to 3% once calibrated. It is stated, not hidden.
- **Verdicts.** s10 moved from unresolved to **partial** (its arm node is now placed). The other
  14 are unchanged: 12 partial, 2 unresolved (s1, s13).
- **Diagnostics.** None read the arm series or the sequence nodes, so there is no ledger effect
  from this change.

## For Mark to look at in the app (G4)

- **s8** — the swing everything started from: the arm node moves 94 → 107 ms, and σ_t 18 → 10 ms.
- **s10** — the verdict changes from unresolved to partial, because the arm is now placed.
- **s14** — the largest move, 87 → 114 ms, which is the two-hump case.
