# Why re-tracking a swing moved its positions (#154)

Measured 6 Nov 2026 on the cabin PC with `swinglab_run`, on copies of corpus swings; no corpus file
was changed. Population: the 31 corpus swings with hand-marked P1–P10 truth, plus the two swings in
the issue. "Before" is the beta7 build (8abadb8c); "after" is that build with the fix applied.
Data: `issue154_positions_before_fix.csv`, `issue154_positions_after_fix.csv`,
`issue154_metric_diffs_after_fix.csv`.

## What was wrong

**1. The camera-only two-pass pose used a different person crop.** The crop came from all the
coarse pass-1 frames, not the eight-frame scan the single-pass path uses, and pass-1 frames stayed
full-frame among cropped neighbours. On the same frames the wrists differed by a median of 5–7 px
and up to 122 px, and the positions read off the hand speed followed them.

Isolated on the two issue swings: with the crop off, the two-pass and full-window paths gave
identical positions on both, so the crop is the whole difference. Turning `shaft.spanBound` off
changed no position, and the frames posed from 1.8 s before impact onward were already the same.

**2. A fresh track and the same track reloaded gave different synths.** `EventRefine` moved P1 after
the synth was built and nothing rebuilt it. A reused track was rebuilt from the moved P1. On the
first issue swing that changed 17 metrics (`shaftLie` by 35.7°, `lagAngle` by 11.2°).

**3. A reused pose was re-stamped with the current run's scope.** A full-window pose became "span"
after a write-back, so a later `--full-window` re-posed it for nothing.

**4. Not a defect: `clubAngularSpeed` on a normal write-back.** The stored results predate the club
rate filter (shipped 24 Oct; the corpus was last swept 20 Oct), so a write-back was the first time
those swings went through it. The series is rebuilt from the same stored track.

Ruled out: randomness (two forced runs on one binary gave 0 of 77 and 0 of 103 metrics different)
and tracker drift (with `.raw` present a forced re-track reproduced stored positions, median and p90
move 0 at every position).

## Which answer is right

Absolute error against the hand marks, ms: median / p90 / worst (positions not found).

| Position | Stored | Two-pass before | After the fix |
|---|---|---|---|
| P1 | 87 / 147 / 288 | 47 / 127 / 181 | 82 / 141 / 241 |
| P2 | 8 / 12 / 16 | 10 / 16 / 17 | 7 / 13 / 17 |
| P3 | 40 / 49 / 65 | 37 / 48 / 65 | 39 / 48 / 65 |
| P4 | 20 / 47 / 60 | 27 / 54 / 61 | 20 / 34 / 60 |
| P5 | 16 / 24 / 30 | 16 / 23 / 32 (1) | 15 / 23 / 29 |
| P6 | 2 / 4 / 5 | 2 / 4 / 6 (1) | 1 / 4 / 5 |
| P7 | 0 / 7 / 20 | 0 / 7 / 20 | 0 / 7 / 20 |
| P8 | 4 / 12 / 21 | 4 / 14 / 71 (1) | 3 / 12 / 56 |
| P10 | 63 / 302 / 335 | 132 / 308 / 563 | 60 / 302 / 335 |

The old two-pass result was the worse one on P4, P8 and P10 and lost positions the others found. It
was better on P1 only. P10 marks are not consistent between sessions (about +500 ms after impact in
June and July, +280 in August, +220 in September), so its error is a weak yardstick.

## After the fix

- Fresh then reused: every position identical on all 33 swings.
- Two-pass against full window: same positions, except P2 by up to 2.8 ms, P6 by up to 0.3 ms and
  P8 by up to 39 ms, with p90 0 at each.
- Fresh then reused, metrics: 27 of 33 swings identical to 0.01; the other six differ by up to
  16 °/s on `clubAngularSpeed`. Two causes remain: the DTL tracker's witness comes from the decide
  trace on a fresh run and from sample flags on a reuse, and the follow-through check re-applied on
  a reuse can demote one more sample. On the three swings tested, later write-backs were identical
  (differences under 1e-6).
- Not closed: metric series still differ between the two-pass and `--full-window` runs, which also
  track the whole window. `--full-window` is a developer option; the app does not use it.
- `kPoseStageVersion` is 2, so the next re-analysis of a stored swing re-poses and re-tracks it once.
  With `.raw` that reproduced stored positions (P8 up to 6.5 ms). Without `.raw`, on these 33 swings
  from stored to after: P1 median 5 ms, worst 100; P4 worst 27; P8 worst 429; P10 worst 301.

## Limits found in review

An independent read of the change found no defect in it. It found these limits, none measured:

- A P1 angle rewrite in the ball-anchor step (`shaft_tracker.cpp`) also happens after the synth is
  built. It goes unrebuilt on a fresh run only when `ImpactAnchor` finds no ball.
- `ImpactAnchor` reads the pre-refine P1 on a fresh run and the refined P1 on a reuse.
- A sensor-bounded swing still measures its crop on its own range, so `--full-window` can give it a
  different crop. Camera-only swings are not affected.
- The eight-frame crop is tighter than the old one. Simulated on 23 swings, 3 had the hands outside
  it on one frame of about 220, and several had only 2–3 % margin at the top.
- The pose bump leaves the down-the-line pose reused, so a swing can hold a new face-on pose beside a
  DTL pose scheduled around its old ladder.
